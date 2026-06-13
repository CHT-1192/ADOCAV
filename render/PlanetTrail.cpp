#include "PlanetTrail.h"
#include "render/vulkan_impl/VulkanPipeline.h"
#include "render/vulkan_impl/VulkanCore.h"
#include "camera/Camera.h"
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <algorithm>
#include <cstring>

PlanetTrail::PlanetTrail(const glm::vec3& color, float planetRadius)
    : m_planetRadius(planetRadius), m_color(color) {}

PlanetTrail::~PlanetTrail() {
    auto allocator = VulkanCore::instance().allocator();
    if (m_indexBuffer) vmaDestroyBuffer(allocator, m_indexBuffer, m_indexAlloc);
    if (m_vertexBuffer) vmaDestroyBuffer(allocator, m_vertexBuffer, m_vertexAlloc);
}

PlanetTrail::PlanetTrail(PlanetTrail&& o) noexcept
    : m_pointCount(o.m_pointCount), m_pointHead(o.m_pointHead),
      m_center(o.m_center), m_maxPoints(o.m_maxPoints),
      m_trailDuration(o.m_trailDuration), m_planetRadius(o.m_planetRadius), m_color(o.m_color),
      m_vertexBuffer(o.m_vertexBuffer), m_vertexAlloc(o.m_vertexAlloc),
      m_indexBuffer(o.m_indexBuffer), m_indexAlloc(o.m_indexAlloc),
      m_vertexMapped(o.m_vertexMapped), m_indexMapped(o.m_indexMapped),
      m_vertexCount(o.m_vertexCount), m_indexCount(o.m_indexCount),
      m_dirty(o.m_dirty), m_gpuAllocated(o.m_gpuAllocated) {
    memcpy(m_pointBuf, o.m_pointBuf, sizeof(m_pointBuf));
    o.m_vertexBuffer = VK_NULL_HANDLE; o.m_indexBuffer = VK_NULL_HANDLE;
    o.m_vertexMapped = nullptr; o.m_indexMapped = nullptr;
    o.m_vertexCount = o.m_indexCount = 0; o.m_pointCount = 0;
    o.m_gpuAllocated = false;
}

PlanetTrail& PlanetTrail::operator=(PlanetTrail&& o) noexcept {
    if (this != &o) {
        auto allocator = VulkanCore::instance().allocator();
        if (m_indexBuffer) vmaDestroyBuffer(allocator, m_indexBuffer, m_indexAlloc);
        if (m_vertexBuffer) vmaDestroyBuffer(allocator, m_vertexBuffer, m_vertexAlloc);
        m_pointCount = o.m_pointCount; m_pointHead = o.m_pointHead;
        memcpy(m_pointBuf, o.m_pointBuf, sizeof(m_pointBuf));
        m_center = o.m_center; m_maxPoints = o.m_maxPoints;
        m_trailDuration = o.m_trailDuration; m_planetRadius = o.m_planetRadius;
        m_color = o.m_color;
        m_vertexBuffer = o.m_vertexBuffer; m_vertexAlloc = o.m_vertexAlloc;
        m_indexBuffer = o.m_indexBuffer; m_indexAlloc = o.m_indexAlloc;
        m_vertexMapped = o.m_vertexMapped; m_indexMapped = o.m_indexMapped;
        m_vertexCount = o.m_vertexCount; m_indexCount = o.m_indexCount;
        m_dirty = o.m_dirty; m_gpuAllocated = o.m_gpuAllocated;
        o.m_vertexBuffer = VK_NULL_HANDLE; o.m_indexBuffer = VK_NULL_HANDLE;
        o.m_vertexMapped = nullptr; o.m_indexMapped = nullptr;
        o.m_vertexCount = o.m_indexCount = 0; o.m_pointCount = 0;
        o.m_gpuAllocated = false;
    }
    return *this;
}

// Catmull-Rom spline interpolation
glm::vec2 PlanetTrail::catmullRom(const glm::vec2& p0, const glm::vec2& p1,
                                   const glm::vec2& p2, const glm::vec2& p3, float t) {
    float t2 = t * t, t3 = t2 * t;
    return (-0.5f * t3 + t2 - 0.5f * t) * p0
         + ( 1.5f * t3 - 2.5f * t2 + 1.0f) * p1
         + (-1.5f * t3 + 2.0f * t2 + 0.5f * t) * p2
         + ( 0.5f * t3 - 0.5f * t2) * p3;
}

glm::vec2 PlanetTrail::catmullRomTangent(const glm::vec2& p0, const glm::vec2& p1,
                                          const glm::vec2& p2, const glm::vec2& p3, float t) {
    float t2 = t * t;
    return (-1.5f * t2 + 2.0f * t - 0.5f) * p0
         + ( 4.5f * t2 - 5.0f * t) * p1
         + (-4.5f * t2 + 4.0f * t + 0.5f) * p2
         + ( 1.5f * t2 - 1.0f * t) * p3;
}

