#include <iostream>
#include <vector>
#include <cmath>
#include <chrono>
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

// Matches the push_constant block in simulate.comp.
struct SimulationPushConstants {
    float deltaTime;
    uint32_t particleCount;
};

class NBodySimulator {
    private:
        // Declared first so it is destroyed last, after the destructor has released the swapchain and the window.
        PixelKiln m_kiln;
        GLFWwindow* m_window = nullptr;
        uint64_t m_swapchain = 0;
        SwapchainInfo m_swapchainInfo{};
        uint64_t m_computeProgram = 0;
        uint64_t m_graphicsProgram = 0;
        uint64_t m_vertexBuffer = 0;
        uint64_t m_indexBuffer = 0;
        uint64_t m_bodyPositionBuffers[2] = {0, 0};
        uint64_t m_bodyVelocityBuffers[2] = {0, 0};
        std::chrono::time_point<std::chrono::high_resolution_clock> m_lastTime;
        uint8_t m_currentState = 0;
        SimulationPushConstants m_pushConstants = {0.0f, NUM_PARTICLES};
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
            desc.presentMode = PRESENT_MODE_VSYNC;
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
            m_lastTime = std::chrono::high_resolution_clock::now();
        };
        float updateTime(){
            auto currentTime = std::chrono::high_resolution_clock::now();
            auto elapsedTime = std::chrono::duration_cast<std::chrono::microseconds>(currentTime - m_lastTime).count();
            m_lastTime = currentTime;
            glfwSetWindowTitle(m_window, std::to_string(static_cast<uint32_t>(1/(elapsedTime / 1000000.0f))).c_str());
            return elapsedTime / 1000000.0f;
        };
        void computeNextState(float deltaTime){
            m_pushConstants.deltaTime = deltaTime;
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
            call.groupCountX = (NUM_PARTICLES / 64) + 1;
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
        NBodySimulator() : m_kiln(createConfig()) {
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
                // Measured every iteration, so time spent without an image doesn't become one huge step.
                float deltaTime = updateTime();
                if (image == 0) {
                    // Nothing to draw into (e.g. minimized): the simulation pauses too.
                    glfwWaitEventsTimeout(0.1);
                    continue;
                }
                computeNextState(deltaTime);
                // The step is submitted on its own, before the draw is recorded: the draw's submission waits for the
                // window to release the swapchain image, the step doesn't have to.
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

int main() {
    try {
        NBodySimulator nBodySimulator;
        nBodySimulator.run();
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    return 0;
}
