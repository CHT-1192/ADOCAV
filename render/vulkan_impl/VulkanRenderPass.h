#pragma once

#define VK_NO_PROTOTYPES
#include <volk.h>
#include <vector>

// Creates and manages the single merged Vulkan render pass and framebuffers.

class VulkanRenderPass {
public:
    VulkanRenderPass() = default;
    ~VulkanRenderPass() { destroy(); }

    bool create(VkFormat colorFormat, VkFormat depthFormat);
    void destroy();
    void recreateFramebuffers(VkExtent2D extent,
                               const std::vector<VkImageView>& imageViews,
                               VkImageView depthView);

    VkRenderPass renderPass() const { return m_renderPass; }
    const std::vector<VkFramebuffer>& framebuffers() const { return m_framebuffers; }

private:
    VkRenderPass m_renderPass = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> m_framebuffers;
};
