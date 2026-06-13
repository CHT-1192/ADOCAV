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

private:
    std::vector<ShapeGroup> m_shapes;
    std::vector<ShapeGroup> m_iconGroups;
    mutable std::vector<VisibilityCache> m_visCaches;
    mutable std::vector<VisibilityCache> m_iconVisCaches;

    void destroy();
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
