#include <iostream>
#define VMA_IMPLEMENTATION
#define VMA_VULKAN_VERSION 1002000
#include <vk_mem_alloc.h>
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>
#include <vector>
#include "comp.h"

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

#define NUMBER_OF_STATES 2
#define STATE_READY_FOR_COMPUTE 0
#define STATE_READY_FOR_RENDER 1
#define STATE_READY_FOR_SOURCE_OF_NEXT_COMPUTE 2

class NBodySimulator {
    private:
        vk::Instance m_instance;
        vk::PhysicalDevice m_physicalDevice;
        vk::PhysicalDeviceType m_physicalDeviceType;
        vk::Device m_device;
        vk::Queue m_queue;
        VmaAllocator m_allocator;
        vk::CommandPool m_commandPool;
        vk::CommandBuffer m_commandBuffer;
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
        void createInstance() {
            VULKAN_HPP_DEFAULT_DISPATCHER.init();
            vk::ApplicationInfo appInfo{};
            appInfo.pApplicationName = "NBodySimulator";
            appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
            appInfo.pEngineName = "NBodySimulator";
            appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
            appInfo.apiVersion = VK_API_VERSION_1_2;
            std::vector<const char*> instanceExtensions = {
                VK_KHR_SURFACE_EXTENSION_NAME,
                VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME,
            };
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
            allocateInfo.commandBufferCount = 1;
            m_commandBuffer = m_device.allocateCommandBuffers(allocateInfo)[0];
        }
        void createVertexBuffer() {
            vk::BufferCreateInfo bufferInfo{};
            bufferInfo.size = sizeof(float) * 2 * 11;
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
            bufferInfo.size = sizeof(uint32_t) * 3 * 10;
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
            bufferInfo.size = sizeof(float) * 2 * 1000;
            bufferInfo.usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eVertexBuffer;
            VmaAllocationCreateInfo allocInfo{};
            allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
            if(m_physicalDeviceType == vk::PhysicalDeviceType::eDiscreteGpu) {
                bufferInfo.usage |= vk::BufferUsageFlagBits::eTransferDst;
            }else{
                allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
            }
            VmaAllocationInfo info{};
            for (int i = 0; i < 2; ++i) {
                vmaCreateBuffer(m_allocator, reinterpret_cast<VkBufferCreateInfo*>(&bufferInfo),
                                reinterpret_cast<VmaAllocationCreateInfo*>(&allocInfo),
                                reinterpret_cast<VkBuffer*>(&m_bodyPositionBuffers[i]),
                                &m_bodyPositionBufferAllocations[i], &info);
                m_mappedBodyPositionBuffers[i] = info.pMappedData;
            }
        };
        void createBodyVelocityBuffers(){
            vk::BufferCreateInfo bufferInfo{};
            bufferInfo.size = sizeof(float) * 2 * 1000;
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
            for (int i = 0; i < NUMBER_OF_STATES; ++i) {
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
        void createComputDescriptorSetLayout(){
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
    public:
        NBodySimulator() {
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
            createComputDescriptorSetLayout();
            createComputePipelineLayout();
            createComputePipeline();
        }
        ~NBodySimulator() {
            m_device.waitIdle();
            m_device.destroyPipeline(m_computePipeline);
            m_device.destroyPipelineLayout(m_computePipelineLayout);
            m_device.destroyDescriptorSetLayout(m_computeDescriptorSetLayout);
            if (m_physicalDeviceType == vk::PhysicalDeviceType::eDiscreteGpu) {
                vmaDestroyBuffer(m_allocator, m_stagingBuffer, m_stagingBufferAllocation);
            }
            for (int i = 0; i < NUMBER_OF_STATES; ++i) {
                m_device.destroySemaphore(m_stateSemaphores[i]);
                m_device.destroySemaphore(m_imageAvailableSemaphores[i]);
                m_device.destroyFence(m_inFlightFences[i]);
                vmaDestroyBuffer(m_allocator, m_bodyPositionBuffers[i], m_bodyPositionBufferAllocations[i]);
                vmaDestroyBuffer(m_allocator, m_bodyVelocityBuffers[i], m_bodyVelocityBufferAllocations[i]);
            }
            vmaDestroyBuffer(m_allocator, m_indexBuffer, m_indexBufferAllocation);
            vmaDestroyBuffer(m_allocator, m_vertexBuffer, m_vertexBufferAllocation);
            m_device.destroyCommandPool(m_commandPool);
            vmaDestroyAllocator(m_allocator);
            m_device.destroy();
            m_instance.destroy();
        }
};

int main() {
    NBodySimulator nBodySimulator;
    return 0;
}
