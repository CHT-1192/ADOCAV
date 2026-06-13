#pragma once

#define VK_NO_PROTOTYPES
#include <volk.h>
#include <vector>

// Per-frame resources: command pool, command buffer, synchronization primitives.
// Triple-buffered with MAX_FRAMES_IN_FLIGHT = 2.

constexpr int MAX_FRAMES_IN_FLIGHT = 2;

class VulkanBuffer;

struct FrameResources {
    VkCommandPool   commandPool = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkSemaphore     imageAvailable = VK_NULL_HANDLE;
    VkSemaphore     renderFinished = VK_NULL_HANDLE;
    VkFence         inFlight = VK_NULL_HANDLE;

    // Per-frame persistent-mapped uniform buffers
    // - frustumData: 6 x vec4 (96 bytes) for compute culling
    // - cameraData: 2 x float (8 bytes) camera world position
    VulkanBuffer* frustumBuffer = nullptr;
    VulkanBuffer* cameraBuffer = nullptr;

    bool create();
    void destroy();
};

class FrameData {
public:
    FrameData() = default;
    ~FrameData() { destroy(); }

    bool create();
    void destroy();

    // Advance to next frame, waiting if it's still in flight
    void beginFrame(uint32_t& imageIndex, bool& recreated,
                    VkSwapchainKHR swapchain, VkSemaphore& outImageAvailable);

    // Submit current frame's work and present
    void submitAndPresent(VkQueue graphicsQueue, VkQueue presentQueue,
                          VkSwapchainKHR swapchain, uint32_t imageIndex);

    FrameResources& current() { return m_frames[m_currentFrame]; }
    uint32_t        currentIndex() const { return m_currentFrame; }

    // Pool-level reset: reset all command buffers at once
    VkCommandPool commandPool(uint32_t idx) const { return m_frames[idx].commandPool; }

private:
    FrameResources m_frames[MAX_FRAMES_IN_FLIGHT];
    uint32_t m_currentFrame = 0;
};
