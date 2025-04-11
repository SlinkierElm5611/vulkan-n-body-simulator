#include <iostream>
#define VMA_IMPLEMENTATION
#define VMA_VULKAN_VERSION 1002000
#include <vk_mem_alloc.h>
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vector>
#include "comp.h"
#include "vert.h"
#include "frag.h"

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

#define WINDOW_SIZE 800

#define NUMBER_OF_STATES 3
#define FRAMES_IN_FLIGHT 2
#define STATE_READY_FOR_COMPUTE 0
#define STATE_READY_FOR_RENDER 1
#define STATE_READY_FOR_SOURCE_OF_NEXT_COMPUTE 2

#define NUM_TRIANGLES 10
#define NUM_PARTICLES 1000

class NBodySimulator {
    private:
        GLFWwindow* m_window;
        vk::Instance m_instance;
        vk::PhysicalDevice m_physicalDevice;
        vk::PhysicalDeviceType m_physicalDeviceType;
        vk::Device m_device;
        vk::Queue m_queue;
        VmaAllocator m_allocator;
        vk::CommandPool m_commandPool;
        std::vector<vk::CommandBuffer> m_commandBuffers;
        vk::Buffer m_vertexBuffer;
        VmaAllocation m_vertexBufferAllocation;
        void* m_mappedVertexBuffer;
        vk::Buffer m_indexBuffer;
        VmaAllocation m_indexBufferAllocation;
        void* m_mappedIndexBuffer;
        vk::Buffer m_bodyPositionBuffers[NUMBER_OF_STATES];
        VmaAllocation m_bodyPositionBufferAllocations[NUMBER_OF_STATES];
        void* m_mappedBodyPositionBuffers[NUMBER_OF_STATES];
        vk::Buffer m_bodyVelocityBuffers[NUMBER_OF_STATES];
        VmaAllocation m_bodyVelocityBufferAllocations[NUMBER_OF_STATES];
        void* m_mappedBodyVelocityBuffers[NUMBER_OF_STATES];
        vk::Buffer m_stagingBuffer;
        VmaAllocation m_stagingBufferAllocation;
        void* m_mappedStagingBuffer;
        uint8_t m_currentState = 0;
        uint8_t m_nextState = 1;
        vk::Semaphore m_stateSemaphores[NUMBER_OF_STATES];
        vk::Semaphore m_imageAvailableSemaphores[NUMBER_OF_STATES];
        vk::Fence m_inFlightFences[NUMBER_OF_STATES];
        vk::DescriptorSetLayout m_computeDescriptorSetLayout;
        vk::PipelineLayout m_computePipelineLayout;
        vk::Pipeline m_computePipeline;
        vk::PipelineCache m_computePipelineCache;
        vk::RenderPass m_renderPass;
        vk::PipelineLayout m_graphicsPipelineLayout;
        vk::Pipeline m_graphicsPipeline;
        vk::PipelineCache m_graphicsPipelineCache;
        vk::SurfaceKHR m_surface;
        vk::SwapchainKHR m_swapChain;
        vk::Image m_swapChainImages[FRAMES_IN_FLIGHT];
        vk::ImageView m_swapChainImageViews[FRAMES_IN_FLIGHT];
        vk::Framebuffer m_swapChainFramebuffers[FRAMES_IN_FLIGHT];
        void createWindow() {
            if (!glfwInit()) {
                throw std::runtime_error("Failed to initialize GLFW");
            }
            glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
            glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
            m_window = glfwCreateWindow(WINDOW_SIZE, WINDOW_SIZE, "NBodySimulator", nullptr, nullptr);
            if (!m_window) {
                throw std::runtime_error("Failed to create GLFW window");
            }
        }
        void createInstance() {
            VULKAN_HPP_DEFAULT_DISPATCHER.init();
            vk::ApplicationInfo appInfo{};
            appInfo.pApplicationName = "NBodySimulator";
            appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
            appInfo.pEngineName = "NBodySimulator";
            appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
            appInfo.apiVersion = VK_API_VERSION_1_2;
            uint32_t glfwExtensionCount = 0;
            const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
            std::vector<const char*> instanceExtensions(glfwExtensions, glfwExtensions + glfwExtensionCount);
            instanceExtensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
            std::vector<const char*> instanceLayers = {
                "VK_LAYER_KHRONOS_validation",
            };
            vk::InstanceCreateInfo createInfo{};
            createInfo.pApplicationInfo = &appInfo;
            createInfo.enabledExtensionCount = static_cast<uint32_t>(instanceExtensions.size());
            createInfo.ppEnabledExtensionNames = instanceExtensions.data();
            createInfo.enabledLayerCount = static_cast<uint32_t>(instanceLayers.size());
            createInfo.ppEnabledLayerNames = instanceLayers.data();
            createInfo.flags = vk::InstanceCreateFlagBits::eEnumeratePortabilityKHR;
            m_instance = vk::createInstance(createInfo);
            VULKAN_HPP_DEFAULT_DISPATCHER.init(m_instance);
        }
        void createPhysicalDevice() {
            std::vector<vk::PhysicalDevice> physicalDevices = m_instance.enumeratePhysicalDevices();
            std::vector<vk::PhysicalDeviceType> deviceTypes = {
                vk::PhysicalDeviceType::eDiscreteGpu,
                vk::PhysicalDeviceType::eIntegratedGpu,
                vk::PhysicalDeviceType::eCpu,
            };
            for (const auto& deviceType : deviceTypes) {
                for (const auto& physicalDevice : physicalDevices) {
                    vk::PhysicalDeviceProperties properties = physicalDevice.getProperties();
                    if (properties.deviceType == deviceType) {
                        m_physicalDevice = physicalDevice;
                        m_physicalDeviceType = deviceType;
                        break;
                    }
                }
                if (m_physicalDevice) {
                    break;
                }
            }
        }
        void createDevice() {
            vk::PhysicalDeviceVulkan12Features features12{};
            features12.setTimelineSemaphore(VK_TRUE);
            vk::PhysicalDeviceFeatures2 features{};
            features.setPNext(&features12);
            std::vector<const char*> deviceExtensions = {
                VK_KHR_SWAPCHAIN_EXTENSION_NAME,
                VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME,
                "VK_KHR_portability_subset",
            };
            std::vector<vk::DeviceQueueCreateInfo> queueCreateInfos;
            std::vector<float> queuePriorities = {1.0f};
            std::vector<vk::PhysicalDeviceFeatures> deviceFeatures;
            vk::DeviceQueueCreateInfo queueCreateInfo{};
            queueCreateInfo.queueFamilyIndex = 0;
            queueCreateInfo.queueCount = 1;
            queueCreateInfo.pQueuePriorities = queuePriorities.data();
            queueCreateInfos.push_back(queueCreateInfo);
            vk::DeviceCreateInfo createInfo{};
            createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
            createInfo.pQueueCreateInfos = queueCreateInfos.data();
            createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
            createInfo.ppEnabledExtensionNames = deviceExtensions.data();
            createInfo.pNext = &features;
            m_device = m_physicalDevice.createDevice(createInfo);
            m_queue = m_device.getQueue(0, 0);
            VULKAN_HPP_DEFAULT_DISPATCHER.init(m_device);
        }
        void createAllocator() {
            VmaAllocatorCreateInfo allocatorInfo{};
            allocatorInfo.physicalDevice = m_physicalDevice;
            allocatorInfo.device = m_device;
            allocatorInfo.instance = m_instance;
            vmaCreateAllocator(&allocatorInfo, &m_allocator);
        }
        void createCommandPool() {
            vk::CommandPoolCreateInfo createInfo{};
            createInfo.queueFamilyIndex = 0;
            createInfo.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
            m_commandPool = m_device.createCommandPool(createInfo);
        }
        void createCommandBuffer() {
            vk::CommandBufferAllocateInfo allocateInfo{};
            allocateInfo.commandPool = m_commandPool;
            allocateInfo.level = vk::CommandBufferLevel::ePrimary;
            allocateInfo.commandBufferCount = FRAMES_IN_FLIGHT;
            for (const auto& newCommandBuffer : m_device.allocateCommandBuffers(allocateInfo)){
                m_commandBuffers.push_back(newCommandBuffer);
            }
        }
        void createVertexBuffer() {
            vk::BufferCreateInfo bufferInfo{};
            bufferInfo.size = sizeof(float) * 2 * (NUM_TRIANGLES + 1);
            bufferInfo.usage = vk::BufferUsageFlagBits::eVertexBuffer;
            VmaAllocationCreateInfo allocInfo{};
            allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
            if(m_physicalDeviceType == vk::PhysicalDeviceType::eDiscreteGpu) {
                bufferInfo.usage |= vk::BufferUsageFlagBits::eTransferDst;
            }else{
                allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
            }
            VmaAllocationInfo info{};
            vmaCreateBuffer(m_allocator, reinterpret_cast<VkBufferCreateInfo*>(&bufferInfo),
                            reinterpret_cast<VmaAllocationCreateInfo*>(&allocInfo),
                            reinterpret_cast<VkBuffer*>(&m_vertexBuffer),
                            &m_vertexBufferAllocation, &info);
            m_mappedVertexBuffer = info.pMappedData;
        }
        void createIndexBuffer() {
            vk::BufferCreateInfo bufferInfo{};
            bufferInfo.size = sizeof(uint32_t) * 3 * NUM_TRIANGLES;
            bufferInfo.usage = vk::BufferUsageFlagBits::eIndexBuffer;
            VmaAllocationCreateInfo allocInfo{};
            allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
            if(m_physicalDeviceType == vk::PhysicalDeviceType::eDiscreteGpu) {
                bufferInfo.usage |= vk::BufferUsageFlagBits::eTransferDst;
            }else{
                allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
            }
            VmaAllocationInfo info{};
            vmaCreateBuffer(m_allocator, reinterpret_cast<VkBufferCreateInfo*>(&bufferInfo),
                            reinterpret_cast<VmaAllocationCreateInfo*>(&allocInfo),
                            reinterpret_cast<VkBuffer*>(&m_indexBuffer),
                            &m_indexBufferAllocation, &info);
            m_mappedIndexBuffer = info.pMappedData;
        };
        void createBodyPositionBuffers(){
            vk::BufferCreateInfo bufferInfo{};
            bufferInfo.size = sizeof(float) * 2 * NUM_PARTICLES;
            bufferInfo.usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eVertexBuffer;
            VmaAllocationCreateInfo allocInfo{};
            allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
            if(m_physicalDeviceType == vk::PhysicalDeviceType::eDiscreteGpu) {
                bufferInfo.usage |= vk::BufferUsageFlagBits::eTransferDst;
            }else{
                allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
            }
            VmaAllocationInfo info{};
            for (int i = 0; i < NUMBER_OF_STATES; ++i) {
                vmaCreateBuffer(m_allocator, reinterpret_cast<VkBufferCreateInfo*>(&bufferInfo),
                                reinterpret_cast<VmaAllocationCreateInfo*>(&allocInfo),
                                reinterpret_cast<VkBuffer*>(&m_bodyPositionBuffers[i]),
                                &m_bodyPositionBufferAllocations[i], &info);
                m_mappedBodyPositionBuffers[i] = info.pMappedData;
            }
        };
        void createBodyVelocityBuffers(){
            vk::BufferCreateInfo bufferInfo{};
            bufferInfo.size = sizeof(float) * 2 * NUM_PARTICLES;
            bufferInfo.usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eVertexBuffer;
            VmaAllocationCreateInfo allocInfo{};
            allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
            if(m_physicalDeviceType == vk::PhysicalDeviceType::eDiscreteGpu) {
                bufferInfo.usage |= vk::BufferUsageFlagBits::eTransferDst;
            }else{
                allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
            }
            VmaAllocationInfo info{};
            for (int i = 0; i < NUMBER_OF_STATES; ++i) {
                vmaCreateBuffer(m_allocator, reinterpret_cast<VkBufferCreateInfo*>(&bufferInfo),
                                reinterpret_cast<VmaAllocationCreateInfo*>(&allocInfo),
                                reinterpret_cast<VkBuffer*>(&m_bodyVelocityBuffers[i]),
                                &m_bodyVelocityBufferAllocations[i], &info);
                m_mappedBodyVelocityBuffers[i] = info.pMappedData;
            }
        };
        void createStagingBuffer() {
            vk::BufferCreateInfo bufferInfo{};
            bufferInfo.size = sizeof(float) * 2 * 1000;
            bufferInfo.usage = vk::BufferUsageFlagBits::eTransferSrc;
            VmaAllocationCreateInfo allocInfo{};
            allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
            allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
            VmaAllocationInfo info{};
            vmaCreateBuffer(m_allocator, reinterpret_cast<VkBufferCreateInfo*>(&bufferInfo),
                            reinterpret_cast<VmaAllocationCreateInfo*>(&allocInfo),
                            reinterpret_cast<VkBuffer*>(&m_stagingBuffer),
                            &m_stagingBufferAllocation, &info);
            m_mappedStagingBuffer = info.pMappedData;
        };
        void createSyncObjects(){
            vk::FenceCreateInfo fenceInfo{};
            fenceInfo.flags = vk::FenceCreateFlagBits::eSignaled;
            vk::SemaphoreCreateInfo semaphoreInfo{};
            for (int i = 0; i < FRAMES_IN_FLIGHT; ++i) {
                m_imageAvailableSemaphores[i] = m_device.createSemaphore(semaphoreInfo);
                m_inFlightFences[i] = m_device.createFence(fenceInfo);
            }
            vk::SemaphoreCreateInfo stateSemaphoreCreateInfo{};
            vk::SemaphoreTypeCreateInfo stateSemaphoreTypeCreateInfo{};
            stateSemaphoreTypeCreateInfo.semaphoreType = vk::SemaphoreType::eTimeline;
            stateSemaphoreTypeCreateInfo.initialValue = STATE_READY_FOR_COMPUTE;
            stateSemaphoreCreateInfo.pNext = &stateSemaphoreTypeCreateInfo;
            for (int i = 0; i < NUMBER_OF_STATES; ++i) {
                m_stateSemaphores[i] = m_device.createSemaphore(stateSemaphoreCreateInfo);
            }
        };
        void createComputeDescriptorSetLayout(){
            vk::DescriptorSetLayoutBinding bindings[4];
            for(uint8_t i=0; i<4; i++){
                bindings[i].binding = i;
                bindings[i].descriptorType = vk::DescriptorType::eStorageBuffer;
                bindings[i].descriptorCount = 1;
                bindings[i].stageFlags = vk::ShaderStageFlagBits::eCompute;
            }
            vk::DescriptorSetLayoutCreateInfo createInfo{};
            createInfo.bindingCount = 4;
            createInfo.pBindings = bindings;
            createInfo.flags = vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptor;
            m_computeDescriptorSetLayout = m_device.createDescriptorSetLayout(createInfo);
        };
        void createComputePipelineLayout(){
            vk::PipelineLayoutCreateInfo createInfo{};
            createInfo.setLayoutCount = 1;
            createInfo.pSetLayouts = &m_computeDescriptorSetLayout;
            vk::PushConstantRange pushConstantInfo{};
            pushConstantInfo.offset = 0;
            pushConstantInfo.size = sizeof(uint32_t) + sizeof(float);
            pushConstantInfo.stageFlags = vk::ShaderStageFlagBits::eCompute;
            createInfo.pushConstantRangeCount = 1;
            createInfo.pPushConstantRanges = &pushConstantInfo;
            m_computePipelineLayout = m_device.createPipelineLayout(createInfo);
        };
        void createComputePipeline(){
            vk::ShaderModuleCreateInfo shaderModuleInfo{};
            shaderModuleInfo.codeSize = comp_spv_len;
            shaderModuleInfo.pCode = reinterpret_cast<const uint32_t*>(comp_spv);
            vk::ShaderModule shaderModule = m_device.createShaderModule(shaderModuleInfo);
            vk::PipelineShaderStageCreateInfo shaderStageInfo{};
            shaderStageInfo.stage = vk::ShaderStageFlagBits::eCompute;
            shaderStageInfo.module = shaderModule;
            shaderStageInfo.pName = "main";
            shaderStageInfo.pSpecializationInfo = nullptr;
            vk::ComputePipelineCreateInfo pipelineInfo{};
            pipelineInfo.stage = shaderStageInfo;
            pipelineInfo.layout = m_computePipelineLayout;
            m_computePipeline = m_device.createComputePipeline(m_computePipelineCache, pipelineInfo).value;
            m_device.destroyShaderModule(shaderModule);
        };
        void createRenderPass(){
            vk::AttachmentDescription colourAttachment{};
            colourAttachment.format = vk::Format::eB8G8R8A8Srgb;
            colourAttachment.samples = vk::SampleCountFlagBits::e1;
            colourAttachment.loadOp = vk::AttachmentLoadOp::eClear;
            colourAttachment.storeOp = vk::AttachmentStoreOp::eStore;
            colourAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
            colourAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
            colourAttachment.initialLayout = vk::ImageLayout::eUndefined;
            colourAttachment.finalLayout = vk::ImageLayout::ePresentSrcKHR;
            vk::AttachmentReference colourAttachmentRef{};
            colourAttachmentRef.attachment = 0;
            colourAttachmentRef.layout = vk::ImageLayout::eColorAttachmentOptimal;
            vk::SubpassDescription subpass{};
            subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
            subpass.colorAttachmentCount = 1;
            subpass.pColorAttachments = &colourAttachmentRef;
            vk::SubpassDependency dependency{};
            dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
            dependency.dstSubpass = 0;
            dependency.srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
            dependency.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
            dependency.dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
            dependency.dstAccessMask = vk::AccessFlagBits::eColorAttachmentRead;
            vk::RenderPassCreateInfo renderPassInfo{};
            renderPassInfo.attachmentCount = 1;
            renderPassInfo.pAttachments = &colourAttachment;
            renderPassInfo.subpassCount = 1;
            renderPassInfo.pSubpasses = &subpass;
            renderPassInfo.dependencyCount = 1;
            renderPassInfo.pDependencies = &dependency;
            m_renderPass = m_device.createRenderPass(renderPassInfo);
        };
        void createGraphicsPipelineLayout(){
            vk::PipelineLayoutCreateInfo createInfo{};
            m_graphicsPipelineLayout = m_device.createPipelineLayout(createInfo);
        };
        void createGraphicsPipeline(){
            vk::PipelineShaderStageCreateInfo shaderStages[2];
            vk::ShaderModuleCreateInfo vertShaderModuleInfo{};
            vertShaderModuleInfo.codeSize = vert_spv_len;
            vertShaderModuleInfo.pCode = reinterpret_cast<const uint32_t*>(vert_spv);
            vk::ShaderModule vertShaderModule = m_device.createShaderModule(vertShaderModuleInfo);
            shaderStages[0].stage = vk::ShaderStageFlagBits::eVertex;
            shaderStages[0].module = vertShaderModule;
            shaderStages[0].pName = "main";
            vk::ShaderModuleCreateInfo fragShaderModuleInfo{};
            fragShaderModuleInfo.codeSize = frag_spv_len;
            fragShaderModuleInfo.pCode = reinterpret_cast<const uint32_t*>(frag_spv);
            vk::ShaderModule fragShaderModule = m_device.createShaderModule(fragShaderModuleInfo);
            shaderStages[1].stage = vk::ShaderStageFlagBits::eFragment;
            shaderStages[1].module = fragShaderModule;
            shaderStages[1].pName = "main";
            vk::PipelineVertexInputStateCreateInfo vertexInputInfo{};
            vertexInputInfo.vertexBindingDescriptionCount = 2;
            vk::VertexInputBindingDescription bindingDescriptions[2];
            bindingDescriptions[0].binding = 0;
            bindingDescriptions[0].stride = sizeof(float) * 2;
            bindingDescriptions[0].inputRate = vk::VertexInputRate::eVertex;
            bindingDescriptions[1].binding = 1;
            bindingDescriptions[1].stride = sizeof(float) * 2;
            bindingDescriptions[1].inputRate = vk::VertexInputRate::eInstance;
            vk::VertexInputAttributeDescription attributeDescriptions[2];
            attributeDescriptions[0].binding = 0;
            attributeDescriptions[0].location = 0;
            attributeDescriptions[0].format = vk::Format::eR32G32Sfloat;
            attributeDescriptions[0].offset = 0;
            attributeDescriptions[1].binding = 1;
            attributeDescriptions[1].location = 1;
            attributeDescriptions[1].format = vk::Format::eR32G32Sfloat;
            attributeDescriptions[1].offset = 0;
            vertexInputInfo.pVertexBindingDescriptions = bindingDescriptions;
            vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions;
            vertexInputInfo.vertexBindingDescriptionCount = 2;
            vertexInputInfo.vertexAttributeDescriptionCount = 2;
            vk::PipelineInputAssemblyStateCreateInfo inputAssembly{};
            inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;
            inputAssembly.primitiveRestartEnable = VK_FALSE;
            vk::Viewport viewport{};
            viewport.x = 0.0f;
            viewport.y = 0.0f;
            viewport.width = WINDOW_SIZE;
            viewport.height = WINDOW_SIZE;
            viewport.minDepth = 0.0f;
            viewport.maxDepth = 1.0f;
            vk::Rect2D scissor{{0, 0}, {WINDOW_SIZE, WINDOW_SIZE}};
            vk::PipelineViewportStateCreateInfo viewportState{};
            viewportState.viewportCount = 1;
            viewportState.pViewports = &viewport;
            viewportState.scissorCount = 1;
            viewportState.pScissors = &scissor;
            vk::PipelineRasterizationStateCreateInfo rasterizer{};
            rasterizer.depthClampEnable = VK_FALSE;
            rasterizer.rasterizerDiscardEnable = VK_FALSE;
            rasterizer.polygonMode = vk::PolygonMode::eFill;
            rasterizer.lineWidth = 1.0f;
            rasterizer.cullMode = vk::CullModeFlagBits::eBack;
            rasterizer.frontFace = vk::FrontFace::eCounterClockwise;
            rasterizer.depthBiasEnable = VK_FALSE;
            rasterizer.depthBiasConstantFactor = 0.0f;
            rasterizer.depthBiasClamp = 0.0f;
            rasterizer.depthBiasSlopeFactor = 0.0f;
            vk::PipelineMultisampleStateCreateInfo multisampling{};
            multisampling.sampleShadingEnable = VK_FALSE;
            multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;
            multisampling.minSampleShading = 1.0f;
            multisampling.pSampleMask = nullptr;
            multisampling.alphaToCoverageEnable = VK_FALSE;
            multisampling.alphaToOneEnable = VK_FALSE;
            vk::PipelineColorBlendAttachmentState colorBlendAttachment{};
            colorBlendAttachment.blendEnable = VK_FALSE;
            colorBlendAttachment.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                                                  vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
            colorBlendAttachment.srcColorBlendFactor = vk::BlendFactor::eOne;
            colorBlendAttachment.dstColorBlendFactor = vk::BlendFactor::eZero;
            colorBlendAttachment.colorBlendOp = vk::BlendOp::eAdd;
            colorBlendAttachment.srcAlphaBlendFactor = vk::BlendFactor::eOne;
            colorBlendAttachment.dstAlphaBlendFactor = vk::BlendFactor::eZero;
            colorBlendAttachment.alphaBlendOp = vk::BlendOp::eAdd;
            vk::PipelineColorBlendStateCreateInfo colorBlending{};
            colorBlending.logicOpEnable = VK_FALSE;
            colorBlending.logicOp = vk::LogicOp::eCopy;
            colorBlending.attachmentCount = 1;
            colorBlending.pAttachments = &colorBlendAttachment;
            colorBlending.blendConstants[0] = 0.0f;
            colorBlending.blendConstants[1] = 0.0f;
            colorBlending.blendConstants[2] = 0.0f;
            colorBlending.blendConstants[3] = 0.0f;
            vk::GraphicsPipelineCreateInfo pipelineInfo{};
            pipelineInfo.stageCount = 2;
            pipelineInfo.pStages = shaderStages;
            pipelineInfo.pVertexInputState = &vertexInputInfo;
            pipelineInfo.pInputAssemblyState = &inputAssembly;
            pipelineInfo.pViewportState = &viewportState;
            pipelineInfo.pRasterizationState = &rasterizer;
            pipelineInfo.pMultisampleState = &multisampling;
            pipelineInfo.pDepthStencilState = nullptr;
            pipelineInfo.pColorBlendState = &colorBlending;
            pipelineInfo.pDynamicState = nullptr;
            pipelineInfo.layout = m_graphicsPipelineLayout;
            pipelineInfo.renderPass = m_renderPass;
            pipelineInfo.subpass = 0;
            pipelineInfo.basePipelineHandle = nullptr;
            pipelineInfo.basePipelineIndex = -1;
            pipelineInfo.flags = vk::PipelineCreateFlags();
            m_graphicsPipeline = m_device.createGraphicsPipeline(m_graphicsPipelineCache, pipelineInfo).value;
            m_device.destroyShaderModule(vertShaderModule);
            m_device.destroyShaderModule(fragShaderModule);
        };
        void createSurface() {
            if (glfwCreateWindowSurface(m_instance, m_window, nullptr, reinterpret_cast<VkSurfaceKHR*>(&m_surface)) != VK_SUCCESS) {
                throw std::runtime_error("Failed to create window surface");
            }
        };
        void createSwapchain(){
            vk::SwapchainCreateInfoKHR createInfo{};
            createInfo.surface = m_surface;
            createInfo.minImageCount = FRAMES_IN_FLIGHT;
            createInfo.imageFormat = vk::Format::eB8G8R8A8Srgb;
            createInfo.imageColorSpace = vk::ColorSpaceKHR::eSrgbNonlinear;
            createInfo.imageExtent.width = WINDOW_SIZE;
            createInfo.imageExtent.height = WINDOW_SIZE;
            createInfo.imageArrayLayers = 1;
            createInfo.imageUsage = vk::ImageUsageFlagBits::eColorAttachment;
            createInfo.preTransform = vk::SurfaceTransformFlagBitsKHR::eIdentity;
            createInfo.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
            createInfo.presentMode = vk::PresentModeKHR::eFifo;
            createInfo.clipped = VK_TRUE;
            m_swapChain = m_device.createSwapchainKHR(createInfo);
        };
        void getSwapchainImages(){
            uint32_t counter = 0;
            for (const auto& swapChainImage : m_device.getSwapchainImagesKHR(m_swapChain)){
                m_swapChainImages[counter] = swapChainImage;
                counter++;
            }
        };
        void createSwapchainImageViews(){
            vk::ImageViewCreateInfo createInfo{};
            createInfo.viewType = vk::ImageViewType::e2D;
            createInfo.format = vk::Format::eB8G8R8A8Srgb;
            createInfo.components.r = vk::ComponentSwizzle::eIdentity;
            createInfo.components.g = vk::ComponentSwizzle::eIdentity;
            createInfo.components.b = vk::ComponentSwizzle::eIdentity;
            createInfo.components.a = vk::ComponentSwizzle::eIdentity;
            createInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
            createInfo.subresourceRange.baseMipLevel = 0;
            createInfo.subresourceRange.levelCount = 1;
            createInfo.subresourceRange.baseArrayLayer = 0;
            createInfo.subresourceRange.layerCount = 1;
            for (int i = 0; i < FRAMES_IN_FLIGHT; ++i) {
                createInfo.image = m_swapChainImages[i];
                m_swapChainImageViews[i] = m_device.createImageView(createInfo);
            }
        };
        void createSwapchainFramebuffers(){
            vk::FramebufferCreateInfo framebufferInfo{};
            framebufferInfo.renderPass = m_renderPass;
            framebufferInfo.attachmentCount = 1;
            framebufferInfo.pAttachments = &m_swapChainImageViews[0];
            framebufferInfo.width = WINDOW_SIZE;
            framebufferInfo.height = WINDOW_SIZE;
            framebufferInfo.layers = 1;
            for (int i = 0; i < FRAMES_IN_FLIGHT; ++i) {
                m_swapChainFramebuffers[i] = m_device.createFramebuffer(framebufferInfo);
            }
        };
    public:
        NBodySimulator() {
            createWindow();
            createInstance();
            createPhysicalDevice();
            createDevice();
            createAllocator();
            createCommandPool();
            createCommandBuffer();
            createVertexBuffer();
            createIndexBuffer();
            createBodyPositionBuffers();
            createBodyVelocityBuffers();
            if (m_physicalDeviceType == vk::PhysicalDeviceType::eDiscreteGpu) {
                createStagingBuffer();
            }
            createSyncObjects();
            createComputeDescriptorSetLayout();
            createComputePipelineLayout();
            createComputePipeline();
            createRenderPass();
            createGraphicsPipelineLayout();
            createGraphicsPipeline();
            createSurface();
            createSwapchain();
            getSwapchainImages();
            createSwapchainImageViews();
            createSwapchainFramebuffers();
        }
        ~NBodySimulator() {
            m_device.waitIdle();
            for (int i = 0; i < FRAMES_IN_FLIGHT; ++i) {
                m_device.destroyImageView(m_swapChainImageViews[i]);
                m_device.destroyFramebuffer(m_swapChainFramebuffers[i]);
            }
            m_device.destroySwapchainKHR(m_swapChain);
            m_instance.destroySurfaceKHR(m_surface);
            m_device.destroyPipelineCache(m_graphicsPipelineCache);
            m_device.destroyPipeline(m_graphicsPipeline);
            m_device.destroyPipelineLayout(m_graphicsPipelineLayout);
            m_device.destroyRenderPass(m_renderPass);
            m_device.destroyPipeline(m_computePipeline);
            m_device.destroyPipelineLayout(m_computePipelineLayout);
            m_device.destroyDescriptorSetLayout(m_computeDescriptorSetLayout);
            if (m_physicalDeviceType == vk::PhysicalDeviceType::eDiscreteGpu) {
                vmaDestroyBuffer(m_allocator, m_stagingBuffer, m_stagingBufferAllocation);
            }
            for (int i = 0; i < NUMBER_OF_STATES; ++i) {
                m_device.destroySemaphore(m_stateSemaphores[i]);
                vmaDestroyBuffer(m_allocator, m_bodyPositionBuffers[i], m_bodyPositionBufferAllocations[i]);
                vmaDestroyBuffer(m_allocator, m_bodyVelocityBuffers[i], m_bodyVelocityBufferAllocations[i]);
            }
            for (int i = 0; i < FRAMES_IN_FLIGHT; ++i) {
                m_device.destroySemaphore(m_imageAvailableSemaphores[i]);
                m_device.destroyFence(m_inFlightFences[i]);
            }
            vmaDestroyBuffer(m_allocator, m_indexBuffer, m_indexBufferAllocation);
            vmaDestroyBuffer(m_allocator, m_vertexBuffer, m_vertexBufferAllocation);
            m_device.destroyCommandPool(m_commandPool);
            vmaDestroyAllocator(m_allocator);
            m_device.destroy();
            m_instance.destroy();
            glfwDestroyWindow(m_window);
            glfwTerminate();
        }
};

int main() {
    NBodySimulator nBodySimulator;
    return 0;
}
