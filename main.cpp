#include <iostream>
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <numbers>
#include <stdexcept>
#include <string>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define GLFW_EXPOSE_NATIVE_WIN32
#elif defined(__APPLE__)
#define GLFW_EXPOSE_NATIVE_COCOA
#else
#define GLFW_EXPOSE_NATIVE_X11
#endif
#include <GLFW/glfw3native.h>
#include <pixelKiln.h>

static const uint32_t simulateSpirv[] =
#include "simulate.comp.h"
;
static const uint32_t particleVertSpirv[] =
#include "particle.vert.h"
;
static const uint32_t particleFragSpirv[] =
#include "particle.frag.h"
;

#define WINDOW_SIZE 1000

#define NUM_TRIANGLES 12
#define NUM_PARTICLES 30720

// Simulated seconds per compute step. Fixed, so the simulation doesn't depend on the frame rate.
#define FIXED_TIME_STEP (1.0 / 240.0)
// A frame longer than this many steps (a stall, a suspend, a GPU that can't keep up) slows the simulation down
// instead of being caught up with ever more steps.
#define MAX_STEPS_PER_FRAME 8
// TILE_SIZE in simulate.comp, its workgroup size.
#define SIMULATION_GROUP_SIZE 128

// Matches the push_constant block in simulate.comp.
struct SimulationPushConstants {
    float deltaTime;
    uint32_t particleCount;
};

