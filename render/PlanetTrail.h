#pragma once

#define VK_NO_PROTOTYPES
#include <volk.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>

class VulkanPipeline;
class Camera;

class PlanetTrail {
public:
    PlanetTrail(const glm::vec3& color, float planetRadius);
    ~PlanetTrail();

    PlanetTrail(const PlanetTrail&) = delete;
    PlanetTrail& operator=(const PlanetTrail&) = delete;
    PlanetTrail(PlanetTrail&&) noexcept;
    PlanetTrail& operator=(PlanetTrail&&) noexcept;

    void update(const glm::vec2& pos, float currentTime);
    void setPoints(const float* xy, int count);
    void recordDrawCommands(VkCommandBuffer cmd, const VulkanPipeline& pipelines,
                            const Camera& camera, double camX, double camY);
    void clear();
    void setPlanetRadius(float r) { m_planetRadius = r; }

private:
    struct Point { glm::vec2 pos; float time; };
    static constexpr int MAX_TRAIL_POINTS = 256;
    Point m_pointBuf[MAX_TRAIL_POINTS];
    int m_pointCount = 0;
    int m_pointHead = 0;     // ring buffer: write here

    glm::vec2 m_center{0.0f};

    int m_maxPoints = 200;
    float m_trailDuration = 0.4f;
    float m_planetRadius;
    glm::vec3 m_color;

    // Vulkan GPU resources
    VkBuffer m_vertexBuffer = VK_NULL_HANDLE;
    VmaAllocation m_vertexAlloc = VK_NULL_HANDLE;
    VkBuffer m_indexBuffer = VK_NULL_HANDLE;
    VmaAllocation m_indexAlloc = VK_NULL_HANDLE;
    void* m_vertexMapped = nullptr;  // persistent mapped pointer
    void* m_indexMapped = nullptr;
    uint32_t m_vertexCount = 0;
    uint32_t m_indexCount = 0;
    bool m_dirty = false;
    bool m_gpuAllocated = false;

    // LOD: fewer segments when zoomed out
    int m_lodSegsPerPoint = 4;

    static glm::vec2 catmullRom(const glm::vec2& p0, const glm::vec2& p1,
                                const glm::vec2& p2, const glm::vec2& p3, float t);
    static glm::vec2 catmullRomTangent(const glm::vec2& p0, const glm::vec2& p1,
                                       const glm::vec2& p2, const glm::vec2& p3, float t);

    void ensureGPUResources();
    void rebuildGeometry();
    void writeToMappedBuffers(const std::vector<float>& verts, const std::vector<unsigned>& indices);

    // Ring buffer helpers
    Point& ringPoint(int logicalIdx) {
        return m_pointBuf[(m_pointHead - m_pointCount + logicalIdx + MAX_TRAIL_POINTS) % MAX_TRAIL_POINTS];
    }
    const Point& ringPoint(int logicalIdx) const {
        return m_pointBuf[(m_pointHead - m_pointCount + logicalIdx + MAX_TRAIL_POINTS) % MAX_TRAIL_POINTS];
    }
};
