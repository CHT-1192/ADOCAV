#pragma once

#define VK_NO_PROTOTYPES
#include <volk.h>
#include <vector>

// Manages the swapchain: images, image views, depth buffer, and recreation.

class VulkanSwapchain {
public:
    VulkanSwapchain() = default;
    ~VulkanSwapchain() { destroy(); }

    struct Support {
        VkSurfaceCapabilitiesKHR caps;
        std::vector<VkSurfaceFormatKHR> formats;
        std::vector<VkPresentModeKHR> presentModes;
    };

    static Support querySupport(VkPhysicalDevice device, VkSurfaceKHR surface);

    bool create(uint32_t width, uint32_t height, bool vsync = false);
    void destroy();
    bool recreate(uint32_t width, uint32_t height);

    VkSwapchainKHR    swapchain()     const { return m_swapchain; }
    VkFormat          imageFormat()   const { return m_imageFormat; }
    VkExtent2D        extent()        const { return m_extent; }
    VkImage           depthImage()    const { return m_depthImage; }
    VkImageView       depthView()     const { return m_depthView; }
    VkFormat          depthFormat()   const { return m_depthFormat; }

    uint32_t          imageCount()    const { return (uint32_t)m_images.size(); }
    VkImage           image(uint32_t i)  const { return m_images[i]; }
    VkImageView       imageView(uint32_t i) const { return m_imageViews[i]; }
    VkFramebuffer     framebuffer(uint32_t i) const { return m_framebuffers[i]; }

    // Acquire next swapchain image, returns image index and whether recreation is needed
    bool acquireNextImage(VkSemaphore signalSemaphore, uint32_t& imageIndex, bool& recreated);

    // Present the current image
    bool present(VkSemaphore waitSemaphore, uint32_t imageIndex);

private:
    VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
    VkFormat       m_imageFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D     m_extent = {};

    // Depth
    VkImage        m_depthImage = VK_NULL_HANDLE;
    VkDeviceMemory m_depthMemory = VK_NULL_HANDLE;
    VkImageView    m_depthView = VK_NULL_HANDLE;
    VkFormat       m_depthFormat = VK_FORMAT_UNDEFINED;

    // Swapchain images
    std::vector<VkImage>     m_images;
    std::vector<VkImageView> m_imageViews;
    std::vector<VkFramebuffer> m_framebuffers;

    VkRenderPass m_renderPass = VK_NULL_HANDLE;  // owned by VulkanRenderPass, just referenced

    VkSurfaceFormatKHR chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& available);
    VkPresentModeKHR   choosePresentMode(const std::vector<VkPresentModeKHR>& available, bool vsync);
    VkExtent2D         chooseExtent(const VkSurfaceCapabilitiesKHR& caps, uint32_t w, uint32_t h);

    void createDepthResources();
    void createFramebuffers(VkRenderPass renderPass);
    void destroyDepthResources();
    void destroyFramebuffers();
};