class NBodySimulator {
    private:
        // Declared first so it is destroyed last, after the destructor has released the swapchain and the window.
        PixelKiln m_kiln;
        PresentMode m_presentMode;
        GLFWwindow* m_window = nullptr;
        uint64_t m_swapchain = 0;
        SwapchainInfo m_swapchainInfo{};
        uint64_t m_computeProgram = 0;
        uint64_t m_graphicsProgram = 0;
        uint64_t m_vertexBuffer = 0;
        uint64_t m_indexBuffer = 0;
        uint64_t m_bodyPositionBuffers[2] = {0, 0};
        uint64_t m_bodyVelocityBuffers[2] = {0, 0};
        double m_lastTime = 0.0;
        double m_accumulatedTime = 0.0; // elapsed time not simulated yet, less than FIXED_TIME_STEP between frames
        double m_frameRateTime = 0.0;
        uint32_t m_frameRateFrames = 0;
        uint8_t m_currentState = 0;
        SimulationPushConstants m_pushConstants = {static_cast<float>(FIXED_TIME_STEP), NUM_PARTICLES};
        static Config createConfig() {
            Config config{};
            config.applicationName = "NBodySimulator";
            config.gpuType = DEDICATED;
#ifndef NDEBUG
            config.enableValidation = true;
#endif
            return config;
        }
        void createWindow() {
            if (!glfwInit()) {
                throw std::runtime_error("Failed to initialize GLFW");
            }
            glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
            glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
            m_window = glfwCreateWindow(WINDOW_SIZE, WINDOW_SIZE, "NBodySimulator", nullptr, nullptr);
            if (!m_window) {
                glfwTerminate();
                throw std::runtime_error("Failed to create GLFW window");
            }
        }
        NativeWindow getNativeWindow() const {
#if defined(_WIN32)
            return {NATIVE_WINDOW_WIN32, GetModuleHandleW(nullptr), glfwGetWin32Window(m_window)};
#elif defined(__APPLE__)
            return {NATIVE_WINDOW_COCOA_VIEW, nullptr, glfwGetCocoaView(m_window)};
#else
            return {NATIVE_WINDOW_XLIB, glfwGetX11Display(),
                    reinterpret_cast<void*>(static_cast<uintptr_t>(glfwGetX11Window(m_window)))};
#endif
        }
        void createSwapchain() {
            int framebufferWidth = 0;
            int framebufferHeight = 0;
            glfwGetFramebufferSize(m_window, &framebufferWidth, &framebufferHeight);
            SwapchainDesc desc{};
            desc.width = static_cast<uint32_t>(framebufferWidth);
            desc.height = static_cast<uint32_t>(framebufferHeight);
            desc.format = IMAGE_FORMAT_BGRA8_SRGB;
            desc.presentMode = m_presentMode;
            m_swapchain = m_kiln.createSwapchain(getNativeWindow(), desc);
            m_swapchainInfo = m_kiln.getSwapchainInfo(m_swapchain);
        }
        void createBuffers() {
            m_vertexBuffer = m_kiln.createBuffer(sizeof(float) * 2 * (NUM_TRIANGLES + 1), "Vertices");
            m_indexBuffer = m_kiln.createBuffer(sizeof(uint32_t) * 3 * NUM_TRIANGLES, "Indices");
            for (int i = 0; i < 2; ++i) {
                m_bodyPositionBuffers[i] = m_kiln.createBuffer(sizeof(float) * 2 * NUM_PARTICLES, "Body positions");
                m_bodyVelocityBuffers[i] = m_kiln.createBuffer(sizeof(float) * 2 * NUM_PARTICLES, "Body velocities");
            }
        }
        void createComputeProgram() {
            ComputeProgram program{};
            program.computeShader = {simulateSpirv, sizeof(simulateSpirv)};
            program.uniformBindings = {
                UNIFORM_BINDING_TYPE_STORAGE_BUFFER, // current positions
                UNIFORM_BINDING_TYPE_STORAGE_BUFFER, // current velocities
                UNIFORM_BINDING_TYPE_STORAGE_BUFFER, // new positions
                UNIFORM_BINDING_TYPE_STORAGE_BUFFER, // new velocities
            };
            program.pushConstantSize = sizeof(SimulationPushConstants);
            m_computeProgram = m_kiln.loadComputeProgram(program, "Simulate");
        }
        void createGraphicsProgram() {
            RasterDrawProgram program{};
            program.vertexShader = {particleVertSpirv, sizeof(particleVertSpirv)};
            program.fragmentShader = {particleFragSpirv, sizeof(particleFragSpirv)};
            // Slot 0: the particle shape's vertices, slot 1: one position per particle instance.
            program.vertexLayout.buffers = {{sizeof(float) * 2}, {sizeof(float) * 2, true}};
            program.vertexLayout.attributes = {{0, 0, VERTEX_FORMAT_FLOAT2, 0}, {1, 1, VERTEX_FORMAT_FLOAT2, 0}};
            program.topology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            program.cullMode = CULL_MODE_NONE;
            program.colorFormats = {m_swapchainInfo.format};
            m_graphicsProgram = m_kiln.loadRasterDrawProgram(program, "Particles");
        }
        void generateVertices(){
            float vertices[(NUM_TRIANGLES + 1) * 2];
            for (int i = 0; i < NUM_TRIANGLES; ++i) {
                float angle = i * 2.0f * std::numbers::pi_v<float> / NUM_TRIANGLES;
                vertices[i * 2] = cos(angle);
                vertices[i * 2 + 1] = sin(angle);
            }
            vertices[NUM_TRIANGLES * 2] = 0.0f;
            vertices[NUM_TRIANGLES * 2 + 1] = 0.0f;
            m_kiln.uploadBuffer(m_vertexBuffer, vertices, sizeof(vertices));
        };
        void generateIndices(){
            uint32_t indices[NUM_TRIANGLES * 3];
            for (int i = 0; i < NUM_TRIANGLES; ++i) {
                indices[i * 3] = i;
                indices[i * 3 + 1] = (i + 1) % NUM_TRIANGLES;
                indices[i * 3 + 2] = NUM_TRIANGLES; // the center vertex
            }
            m_kiln.uploadBuffer(m_indexBuffer, indices, sizeof(indices));
        };
        void generateBodies(){
            std::vector<float> positions(NUM_PARTICLES * 2);
            std::vector<float> velocities(NUM_PARTICLES * 2);
            for (int i = 0; i < NUM_PARTICLES; ++i) {
                float randomPosXValue = 1.0 - (2.0 * static_cast<float>(rand()) / RAND_MAX);
                float randomPosYValue = 1.0 - (2.0 * static_cast<float>(rand()) / RAND_MAX);
                float randomVolXValue = 0.25 - (0.5 * static_cast<float>(rand()) / RAND_MAX);
                float randomVolYValue = 0.25 - (0.5 * static_cast<float>(rand()) / RAND_MAX);
                positions[i * 2] = randomPosXValue;
                positions[i * 2 + 1] = randomPosYValue;
                velocities[i * 2] = randomVolXValue;
                velocities[i * 2 + 1] = randomVolYValue;
            }
            m_kiln.uploadBuffer(m_bodyPositionBuffers[m_currentState], positions.data(), sizeof(float) * positions.size());
            m_kiln.uploadBuffer(m_bodyVelocityBuffers[m_currentState], velocities.data(), sizeof(float) * velocities.size());
            m_lastTime = glfwGetTime();
        };
        // Seconds since the previous call, from GLFW's monotonic clock.
        double updateTime(){
            double currentTime = glfwGetTime();
            double elapsedTime = currentTime - m_lastTime;
            m_lastTime = currentTime;
            return elapsedTime;
        };
        // Averaged over half a second: a per frame value is unreadable, and setting the title is a round trip to the
        // window system, which adds up without vsync.
        void updateFrameRate(double elapsedTime){
            m_frameRateTime += elapsedTime;
            m_frameRateFrames++;
            if (m_frameRateTime >= 0.5) {
                glfwSetWindowTitle(m_window, std::to_string(std::lround(m_frameRateFrames / m_frameRateTime)).c_str());
                m_frameRateTime = 0.0;
                m_frameRateFrames = 0;
            }
        };
        // Records the fixed steps the elapsed time covers, the remainder carries over to the next frame.
        void advanceSimulation(double elapsedTime){
            m_accumulatedTime += std::min(elapsedTime, MAX_STEPS_PER_FRAME * FIXED_TIME_STEP);
            while (m_accumulatedTime >= FIXED_TIME_STEP) {
                computeNextState();
                m_accumulatedTime -= FIXED_TIME_STEP;
            }
        };
        void computeNextState(){
            uint8_t nextState = (m_currentState + 1) % 2;
            ProgramCall call{};
            call.type = PROGRAM_TYPE_COMPUTE;
            call.program = m_computeProgram;
            call.bindings = {
                {.resource = m_bodyPositionBuffers[m_currentState]},
                {.resource = m_bodyVelocityBuffers[m_currentState]},
                {.resource = m_bodyPositionBuffers[nextState]},
                {.resource = m_bodyVelocityBuffers[nextState]},
            };
            call.pushConstants = &m_pushConstants;
            call.groupCountX = (NUM_PARTICLES + SIMULATION_GROUP_SIZE - 1) / SIMULATION_GROUP_SIZE;
            call.debugLabel = "Simulate";
            m_kiln.record(call);
            m_currentState = nextState;
        };
        void renderCurrentState(uint64_t image){
            ProgramCall call{};
            call.type = PROGRAM_TYPE_RASTER_DRAW;
            call.program = m_graphicsProgram;
            call.colorTargets = {{image, true, {0.0f, 0.0f, 0.0f, 1.0f}}};
            call.vertexBuffers = {m_vertexBuffer, m_bodyPositionBuffers[m_currentState]};
            call.indexBuffer = m_indexBuffer;
            call.indexType = INDEX_TYPE_UINT32;
            call.indexCount = NUM_TRIANGLES * 3;
            call.instanceCount = NUM_PARTICLES;
            call.debugLabel = "Render";
            m_kiln.record(call);
        };
    public:
        explicit NBodySimulator(PresentMode presentMode) : m_kiln(createConfig()), m_presentMode(presentMode) {
            createWindow();
            createSwapchain();
            createBuffers();
            createComputeProgram();
            createGraphicsProgram();
            generateVertices();
            generateIndices();
            generateBodies();
        }
        void run() {
            while (!glfwWindowShouldClose(m_window)) {
                glfwPollEvents();
                uint64_t image = m_kiln.acquireSwapchainImage(m_swapchain);
                // Measured every iteration, so time spent without an image isn't simulated afterwards.
                double elapsedTime = updateTime();
                if (image == 0) {
                    // Nothing to draw into (e.g. minimized): the simulation pauses too.
                    glfwWaitEventsTimeout(0.1);
                    continue;
                }
                updateFrameRate(elapsedTime);
                advanceSimulation(elapsedTime);
                // The steps are submitted together, before the draw is recorded: the draw's submission waits for the
                // window to release the swapchain image, the steps don't have to.
                m_kiln.flush();
                renderCurrentState(image);
                m_kiln.present(m_swapchain);
            }
        }
        ~NBodySimulator() {
            m_kiln.unloadProgram(m_graphicsProgram);
            m_kiln.unloadProgram(m_computeProgram);
            for (int i = 0; i < 2; ++i) {
                m_kiln.destroyBuffer(m_bodyPositionBuffers[i]);
                m_kiln.destroyBuffer(m_bodyVelocityBuffers[i]);
            }
            m_kiln.destroyBuffer(m_indexBuffer);
            m_kiln.destroyBuffer(m_vertexBuffer);
            // Waits for the GPU, and must happen before the window it presents to is destroyed.
            m_kiln.destroySwapchain(m_swapchain);
            glfwDestroyWindow(m_window);
            glfwTerminate();
        }
};

int main(int argc, char** argv) {
    // --no-vsync presents without waiting for the display (where supported), to see how fast it can run.
    PresentMode presentMode = PRESENT_MODE_VSYNC;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--no-vsync") {
            presentMode = PRESENT_MODE_IMMEDIATE;
        } else {
            std::cerr << "Unknown argument " << argv[i] << ", usage: " << argv[0] << " [--no-vsync]" << std::endl;
            return EXIT_FAILURE;
        }
    }
    try {
        NBodySimulator nBodySimulator(presentMode);
        nBodySimulator.run();
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    return 0;
}
