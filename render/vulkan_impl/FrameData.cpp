#include "FrameData.h"
#include "VulkanCore.h"
#include "VulkanBuffer.h"
#include "util/Logger.h"

bool FrameResources::create() {
    auto device = VulkanCore::instance().device();

    // Command pool
    VkCommandPoolCreateInfo poolCI = {};
    poolCI.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolCI.queueFamilyIndex = VulkanCore::instance().queueFamilies().graphics;
    poolCI.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    if (vkCreateCommandPool(device, &poolCI, nullptr, &commandPool) != VK_SUCCESS) {
        LOG_E("FrameResources: Failed to create command pool");
        return false;
    }

    // Command buffer
    VkCommandBufferAllocateInfo allocI = {};
    allocI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocI.commandPool = commandPool;
    allocI.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocI.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(device, &allocI, &commandBuffer) != VK_SUCCESS) {
        LOG_E("FrameResources: Failed to allocate command buffer");
        return false;
    }

    // Semaphores
    VkSemaphoreCreateInfo semCI = {};
    semCI.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    vkCreateSemaphore(device, &semCI, nullptr, &imageAvailable);
    vkCreateSemaphore(device, &semCI, nullptr, &renderFinished);

    // Fence (created signaled so first frame doesn't block)
    VkFenceCreateInfo fenceCI = {};
    fenceCI.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceCI.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    vkCreateFence(device, &fenceCI, nullptr, &inFlight);

    // Persistent-mapped uniform buffers
    frustumBuffer = new VulkanBuffer();
    frustumBuffer->createPersistentMapped(256, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    cameraBuffer = new VulkanBuffer();
    cameraBuffer->createPersistentMapped(256, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

    return true;
}

void FrameResources::destroy() {
    auto device = VulkanCore::instance().device();
    delete frustumBuffer; frustumBuffer = nullptr;
    delete cameraBuffer; cameraBuffer = nullptr;
    if (inFlight)      { vkDestroyFence(device, inFlight, nullptr); inFlight = VK_NULL_HANDLE; }
    if (renderFinished) { vkDestroySemaphore(device, renderFinished, nullptr); renderFinished = VK_NULL_HANDLE; }
    if (imageAvailable) { vkDestroySemaphore(device, imageAvailable, nullptr); imageAvailable = VK_NULL_HANDLE; }
    if (commandBuffer) { vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer); commandBuffer = VK_NULL_HANDLE; }
    if (commandPool)   { vkDestroyCommandPool(device, commandPool, nullptr); commandPool = VK_NULL_HANDLE; }
}

// ---- FrameData ----

bool FrameData::create() {
    for (auto& f : m_frames) {
        if (!f.create()) return false;
    }
    m_currentFrame = 0;
    return true;
}

void FrameData::destroy() {
    vkDeviceWaitIdle(VulkanCore::instance().device());
    for (auto& f : m_frames) f.destroy();
}

void FrameData::beginFrame(uint32_t& imageIndex, bool& recreated,
                            VkSwapchainKHR swapchain, VkSemaphore& outImageAvailable) {
    auto device = VulkanCore::instance().device();
    auto& frame = m_frames[m_currentFrame];

    // Wait for the current frame's fence (GPU done with this frame's resources)
    vkWaitForFences(device, 1, &frame.inFlight, VK_TRUE, UINT64_MAX);
    vkResetFences(device, 1, &frame.inFlight);

    // Acquire next swapchain image
    VkResult result = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX,
                                             frame.imageAvailable, VK_NULL_HANDLE, &imageIndex);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        recreated = true;
        return;
    }
    recreated = false;
    outImageAvailable = frame.imageAvailable;

    // Reset command pool for this frame
    vkResetCommandPool(device, frame.commandPool, 0);
}

void FrameData::submitAndPresent(VkQueue graphicsQueue, VkQueue presentQueue,
                                  VkSwapchainKHR swapchain, uint32_t imageIndex) {
    auto& frame = m_frames[m_currentFrame];

    // Submit
    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submitI = {};
    submitI.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitI.waitSemaphoreCount = 1;
    submitI.pWaitSemaphores = &frame.imageAvailable;
    submitI.pWaitDstStageMask = &waitStage;
    submitI.commandBufferCount = 1;
    submitI.pCommandBuffers = &frame.commandBuffer;
    submitI.signalSemaphoreCount = 1;
    submitI.pSignalSemaphores = &frame.renderFinished;

    vkQueueSubmit(graphicsQueue, 1, &submitI, frame.inFlight);

    // Present
    VkPresentInfoKHR pi = {};
    pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &frame.renderFinished;
    pi.swapchainCount = 1;
    pi.pSwapchains = &swapchain;
    pi.pImageIndices = &imageIndex;
    vkQueuePresentKHR(presentQueue, &pi);

    m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}