void PlanetTrail::update(const glm::vec2& pos, float currentTime) {
    m_pointBuf[m_pointHead] = {pos, currentTime};
    m_pointHead = (m_pointHead + 1) % MAX_TRAIL_POINTS;
    if (m_pointCount < MAX_TRAIL_POINTS) m_pointCount++;

    while (m_pointCount > 0) {
        if (currentTime - ringPoint(0).time <= m_trailDuration) break;
        m_pointCount--;
    }
    while (m_pointCount > m_maxPoints)
        m_pointCount--;

    m_dirty = true;
}

void PlanetTrail::clear() {
    m_pointCount = 0;
    m_pointHead = 0;
    m_dirty = true;
}

void PlanetTrail::ensureGPUResources() {
    if (m_gpuAllocated) return;

    auto& core = VulkanCore::instance();
    auto allocator = core.allocator();

    // Max vertices: (maxPoints-1) * segsPerPoint segments, each generates 2 vertices
    // + 2 more for the endpoints → (maxSegments + 1) * 2
    int maxSegments = (m_maxPoints - 1) * m_lodSegsPerPoint;
    int maxVerts = (maxSegments + 1) * 2;
    int maxIndices = maxSegments * 6;

    // Vertex buffer (persistently mapped)
    {
        VkBufferCreateInfo ci = {};
        ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        ci.size = (VkDeviceSize)maxVerts * 3 * sizeof(float);
        ci.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        VmaAllocationCreateInfo aci = {};
        aci.usage = VMA_MEMORY_USAGE_AUTO;
        aci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                    VMA_ALLOCATION_CREATE_MAPPED_BIT;
        VmaAllocationInfo allocInfo;
        vmaCreateBuffer(allocator, &ci, &aci, &m_vertexBuffer, &m_vertexAlloc, &allocInfo);
        m_vertexMapped = allocInfo.pMappedData;
    }

    // Index buffer (persistently mapped)
    {
        VkBufferCreateInfo ci = {};
        ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        ci.size = (VkDeviceSize)maxIndices * sizeof(unsigned);
        ci.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        VmaAllocationCreateInfo aci = {};
        aci.usage = VMA_MEMORY_USAGE_AUTO;
        aci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                    VMA_ALLOCATION_CREATE_MAPPED_BIT;
        VmaAllocationInfo allocInfo;
        vmaCreateBuffer(allocator, &ci, &aci, &m_indexBuffer, &m_indexAlloc, &allocInfo);
        m_indexMapped = allocInfo.pMappedData;
    }

    m_gpuAllocated = true;
}

void PlanetTrail::writeToMappedBuffers(const std::vector<float>& verts,
                                        const std::vector<unsigned>& indices) {
    if (m_vertexMapped)
        memcpy(m_vertexMapped, verts.data(), verts.size() * sizeof(float));
    if (m_indexMapped)
        memcpy(m_indexMapped, indices.data(), indices.size() * sizeof(unsigned));
}

void PlanetTrail::setPoints(const float* xy, int count) {
    if (count < 2) { clear(); return; }

    // Write points to ring buffer
    m_pointHead = 0;
    m_pointCount = std::min(count, MAX_TRAIL_POINTS);
    for (int i = 0; i < m_pointCount; i++) {
        m_pointBuf[i] = {{xy[i * 2], xy[i * 2 + 1]}, 0.0f};
    }

    ensureGPUResources();

    int n = m_pointCount;
    m_center = (m_pointBuf[0].pos + m_pointBuf[n-1].pos) * 0.5f;

    int totalSegments = (n - 1) * m_lodSegsPerPoint;
    int vertCount = (totalSegments + 1) * 2;
    int idxCount = totalSegments * 6;

    std::vector<float> verts(vertCount * 3);
    std::vector<unsigned> indices(idxCount);

    float maxWidth = m_planetRadius * 2.0f;
    int vi = 0;
    for (int seg = 0; seg <= totalSegments; seg++) {
        float globalT = (float)seg / (float)totalSegments;
        float rawIdx = globalT * (n - 1);
        int i = (int)rawIdx;
        float localT = rawIdx - (float)i;
        auto getP = [&](int idx) -> glm::vec2 {
            int j = std::max(0, std::min(n - 1, idx));
            return ringPoint(j).pos;
        };
        glm::vec2 p0 = getP(i - 1);
        glm::vec2 p1 = getP(i);
        glm::vec2 p2 = getP(i + 1);
        glm::vec2 p3 = getP(i + 2);
        glm::vec2 pt = catmullRom(p0, p1, p2, p3, localT);
        glm::vec2 tangent = catmullRomTangent(p0, p1, p2, p3, localT);
        float len = std::sqrt(tangent.x * tangent.x + tangent.y * tangent.y);
        glm::vec2 normal = (len > 0.001f) ? glm::vec2(-tangent.y / len, tangent.x / len) : glm::vec2(0.0f, 1.0f);
        float width = maxWidth * globalT;
        verts[vi * 3 + 0] = pt.x - m_center.x - normal.x * width * 0.5f;
        verts[vi * 3 + 1] = pt.y - m_center.y - normal.y * width * 0.5f;
        verts[vi * 3 + 2] = 0.0f; vi++;
        verts[vi * 3 + 0] = pt.x - m_center.x + normal.x * width * 0.5f;
        verts[vi * 3 + 1] = pt.y - m_center.y + normal.y * width * 0.5f;
        verts[vi * 3 + 2] = 0.0f; vi++;
    }

    int ii = 0;
    for (int seg = 0; seg < totalSegments; seg++) {
        unsigned base = seg * 2;
        indices[ii++] = base; indices[ii++] = base + 1; indices[ii++] = base + 2;
        indices[ii++] = base + 1; indices[ii++] = base + 3; indices[ii++] = base + 2;
    }

    m_vertexCount = vertCount;
    m_indexCount = idxCount;
    m_dirty = false;

    writeToMappedBuffers(verts, indices);
}

