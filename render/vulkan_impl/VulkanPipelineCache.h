#pragma once

#define VK_NO_PROTOTYPES
#include <volk.h>
#include <string>
#include <vector>

// Serialize/deserialize VkPipelineCache for instant pipeline creation on subsequent runs.

class VulkanPipelineCache {
public:
    VulkanPipelineCache() = default;
    ~VulkanPipelineCache() { destroy(); }

    // Load pipeline cache from disk, or create empty
    bool load(const std::string& filepath);
    // Save pipeline cache to disk
    bool save(const std::string& filepath);
    // Merge another cache into this one
    void merge(VkPipelineCache other);

    VkPipelineCache cache() const { return m_cache; }
    bool isValid() const { return m_cache != VK_NULL_HANDLE; }

    void destroy();

private:
    VkPipelineCache m_cache = VK_NULL_HANDLE;
};
