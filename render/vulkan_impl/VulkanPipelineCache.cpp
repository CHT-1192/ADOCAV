#include "VulkanPipelineCache.h"
#include "VulkanCore.h"
#include "util/Logger.h"
#include <fstream>
#include <vector>

bool VulkanPipelineCache::load(const std::string& filepath) {
    destroy();

    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        // No cache on disk — create empty
        VkPipelineCacheCreateInfo ci = {};
        ci.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
        if (vkCreatePipelineCache(VulkanCore::instance().device(), &ci, nullptr, &m_cache) != VK_SUCCESS) {
            LOG_E("PipelineCache: Failed to create empty cache");
            return false;
        }
        LOG_I("PipelineCache: Created new empty cache");
        return true;
    }

    size_t size = (size_t)file.tellg();
    std::vector<char> data(size);
    file.seekg(0);
    file.read(data.data(), size);

    VkPipelineCacheCreateInfo ci = {};
    ci.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    ci.initialDataSize = size;
    ci.pInitialData = data.data();

    if (vkCreatePipelineCache(VulkanCore::instance().device(), &ci, nullptr, &m_cache) != VK_SUCCESS) {
        LOG_E("PipelineCache: Failed to create cache from file (corrupted? creating empty)");
        ci.initialDataSize = 0;
        ci.pInitialData = nullptr;
        vkCreatePipelineCache(VulkanCore::instance().device(), &ci, nullptr, &m_cache);
        return false;
    }

    LOG_I("PipelineCache: Loaded %zu bytes from %s", size, filepath.c_str());
    return true;
}

bool VulkanPipelineCache::save(const std::string& filepath) {
    if (m_cache == VK_NULL_HANDLE) return false;

    size_t size;
    vkGetPipelineCacheData(VulkanCore::instance().device(), m_cache, &size, nullptr);
    if (size == 0) return false;

    std::vector<char> data(size);
    vkGetPipelineCacheData(VulkanCore::instance().device(), m_cache, &size, data.data());

    std::ofstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        LOG_E("PipelineCache: Cannot write to %s", filepath.c_str());
        return false;
    }
    file.write(data.data(), size);
    LOG_I("PipelineCache: Saved %zu bytes to %s", size, filepath.c_str());
    return true;
}

void VulkanPipelineCache::merge(VkPipelineCache other) {
    if (m_cache && other) {
        vkMergePipelineCaches(VulkanCore::instance().device(), m_cache, 1, &other);
    }
}

void VulkanPipelineCache::destroy() {
    if (m_cache) {
        vkDestroyPipelineCache(VulkanCore::instance().device(), m_cache, nullptr);
        m_cache = VK_NULL_HANDLE;
    }
}
