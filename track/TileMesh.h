#pragma once

#define VK_NO_PROTOTYPES
#include <volk.h>
#include <vk_mem_alloc.h>
#include "level/LevelData.h"
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <map>
#include <tuple>
#include <unordered_map>

class VulkanPipeline;

// Per-instance data (world position + bounding box for culling)
struct TileInstance {
    double offX, offY;
    float  offZ;
    float  fillR, fillG, fillB;
    float  strokeR, strokeG, strokeB;
    float  opacity;
    double minX, minY, maxX, maxY;
};

struct ShapeGroup {
    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VmaAllocation vertexAlloc = VK_NULL_HANDLE;
    VkBuffer indexBuffer = VK_NULL_HANDLE;
    VmaAllocation indexAlloc = VK_NULL_HANDLE;
    VkBuffer instPosBuffer = VK_NULL_HANDLE;
    VmaAllocation instPosAlloc = VK_NULL_HANDLE;
    void*     instPosMapped = nullptr;   // persistent write pointer
    VkBuffer instColorBuffer = VK_NULL_HANDLE;
    VmaAllocation instColorAlloc = VK_NULL_HANDLE;

    uint32_t indexCount = 0;
    uint32_t instanceCount = 0;
    std::vector<TileInstance> instances;
};

// Visibility cache: avoid recomputing visible set when camera hasn't moved
struct VisibilityCache {
    std::vector<uint32_t> indices;
    std::vector<float>    offsets;
    double vl=0, vr=0, vb=0, vt=0;
    bool valid = false;
};

class TileMesh {
public:
    TileMesh() = default;
    ~TileMesh();

    TileMesh(const TileMesh&) = delete;
    TileMesh& operator=(const TileMesh&) = delete;
    TileMesh(TileMesh&&) noexcept;
    TileMesh& operator=(TileMesh&&) noexcept;

    void build(const LevelData& level,
               const std::string& fillColorHex = "FFFFFF",
               const std::string& strokeColorHex = "000000");

    void recordTileDrawCommands(VkCommandBuffer cmd, const VulkanPipeline& pipelines,
                                 float viewL, float viewR, float viewB, float viewT,
                                 double camX, double camY) const;
    void recordIconDrawCommands(VkCommandBuffer cmd, const VulkanPipeline& pipelines,
                                 float viewL, float viewR, float viewB, float viewT,
                                 double camX, double camY) const;

    bool empty() const;
    uint32_t totalTileCount() const { return m_totalTileCount; }

    // Exposed for parallel culling
    static bool frustumCheck(const VisibilityCache& cache, float vl, float vr, float vb, float vt) {
        if (!cache.valid) return true;
        return std::abs((float)cache.vl - vl) > 0.5f || std::abs((float)cache.vr - vr) > 0.5f
            || std::abs((float)cache.vb - vb) > 0.5f || std::abs((float)cache.vt - vt) > 0.5f;
    }

    // Buffer accessors for descriptor set creation
    VkBuffer debugTileBoundsBuf() const { return m_tileBoundsBuf; }
    VkBuffer debugTilePositionsBuf() const { return m_tilePositionsBuf; }
    VkBuffer debugVisibleFlagsBuf() const { return m_visibleFlagsBuf; }
    VkBuffer debugInstanceOffsetsBuf() const { return m_instanceOffsetsBuf; }
    VkBuffer debugIndirectBuf() const { return m_indirectBuf; }

    // GPU culling: update visibility + offsets via compute shader
    void recordCullDispatch(VkCommandBuffer cmd, const VulkanPipeline& pipelines,
                            VkDescriptorSet ds, float vl, float vr, float vb, float vt,
                            double camX, double camY) const;
    // Draw tiles using indirect commands (after GPU culling)
    void recordIndirectDraws(VkCommandBuffer cmd, const VulkanPipeline& pipelines) const;

private:
    std::vector<ShapeGroup> m_shapes;
    std::vector<ShapeGroup> m_iconGroups;
    mutable std::vector<VisibilityCache> m_visCaches;
    mutable std::vector<VisibilityCache> m_iconVisCaches;

    // GPU culling state
    bool m_gpuCulling = true;
    uint32_t m_totalTileCount = 0;
    std::vector<uint32_t> m_groupBaseInstances;  // firstInstance for each shape group

    // Unified GPU buffers for compute shader
    VkBuffer m_tileBoundsBuf = VK_NULL_HANDLE;
    VmaAllocation m_tileBoundsAlloc = VK_NULL_HANDLE;
    VkBuffer m_tilePositionsBuf = VK_NULL_HANDLE;
    VmaAllocation m_tilePositionsAlloc = VK_NULL_HANDLE;
    VkBuffer m_instanceOffsetsBuf = VK_NULL_HANDLE;   // VERTEX + STORAGE
    VmaAllocation m_instanceOffsetsAlloc = VK_NULL_HANDLE;
    VkBuffer m_visibleFlagsBuf = VK_NULL_HANDLE;
    VmaAllocation m_visibleFlagsAlloc = VK_NULL_HANDLE;

    // Indirect draw buffer (one command per shape group)
    std::vector<VkDrawIndexedIndirectCommand> m_indirectCommands;
    VkBuffer m_indirectBuf = VK_NULL_HANDLE;
    VmaAllocation m_indirectAlloc = VK_NULL_HANDLE;

    void destroy();
    void destroyGpuCullBuffers();
    void buildIcons(const LevelData& level);
    static unsigned int hexToUInt(const std::string& hex);
    static bool frustumChanged(const VisibilityCache& cache, float vl, float vr, float vb, float vt);

    void uploadShapeGroupBuffers(ShapeGroup& sg,
                                  const std::vector<float>& interleavedVerts,
                                  const std::vector<unsigned>& idxCopy,
                                  const std::vector<float>& ipos,
                                  const std::vector<float>& icols);

    void shapeDrawCommands(VkCommandBuffer cmd, const VulkanPipeline& pipelines,
                            const std::vector<ShapeGroup>& groups,
                            std::vector<VisibilityCache>& caches,
                            float vl, float vr, float vb, float vt,
                            double camX, double camY, bool isIcon) const;
};
