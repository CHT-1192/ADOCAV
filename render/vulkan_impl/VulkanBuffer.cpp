#include "VulkanBuffer.h"
#include "VulkanCore.h"
#include "util/Logger.h"

VulkanBuffer::VulkanBuffer(VulkanBuffer&& o) noexcept
    : m_buffer(o.m_buffer), m_allocation(o.m_allocation), m_size(o.m_size),
      m_mapped(o.m_mapped), m_usage(o.m_usage), m_isPersistent(o.m_isPersistent),
      m_allocInfo(o.m_allocInfo) {
    o.m_buffer = VK_NULL_HANDLE;
    o.m_allocation = VK_NULL_HANDLE;
    o.m_mapped = nullptr;
}

VulkanBuffer& VulkanBuffer::operator=(VulkanBuffer&& o) noexcept {
    if (this != &o) {
        destroy();
        m_buffer = o.m_buffer; m_allocation = o.m_allocation; m_size = o.m_size;
        m_mapped = o.m_mapped; m_usage = o.m_usage; m_isPersistent = o.m_isPersistent;
        m_allocInfo = o.m_allocInfo;
        o.m_buffer = VK_NULL_HANDLE; o.m_allocation = VK_NULL_HANDLE; o.m_mapped = nullptr;
    }
    return *this;
}

void VulkanBuffer::destroy() {
    if (m_buffer) {
        vmaDestroyBuffer(VulkanCore::instance().allocator(), m_buffer, m_allocation);
        m_buffer = VK_NULL_HANDLE;
        m_allocation = VK_NULL_HANDLE;
        m_mapped = nullptr;
        m_size = 0;
    }
}

bool VulkanBuffer::createStaged(const void* data, VkDeviceSize size,
                                 VkBufferUsageFlags usage) {
    destroy();
    m_size = size;
    m_usage = usage;

    VkBufferCreateInfo stagingCI = {};
    stagingCI.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    stagingCI.size = size;
    stagingCI.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    VmaAllocationCreateInfo stagingAllocCI = {};
    stagingAllocCI.usage = VMA_MEMORY_USAGE_CPU_ONLY;

    VkBuffer stagingBuf;
    VmaAllocation stagingAlloc;
    if (vmaCreateBuffer(VulkanCore::instance().allocator(), &stagingCI, &stagingAllocCI,
                         &stagingBuf, &stagingAlloc, nullptr) != VK_SUCCESS) {
        LOG_E("VulkanBuffer: Failed to create staging buffer");
        return false;
    }

    // Copy data to staging
    void* mapped;
    vmaMapMemory(VulkanCore::instance().allocator(), stagingAlloc, &mapped);
    memcpy(mapped, data, (size_t)size);
    vmaUnmapMemory(VulkanCore::instance().allocator(), stagingAlloc);

    // Create device-local GPU buffer
    VkBufferCreateInfo gpuCI = {};
    gpuCI.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    gpuCI.size = size;
    gpuCI.usage = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    VmaAllocationCreateInfo gpuAllocCI = {};
    gpuAllocCI.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (vmaCreateBuffer(VulkanCore::instance().allocator(), &gpuCI, &gpuAllocCI,
                         &m_buffer, &m_allocation, &m_allocInfo) != VK_SUCCESS) {
        LOG_E("VulkanBuffer: Failed to create device-local buffer");
        vmaDestroyBuffer(VulkanCore::instance().allocator(), stagingBuf, stagingAlloc);
        return false;
    }

    // Immediate copy via command buffer (blocking — called during init only)
    VkCommandPool pool;
    VkCommandPoolCreateInfo poolCI = {};
    poolCI.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolCI.queueFamilyIndex = VulkanCore::instance().queueFamilies().graphics;
    vkCreateCommandPool(VulkanCore::instance().device(), &poolCI, nullptr, &pool);

    VkCommandBuffer cmd;
    VkCommandBufferAllocateInfo allocI = {};
    allocI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocI.commandPool = pool;
    allocI.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocI.commandBufferCount = 1;
    vkAllocateCommandBuffers(VulkanCore::instance().device(), &allocI, &cmd);

    VkCommandBufferBeginInfo beginI = {};
    beginI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginI.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginI);

    VkBufferCopy region = {0, 0, size};
    vkCmdCopyBuffer(cmd, stagingBuf, m_buffer, 1, &region);
    vkEndCommandBuffer(cmd);

    VkSubmitInfo submitI = {};
    submitI.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitI.commandBufferCount = 1;
    submitI.pCommandBuffers = &cmd;
    vkQueueSubmit(VulkanCore::instance().graphicsQueue(), 1, &submitI, VK_NULL_HANDLE);
    vkQueueWaitIdle(VulkanCore::instance().graphicsQueue());

    vkFreeCommandBuffers(VulkanCore::instance().device(), pool, 1, &cmd);
    vkDestroyCommandPool(VulkanCore::instance().device(), pool, nullptr);

    vmaDestroyBuffer(VulkanCore::instance().allocator(), stagingBuf, stagingAlloc);
    return true;
}

