#include "VulkanCore.h"
#include "util/Logger.h"
#include <cstring>
#include <set>
#include <algorithm>

VulkanCore& VulkanCore::instance() {
    static VulkanCore s;
    return s;
}

bool VulkanCore::initInstance(bool enableValidation) {
    LOG_I("VulkanCore: initInstance (validation=%d)...", enableValidation);
    if (volkInitialize() != VK_SUCCESS) {
        LOG_E("VulkanCore: volkInitialize failed");
        return false;
    }
    LOG_I("VulkanCore: creating instance...");
    if (!createInstance(enableValidation)) {
        LOG_E("VulkanCore: createInstance failed");
        return false;
    }
    volkLoadInstance(m_instance);
    LOG_I("VulkanCore: instance created");
    return true;
}

bool VulkanCore::initDevice(VkSurfaceKHR surface) {
    LOG_I("VulkanCore: initDevice...");
    m_surface = surface;

    LOG_I("VulkanCore: picking physical device...");
    if (!pickPhysicalDevice()) { LOG_E("VulkanCore: pickPhysicalDevice failed"); return false; }
    LOG_I("VulkanCore: creating logical device...");
    if (!createLogicalDevice()) { LOG_E("VulkanCore: createLogicalDevice failed"); return false; }
    volkLoadDevice(m_device);

    LOG_I("VulkanCore: creating allocator...");
    if (!createAllocator()) { LOG_E("VulkanCore: createAllocator failed"); return false; }

    LOG_I("VulkanCore: creating command pool...");
    VkCommandPoolCreateInfo poolCI = {};
    poolCI.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolCI.queueFamilyIndex = m_queueFamilies.graphics;
    poolCI.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    vkCreateCommandPool(m_device, &poolCI, nullptr, &m_graphicsCommandPool);

    LOG_I("VulkanCore: Initialized — %s, Vulkan %d.%d.%d",
          m_deviceProperties.deviceName,
          VK_API_VERSION_MAJOR(m_deviceProperties.apiVersion),
          VK_API_VERSION_MINOR(m_deviceProperties.apiVersion),
          VK_API_VERSION_PATCH(m_deviceProperties.apiVersion));
    return true;
}

bool VulkanCore::init(VkSurfaceKHR surface, bool enableValidation) {
    if (!initInstance(enableValidation)) return false;
    if (!initDevice(surface)) return false;
    return true;
}

void VulkanCore::shutdown() {
    if (m_allocator) { vmaDestroyAllocator(m_allocator); m_allocator = VK_NULL_HANDLE; }
    if (m_graphicsCommandPool && m_device) { vkDestroyCommandPool(m_device, m_graphicsCommandPool, nullptr); m_graphicsCommandPool = VK_NULL_HANDLE; }
    if (m_device)    { vkDestroyDevice(m_device, nullptr); m_device = VK_NULL_HANDLE; }
    destroyDebugMessenger();
    if (m_instance)  { vkDestroyInstance(m_instance, nullptr); m_instance = VK_NULL_HANDLE; }
}

bool VulkanCore::createInstance(bool enableValidation) {
    VkApplicationInfo appInfo = {};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "ADOCAV";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "ADOCAV";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_2;

    // Required extensions
    std::vector<const char*> extensions = { VK_KHR_SURFACE_EXTENSION_NAME };
#ifdef _WIN32
    extensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
#else
    extensions.push_back(VK_KHR_XLIB_SURFACE_EXTENSION_NAME);
#endif

    std::vector<const char*> layers;
    if (enableValidation) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        layers.push_back("VK_LAYER_KHRONOS_validation");
    }

    VkInstanceCreateInfo ci = {};
    ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo = &appInfo;
    ci.enabledExtensionCount = (uint32_t)extensions.size();
    ci.ppEnabledExtensionNames = extensions.data();
    ci.enabledLayerCount = (uint32_t)layers.size();
    ci.ppEnabledLayerNames = layers.data();

    if (vkCreateInstance(&ci, nullptr, &m_instance) != VK_SUCCESS) {
        LOG_E("VulkanCore: Failed to create VkInstance");
        return false;
    }

    if (enableValidation) setupDebugMessenger();
    return true;
}

VKAPI_ATTR VkBool32 VKAPI_CALL VulkanCore::debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT* data,
    void*) {
    const char* sev = "INFO";
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)   sev = "ERROR";
    else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) sev = "WARN";
    LOG_I("[Vulkan/%s] %s", sev, data->pMessage);
    return VK_FALSE;
}

void VulkanCore::setupDebugMessenger() {
    auto vkCreateDebugUtilsMessengerEXT = (PFN_vkCreateDebugUtilsMessengerEXT)
        vkGetInstanceProcAddr(m_instance, "vkCreateDebugUtilsMessengerEXT");
    if (!vkCreateDebugUtilsMessengerEXT) return;

    VkDebugUtilsMessengerCreateInfoEXT ci = {};
    ci.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    ci.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT
                       | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;
    ci.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
                   | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
                   | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    ci.pfnUserCallback = debugCallback;
    vkCreateDebugUtilsMessengerEXT(m_instance, &ci, nullptr, &m_debugMessenger);
}

