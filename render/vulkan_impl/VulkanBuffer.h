#pragma once

#define VK_NO_PROTOTYPES
#include <volk.h>
#include <vk_mem_alloc.h>
#include <cstddef>
#include <cstring>

// RAII wrapper around VkBuffer + VmaAllocation.
// Supports staged upload (CPU→staging→GPU) and persistent mapping (CPU→GPU directly).

class VulkanBuffer {
public:
    VulkanBuffer() = default;
    ~VulkanBuffer() { destroy(); }

    VulkanBuffer(const VulkanBuffer&) = delete;
    VulkanBuffer& operator=(const VulkanBuffer&) = delete;
    VulkanBuffer(VulkanBuffer&& o) noexcept;
    VulkanBuffer& operator=(VulkanBuffer&& o) noexcept;

    // Create with staging copy: host-visible staging → device-local GPU buffer
    bool createStaged(const void* data, VkDeviceSize size,
                      VkBufferUsageFlags usage);

    // Create host-visible, persistently-mapped buffer (CPU writes directly)
    bool createPersistentMapped(VkDeviceSize size, VkBufferUsageFlags usage);

    // Create device-local buffer (no mapping — GPU reads only)
    bool createDeviceLocal(VkDeviceSize size, VkBufferUsageFlags usage);

    // Create with VMA allocation flags for advanced usage (REQUIRED for HOST_ACCESS_SEQUENTIAL_WRITE)
    bool createWithFlags(VkDeviceSize size, VkBufferUsageFlags usage,
                         VmaAllocationCreateFlags vmaFlags,
                         VkMemoryPropertyFlags requiredFlags = 0,
                         VkMemoryPropertyFlags preferredFlags = 0);

    void destroy();

    VkBuffer       buffer()     const { return m_buffer; }
    VkDeviceSize   size()       const { return m_size; }
    void*          mappedData() const { return m_mapped; }

    // Copy data to mapped buffer (for persistent-mapped or staged host buffer)
    void copyToMapped(const void* data, VkDeviceSize size, VkDeviceSize offset = 0);

    // Upload data via staging buffer (for device-local buffers)
    void uploadStaged(const void* data, VkDeviceSize size);

    bool isValid() const { return m_buffer != VK_NULL_HANDLE; }

private:
    VkBuffer      m_buffer = VK_NULL_HANDLE;
    VmaAllocation m_allocation = VK_NULL_HANDLE;
    VkDeviceSize  m_size = 0;
    void*         m_mapped = nullptr;
    VkBufferUsageFlags m_usage = 0;
    bool          m_isPersistent = false;

    friend class VulkanBuffer;
    VmaAllocationInfo m_allocInfo = {};
};