bool VulkanBuffer::createPersistentMapped(VkDeviceSize size, VkBufferUsageFlags usage) {
    destroy();
    m_size = size;
    m_usage = usage;
    m_isPersistent = true;

    VkBufferCreateInfo ci = {};
    ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    ci.size = size;
    ci.usage = usage;

    VmaAllocationCreateInfo allocCI = {};
    allocCI.usage = VMA_MEMORY_USAGE_AUTO;
    allocCI.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                    VMA_ALLOCATION_CREATE_MAPPED_BIT;

    if (vmaCreateBuffer(VulkanCore::instance().allocator(), &ci, &allocCI,
                         &m_buffer, &m_allocation, &m_allocInfo) != VK_SUCCESS) {
        LOG_E("VulkanBuffer: Failed to create persistent-mapped buffer");
        return false;
    }

    m_mapped = m_allocInfo.pMappedData;
    return true;
}

bool VulkanBuffer::createDeviceLocal(VkDeviceSize size, VkBufferUsageFlags usage) {
    destroy();
    m_size = size;
    m_usage = usage;

    VkBufferCreateInfo ci = {};
    ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    ci.size = size;
    ci.usage = usage;

    VmaAllocationCreateInfo allocCI = {};
    allocCI.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (vmaCreateBuffer(VulkanCore::instance().allocator(), &ci, &allocCI,
                         &m_buffer, &m_allocation, &m_allocInfo) != VK_SUCCESS) {
        LOG_E("VulkanBuffer: Failed to create device-local buffer");
        return false;
    }
    return true;
}

bool VulkanBuffer::createWithFlags(VkDeviceSize size, VkBufferUsageFlags usage,
                                    VmaAllocationCreateFlags vmaFlags,
                                    VkMemoryPropertyFlags requiredFlags,
                                    VkMemoryPropertyFlags preferredFlags) {
    destroy();
    m_size = size;
    m_usage = usage;

    VkBufferCreateInfo ci = {};
    ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    ci.size = size;
    ci.usage = usage;

    VmaAllocationCreateInfo allocCI = {};
    allocCI.usage = VMA_MEMORY_USAGE_AUTO;
    allocCI.flags = vmaFlags;
    allocCI.requiredFlags = requiredFlags;
    allocCI.preferredFlags = preferredFlags;

    if (vmaCreateBuffer(VulkanCore::instance().allocator(), &ci, &allocCI,
                         &m_buffer, &m_allocation, &m_allocInfo) != VK_SUCCESS) {
        LOG_E("VulkanBuffer: Failed to create buffer with flags");
        return false;
    }

    if (vmaFlags & VMA_ALLOCATION_CREATE_MAPPED_BIT) {
        m_mapped = m_allocInfo.pMappedData;
        m_isPersistent = true;
    }
    return true;
}

void VulkanBuffer::copyToMapped(const void* data, VkDeviceSize size, VkDeviceSize offset) {
    if (m_mapped && offset + size <= m_size) {
        memcpy((char*)m_mapped + offset, data, (size_t)size);
        // VMA handles flushing automatically with HOST_ACCESS_SEQUENTIAL_WRITE_BIT
        vmaFlushAllocation(VulkanCore::instance().allocator(), m_allocation, offset, size);
    }
}

void VulkanBuffer::uploadStaged(const void* data, VkDeviceSize size) {
    // Re-use the createStaged pattern for already-created device-local buffers
    VulkanBuffer staging;
    VkBufferUsageFlags stagingUsage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    if (staging.createPersistentMapped(size, stagingUsage)) {
        staging.copyToMapped(data, size);
        // TODO: record copy command in current frame's command buffer
        // For now, this is just a placeholder
    }
}