void PlanetTrail::rebuildGeometry() {
    int n = m_pointCount;
    if (n < 2) { m_vertexCount = 0; m_indexCount = 0; return; }

    auto& first = ringPoint(0);
    auto& last = ringPoint(n - 1);
    m_center = (first.pos + last.pos) * 0.5f;

    int totalSegments = (n - 1) * m_lodSegsPerPoint;
    int vertCount = (totalSegments + 1) * 2;
    int idxCount = totalSegments * 6;

    std::vector<float> verts(vertCount * 3);
    std::vector<unsigned> indices(idxCount);

    float maxWidth = m_planetRadius * 2.0f;
    int vi = 0;

    for (int seg = 0; seg <= totalSegments; seg++) {
        float globalT = (float)seg / (float)totalSegments;
        float rawIdx = globalT * (n - 1);
        int i = (int)rawIdx;
        float localT = rawIdx - (float)i;
        auto getP2 = [&](int idx) -> glm::vec2 {
            int j = std::max(0, std::min(n - 1, idx));
            return ringPoint(j).pos;
        };
        glm::vec2 p0 = getP2(i - 1);
        glm::vec2 p1 = getP2(i);
        glm::vec2 p2 = getP2(i + 1);
        glm::vec2 p3 = getP2(i + 2);
        glm::vec2 pt = catmullRom(p0, p1, p2, p3, localT);
        glm::vec2 tangent = catmullRomTangent(p0, p1, p2, p3, localT);
        float len = std::sqrt(tangent.x * tangent.x + tangent.y * tangent.y);
        glm::vec2 normal = (len > 0.001f) ? glm::vec2(-tangent.y / len, tangent.x / len) : glm::vec2(0.0f, 1.0f);
        float width = maxWidth * globalT;
        verts[vi * 3 + 0] = pt.x - m_center.x - normal.x * width * 0.5f;
        verts[vi * 3 + 1] = pt.y - m_center.y - normal.y * width * 0.5f;
        verts[vi * 3 + 2] = 0.0f; vi++;
        verts[vi * 3 + 0] = pt.x - m_center.x + normal.x * width * 0.5f;
        verts[vi * 3 + 1] = pt.y - m_center.y + normal.y * width * 0.5f;
        verts[vi * 3 + 2] = 0.0f; vi++;
    }

    int ii = 0;
    for (int seg = 0; seg < totalSegments; seg++) {
        unsigned base = seg * 2;
        indices[ii++] = base; indices[ii++] = base + 1; indices[ii++] = base + 2;
        indices[ii++] = base + 1; indices[ii++] = base + 3; indices[ii++] = base + 2;
    }

    m_vertexCount = vertCount;
    m_indexCount = idxCount;
    m_dirty = false;

    writeToMappedBuffers(verts, indices);
}

void PlanetTrail::recordDrawCommands(VkCommandBuffer cmd, const VulkanPipeline& pipelines,
                                      const Camera& camera, double camX, double camY) {
    ensureGPUResources();
    if (m_pointCount < 2) return;
    if (m_dirty) rebuildGeometry();
    if (m_indexCount == 0) return;

    auto model = glm::translate(glm::mat4(1.0f),
        glm::vec3(m_center.x - (float)camX, m_center.y - (float)camY, 0.0f));
    auto mvp = camera.viewProj() * model;

    struct TrailPush { glm::mat4 mvp; glm::vec4 color; };
    TrailPush pc;
    pc.mvp = mvp;
    pc.color = glm::vec4(m_color, 0.6f);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.pipeline(VulkanPipeline::Trail));
    vkCmdPushConstants(cmd, pipelines.trailLayout(),
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(TrailPush), &pc);

    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &m_vertexBuffer, &offset);
    vkCmdBindIndexBuffer(cmd, m_indexBuffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, m_indexCount, 1, 0, 0, 0);
}
