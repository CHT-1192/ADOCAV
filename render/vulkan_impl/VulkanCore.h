#pragma once

#define VK_NO_PROTOTYPES
#include <volk.h>
#include <vk_mem_alloc.h>
#include <vector>
#include <string>
#include <functional>

// Singleton: owns VkInstance, VkPhysicalDevice, VkDevice, VmaAllocator.
// Must be initialized once before any other Vulkan object.

class VulkanCore {
public:
    static VulkanCore& instance();

    VulkanCore(const VulkanCore&) = delete;
    VulkanCore& operator=(const VulkanCore&) = delete;

    struct QueueFamilies {
        uint32_t graphics = ~0u;
        uint32_t present  = ~0u;
        bool complete() const { return graphics != ~0u && present != ~0u; }
    };

    // Two-phase init: instance first (so surface can be created), then device
    bool initInstance(bool enableValidation = true);
    bool initDevice(VkSurfaceKHR surface);
    bool init(VkSurfaceKHR surface, bool enableValidation = true);  // convenience: does both
    void shutdown();

    VkInstance        vkInstance()     const { return m_instance; }
    VkPhysicalDevice   physicalDevice() const { return m_physicalDevice; }
    VkDevice           device()         const { return m_device; }
    VmaAllocator       allocator()      const { return m_allocator; }
    VkQueue            graphicsQueue()  const { return m_graphicsQueue; }
    VkQueue            presentQueue()   const { return m_presentQueue; }
    QueueFamilies      queueFamilies()  const { return m_queueFamilies; }
    VkSurfaceKHR       surface()        const { return m_surface; }
    VkCommandPool      graphicsCommandPool() const { return m_graphicsCommandPool; }

    // GPU properties
    VkPhysicalDeviceProperties properties() const { return m_deviceProperties; }
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags props) const;

    // Query supported formats
    VkFormat findSupportedFormat(const std::vector<VkFormat>& candidates,
                                  VkImageTiling tiling,
                                  VkFormatFeatureFlags features) const;

    // Find suitable depth format
    VkFormat findDepthFormat() const;

private:
    VulkanCore() = default;
    ~VulkanCore() = default;

    VkInstance          m_instance = VK_NULL_HANDLE;
    VkPhysicalDevice    m_physicalDevice = VK_NULL_HANDLE;
    VkDevice            m_device = VK_NULL_HANDLE;
    VmaAllocator        m_allocator = VK_NULL_HANDLE;
    VkSurfaceKHR        m_surface = VK_NULL_HANDLE;
    VkCommandPool       m_graphicsCommandPool = VK_NULL_HANDLE;
    VkQueue             m_graphicsQueue = VK_NULL_HANDLE;
    VkQueue             m_presentQueue = VK_NULL_HANDLE;
    QueueFamilies       m_queueFamilies;
    VkPhysicalDeviceProperties m_deviceProperties = {};
    VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;

    bool createInstance(bool enableValidation);
    bool pickPhysicalDevice();
    bool createLogicalDevice();
    bool createAllocator();
    void setupDebugMessenger();
    void destroyDebugMessenger();

    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT severity,
        VkDebugUtilsMessageTypeFlagsEXT type,
        const VkDebugUtilsMessengerCallbackDataEXT* data,
        void* userData);
};
