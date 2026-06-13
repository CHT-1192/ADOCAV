#include "VulkanRenderPass.h"
#include "VulkanCore.h"
#include "util/Logger.h"

bool VulkanRenderPass::create(VkFormat colorFormat, VkFormat depthFormat) {
    auto device = VulkanCore::instance().device();

    // Color attachment
    VkAttachmentDescription colorAttach = {};
    colorAttach.format = colorFormat;
    colorAttach.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttach.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttach.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttach.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttach.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttach.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttach.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorRef = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

    // Depth attachment
    VkAttachmentDescription depthAttach = {};
    depthAttach.format = depthFormat;
    depthAttach.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttach.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttach.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttach.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttach.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttach.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttach.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthRef = {1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

    bool hasDepth = (depthFormat != VK_FORMAT_UNDEFINED);

    // Single subpass
    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;
    subpass.pDepthStencilAttachment = hasDepth ? &depthRef : nullptr;

    // Subpass dependency (external → subpass 0)
    VkSubpassDependency dep = {};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.srcAccessMask = 0;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    std::vector<VkAttachmentDescription> attachments = {colorAttach};
    if (hasDepth) attachments.push_back(depthAttach);

    VkRenderPassCreateInfo ci = {};
    ci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    ci.attachmentCount = (uint32_t)attachments.size();
    ci.pAttachments = attachments.data();
    ci.subpassCount = 1;
    ci.pSubpasses = &subpass;
    ci.dependencyCount = 1;
    ci.pDependencies = &dep;

    if (vkCreateRenderPass(device, &ci, nullptr, &m_renderPass) != VK_SUCCESS) {
        LOG_E("VulkanRenderPass: Failed to create render pass");
        return false;
    }
    return true;
}

void VulkanRenderPass::destroy() {
    auto device = VulkanCore::instance().device();
    for (auto& fb : m_framebuffers) vkDestroyFramebuffer(device, fb, nullptr);
    m_framebuffers.clear();
    if (m_renderPass) { vkDestroyRenderPass(device, m_renderPass, nullptr); m_renderPass = VK_NULL_HANDLE; }
}

void VulkanRenderPass::recreateFramebuffers(VkExtent2D extent,
                                              const std::vector<VkImageView>& imageViews,
                                              VkImageView depthView) {
    auto device = VulkanCore::instance().device();

    // Destroy old
    for (auto& fb : m_framebuffers) vkDestroyFramebuffer(device, fb, nullptr);
    m_framebuffers.clear();

    m_framebuffers.resize(imageViews.size());
    for (size_t i = 0; i < imageViews.size(); i++) {
        std::vector<VkImageView> attachments = {imageViews[i]};
        if (depthView != VK_NULL_HANDLE) attachments.push_back(depthView);

        VkFramebufferCreateInfo fbCI = {};
        fbCI.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbCI.renderPass = m_renderPass;
        fbCI.attachmentCount = (uint32_t)attachments.size();
        fbCI.pAttachments = attachments.data();
        fbCI.width = extent.width;
        fbCI.height = extent.height;
        fbCI.layers = 1;

        if (vkCreateFramebuffer(device, &fbCI, nullptr, &m_framebuffers[i]) != VK_SUCCESS) {
            LOG_E("VulkanRenderPass: Failed to create framebuffer %zu", i);
        }
    }
}
