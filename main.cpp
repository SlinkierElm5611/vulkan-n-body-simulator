#include <iostream>
#define VMA_IMPLEMENTATION
#define VMA_VULKAN_VERSION 1002000
#include <vk_mem_alloc.h>
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>
#include <vector>

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

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
        vk::Buffer m_bodyPositionBuffers[2];
        VmaAllocation m_bodyPositionBufferAllocations[2];
        void* m_mappedBodyPositionBuffers[2];
        vk::Buffer m_bodyVelocityBuffers[2];
        VmaAllocation m_bodyVelocityBufferAllocations[2];
        void* m_mappedBodyVelocityBuffers[2];
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
            std::vector<const char*> deviceExtensions = {
                VK_KHR_SWAPCHAIN_EXTENSION_NAME,
                VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME,
                "VK_KHR_portability_subset",
            };
            std::vector<vk::DeviceQueueCreateInfo> queueCreateInfos;
            std::vector<float> queuePriorities = {1.0f};
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
            bufferInfo.size = sizeof(float) * 2 * 301;
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
            bufferInfo.size = sizeof(uint32_t) * 3 * 300;
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
            for (int i = 0; i < 2; ++i) {
                vmaCreateBuffer(m_allocator, reinterpret_cast<VkBufferCreateInfo*>(&bufferInfo),
                                reinterpret_cast<VmaAllocationCreateInfo*>(&allocInfo),
                                reinterpret_cast<VkBuffer*>(&m_bodyVelocityBuffers[i]),
                                &m_bodyVelocityBufferAllocations[i], &info);
                m_mappedBodyVelocityBuffers[i] = info.pMappedData;
            }
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
        }
        ~NBodySimulator() {
            for (int i = 0; i < 2; ++i) {
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
