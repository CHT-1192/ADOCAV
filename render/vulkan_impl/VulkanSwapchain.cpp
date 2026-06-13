#include "VulkanSwapchain.h"
#include "VulkanCore.h"
#include "util/Logger.h"
#include <algorithm>

VulkanSwapchain::Support VulkanSwapchain::querySupport(VkPhysicalDevice device, VkSurfaceKHR surface) {
    Support s;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &s.caps);

    uint32_t count;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &count, nullptr);
    if (count > 0) {
        s.formats.resize(count);
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &count, s.formats.data());
    }

    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &count, nullptr);
    if (count > 0) {
        s.presentModes.resize(count);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &count, s.presentModes.data());
    }
    return s;
}

VkSurfaceFormatKHR VulkanSwapchain::chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& available) {
    for (auto& f : available) {
        if (f.format == VK_FORMAT_B8G8R8A8_SRGB && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            return f;
    }
    return available[0];
}

VkPresentModeKHR VulkanSwapchain::choosePresentMode(const std::vector<VkPresentModeKHR>& available, bool vsync) {
    // Prefer MAILBOX (triple-buffered, low latency)
    for (auto& m : available)
        if (m == VK_PRESENT_MODE_MAILBOX_KHR) return m;
    // Fallback: IMMEDIATE (no vsync, for benchmarking) or FIFO (traditional vsync)
    if (vsync) {
        for (auto& m : available)
            if (m == VK_PRESENT_MODE_FIFO_KHR) return m;
    } else {
        for (auto& m : available)
            if (m == VK_PRESENT_MODE_IMMEDIATE_KHR) return m;
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D VulkanSwapchain::chooseExtent(const VkSurfaceCapabilitiesKHR& caps, uint32_t w, uint32_t h) {
    if (caps.currentExtent.width != UINT32_MAX)
        return caps.currentExtent;
    VkExtent2D ext = {w, h};
    ext.width  = std::max(caps.minImageExtent.width,  std::min(caps.maxImageExtent.width,  ext.width));
    ext.height = std::max(caps.minImageExtent.height, std::min(caps.maxImageExtent.height, ext.height));
    return ext;
}

bool VulkanSwapchain::create(uint32_t width, uint32_t height, bool vsync) {
    auto& core = VulkanCore::instance();
    Support support = querySupport(core.physicalDevice(), core.surface());

    VkSurfaceFormatKHR format = chooseSurfaceFormat(support.formats);
    VkPresentModeKHR mode = choosePresentMode(support.presentModes, vsync);
    m_extent = chooseExtent(support.caps, width, height);
    m_imageFormat = format.format;

    uint32_t imageCount = support.caps.minImageCount + 1;
    if (support.caps.maxImageCount > 0 && imageCount > support.caps.maxImageCount)
        imageCount = support.caps.maxImageCount;

    VkSwapchainCreateInfoKHR ci = {};
    ci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    ci.surface = core.surface();
    ci.minImageCount = imageCount;
    ci.imageFormat = format.format;
    ci.imageColorSpace = format.colorSpace;
    ci.imageExtent = m_extent;
    ci.imageArrayLayers = 1;
    ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    ci.preTransform = support.caps.currentTransform;
    ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ci.presentMode = mode;
    ci.clipped = VK_TRUE;
    ci.oldSwapchain = VK_NULL_HANDLE;

    uint32_t qf[] = {core.queueFamilies().graphics, core.queueFamilies().present};
    if (qf[0] != qf[1]) {
        ci.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        ci.queueFamilyIndexCount = 2;
        ci.pQueueFamilyIndices = qf;
    } else {
        ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    if (vkCreateSwapchainKHR(core.device(), &ci, nullptr, &m_swapchain) != VK_SUCCESS) {
        LOG_E("VulkanSwapchain: Failed to create swapchain");
        return false;
    }

    // Get swapchain images
    uint32_t count;
    vkGetSwapchainImagesKHR(core.device(), m_swapchain, &count, nullptr);
    m_images.resize(count);
    m_imageViews.resize(count);
    vkGetSwapchainImagesKHR(core.device(), m_swapchain, &count, m_images.data());

    // Create image views
    for (uint32_t i = 0; i < count; i++) {
        VkImageViewCreateInfo viewCI = {};
        viewCI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewCI.image = m_images[i];
        viewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewCI.format = m_imageFormat;
        viewCI.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewCI.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewCI.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewCI.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewCI.subresourceRange.baseMipLevel = 0;
        viewCI.subresourceRange.levelCount = 1;
        viewCI.subresourceRange.baseArrayLayer = 0;
        viewCI.subresourceRange.layerCount = 1;
        if (vkCreateImageView(core.device(), &viewCI, nullptr, &m_imageViews[i]) != VK_SUCCESS) {
            LOG_E("VulkanSwapchain: Failed to create image view %d", i);
            return false;
        }
    }

    createDepthResources();

    LOG_I("VulkanSwapchain: %dx%d, %d images, format=%d, mode=%d",
          m_extent.width, m_extent.height, count, m_imageFormat, mode);
    return true;
}

void VulkanSwapchain::destroy() {
    auto device = VulkanCore::instance().device();
    destroyFramebuffers();
    destroyDepthResources();
    for (auto& v : m_imageViews) vkDestroyImageView(device, v, nullptr);
    m_imageViews.clear();
    m_images.clear();
    if (m_swapchain) { vkDestroySwapchainKHR(device, m_swapchain, nullptr); m_swapchain = VK_NULL_HANDLE; }
}

bool VulkanSwapchain::recreate(uint32_t width, uint32_t height) {
    auto& core = VulkanCore::instance();
    vkDeviceWaitIdle(core.device());
    destroy();
    return create(width, height);
}

void VulkanSwapchain::createDepthResources() {
    auto& core = VulkanCore::instance();
    m_depthFormat = core.findDepthFormat();
    if (m_depthFormat == VK_FORMAT_UNDEFINED) return;

    VkImageCreateInfo imgCI = {};
    imgCI.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imgCI.imageType = VK_IMAGE_TYPE_2D;
    imgCI.format = m_depthFormat;
    imgCI.extent = {m_extent.width, m_extent.height, 1};
    imgCI.mipLevels = 1;
    imgCI.arrayLayers = 1;
    imgCI.samples = VK_SAMPLE_COUNT_1_BIT;
    imgCI.tiling = VK_IMAGE_TILING_OPTIMAL;
    imgCI.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

    VkMemoryRequirements memReq;
    vkCreateImage(core.device(), &imgCI, nullptr, &m_depthImage);
    vkGetImageMemoryRequirements(core.device(), m_depthImage, &memReq);

    VkMemoryAllocateInfo allocI = {};
    allocI.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocI.allocationSize = memReq.size;
    allocI.memoryTypeIndex = core.findMemoryType(memReq.memoryTypeBits,
                                                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkAllocateMemory(core.device(), &allocI, nullptr, &m_depthMemory);
    vkBindImageMemory(core.device(), m_depthImage, m_depthMemory, 0);

    VkImageViewCreateInfo viewCI = {};
    viewCI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewCI.image = m_depthImage;
    viewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewCI.format = m_depthFormat;
    viewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    viewCI.subresourceRange.baseMipLevel = 0;
    viewCI.subresourceRange.levelCount = 1;
    viewCI.subresourceRange.baseArrayLayer = 0;
    viewCI.subresourceRange.layerCount = 1;
    vkCreateImageView(core.device(), &viewCI, nullptr, &m_depthView);
}

void VulkanSwapchain::destroyDepthResources() {
    auto device = VulkanCore::instance().device();
    if (m_depthView)   { vkDestroyImageView(device, m_depthView, nullptr); m_depthView = VK_NULL_HANDLE; }
    if (m_depthImage)  { vkDestroyImage(device, m_depthImage, nullptr); m_depthImage = VK_NULL_HANDLE; }
    if (m_depthMemory) { vkFreeMemory(device, m_depthMemory, nullptr); m_depthMemory = VK_NULL_HANDLE; }
}

void VulkanSwapchain::destroyFramebuffers() {
    auto device = VulkanCore::instance().device();
    for (auto& fb : m_framebuffers) vkDestroyFramebuffer(device, fb, nullptr);
    m_framebuffers.clear();
}

bool VulkanSwapchain::acquireNextImage(VkSemaphore signalSemaphore, uint32_t& imageIndex, bool& recreated) {
    VkResult result = vkAcquireNextImageKHR(VulkanCore::instance().device(), m_swapchain,
                                             UINT64_MAX, signalSemaphore, VK_NULL_HANDLE, &imageIndex);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        recreated = true;
        return false;
    }
    if (result != VK_SUCCESS) {
        LOG_E("VulkanSwapchain: Failed to acquire next image: %d", result);
        return false;
    }
    recreated = false;
    return true;
}

bool VulkanSwapchain::present(VkSemaphore waitSemaphore, uint32_t imageIndex) {
    VkPresentInfoKHR pi = {};
    pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &waitSemaphore;
    pi.swapchainCount = 1;
    pi.pSwapchains = &m_swapchain;
    pi.pImageIndices = &imageIndex;

    VkResult result = vkQueuePresentKHR(VulkanCore::instance().presentQueue(), &pi);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
        return false;  // needs recreate
    return result == VK_SUCCESS;
}