void VulkanCore::destroyDebugMessenger() {
    if (!m_debugMessenger) return;
    auto vkDestroyDebugUtilsMessengerEXT = (PFN_vkDestroyDebugUtilsMessengerEXT)
        vkGetInstanceProcAddr(m_instance, "vkDestroyDebugUtilsMessengerEXT");
    if (vkDestroyDebugUtilsMessengerEXT)
        vkDestroyDebugUtilsMessengerEXT(m_instance, m_debugMessenger, nullptr);
}

bool VulkanCore::pickPhysicalDevice() {
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(m_instance, &count, nullptr);
    if (count == 0) { LOG_E("VulkanCore: No physical devices"); return false; }

    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(m_instance, &count, devices.data());

    // Pick the first discrete GPU, fall back to first device
    m_physicalDevice = devices[0];
    for (auto& d : devices) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(d, &props);
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            m_physicalDevice = d;
            break;
        }
    }

    vkGetPhysicalDeviceProperties(m_physicalDevice, &m_deviceProperties);

    // Find queue families
    uint32_t qfCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &qfCount, nullptr);
    std::vector<VkQueueFamilyProperties> qfProps(qfCount);
    vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &qfCount, qfProps.data());

    for (uint32_t i = 0; i < qfCount; i++) {
        if (qfProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
            m_queueFamilies.graphics = i;

        VkBool32 presentSupport = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(m_physicalDevice, i, m_surface, &presentSupport);
        if (presentSupport) m_queueFamilies.present = i;

        if (m_queueFamilies.complete()) break;
    }

    return m_queueFamilies.complete();
}

bool VulkanCore::createLogicalDevice() {
    std::set<uint32_t> uniqueFamilies = {
        m_queueFamilies.graphics, m_queueFamilies.present
    };

    std::vector<VkDeviceQueueCreateInfo> qcis;
    float priority = 1.0f;
    for (uint32_t family : uniqueFamilies) {
        VkDeviceQueueCreateInfo qci = {};
        qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qci.queueFamilyIndex = family;
        qci.queueCount = 1;
        qci.pQueuePriorities = &priority;
        qcis.push_back(qci);
    }

    // Required features (minimal — no Vulkan 1.2 features needed)
    VkPhysicalDeviceFeatures features = {};

    // Required extensions
    std::vector<const char*> extensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    };

    VkDeviceCreateInfo ci = {};
    ci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    ci.pNext = nullptr;
    ci.queueCreateInfoCount = (uint32_t)qcis.size();
    ci.pQueueCreateInfos = qcis.data();
    ci.enabledExtensionCount = (uint32_t)extensions.size();
    ci.ppEnabledExtensionNames = extensions.data();
    ci.pEnabledFeatures = &features;

    if (vkCreateDevice(m_physicalDevice, &ci, nullptr, &m_device) != VK_SUCCESS) {
        LOG_E("VulkanCore: Failed to create logical device");
        return false;
    }

    vkGetDeviceQueue(m_device, m_queueFamilies.graphics, 0, &m_graphicsQueue);
    vkGetDeviceQueue(m_device, m_queueFamilies.present, 0, &m_presentQueue);
    return true;
}

bool VulkanCore::createAllocator() {
    // Create VMA allocator with volk function table
    VmaVulkanFunctions vkFuncs = {};
    vkFuncs.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    vkFuncs.vkGetDeviceProcAddr = vkGetDeviceProcAddr;

    VmaAllocatorCreateInfo ci = {};
    ci.physicalDevice = m_physicalDevice;
    ci.device = m_device;
    ci.instance = m_instance;
    ci.vulkanApiVersion = VK_API_VERSION_1_2;
    ci.flags = 0;
    ci.pVulkanFunctions = &vkFuncs;

    LOG_I("VulkanCore: calling vmaCreateAllocator...");
    if (vmaCreateAllocator(&ci, &m_allocator) != VK_SUCCESS) {
        LOG_E("VulkanCore: Failed to create VMA allocator");
        return false;
    }
    LOG_I("VulkanCore: allocator created");
    return true;
}

uint32_t VulkanCore::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags props) const {
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memProps);
    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) &&
            (memProps.memoryTypes[i].propertyFlags & props) == props)
            return i;
    }
    return ~0u;
}

VkFormat VulkanCore::findSupportedFormat(const std::vector<VkFormat>& candidates,
                                          VkImageTiling tiling,
                                          VkFormatFeatureFlags features) const {
    for (VkFormat format : candidates) {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(m_physicalDevice, format, &props);
        if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features)
            return format;
        if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features)
            return format;
    }
    return VK_FORMAT_UNDEFINED;
}

VkFormat VulkanCore::findDepthFormat() const {
    return findSupportedFormat(
        {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT},
        VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
}
