#pragma once

#define VK_NO_PROTOTYPES
#include <volk.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>

class PlanetTrail;
class VulkanPipeline;
class Camera;

class Planet {
public:
    glm::vec3 position{0.0f};
    glm::vec3 color{1.0f};
    float radius = 0.25f;

    PlanetTrail* trail = nullptr;

    Planet() = default;
    Planet(const glm::vec3& color, bool showTrail, float planetRadius = 0.25f);
    ~Planet();

    Planet(const Planet&) = delete;
    Planet& operator=(const Planet&) = delete;
    Planet(Planet&&) noexcept;
    Planet& operator=(Planet&&) noexcept;

    void update(float currentTime);
    void setTrailPoints(const float* xy, int count);
    void clearTrail();

    bool buildGPU();
    void recordDrawCommands(VkCommandBuffer cmd, const VulkanPipeline& pipelines,
                            const Camera& camera, double camX, double camY) const;
    bool gpuBuilt() const { return m_vertexBuffer != VK_NULL_HANDLE; }

private:
    VkBuffer m_vertexBuffer = VK_NULL_HANDLE;
    VmaAllocation m_vertexAlloc = VK_NULL_HANDLE;
    VkBuffer m_indexBuffer = VK_NULL_HANDLE;
    VmaAllocation m_indexAlloc = VK_NULL_HANDLE;
    uint32_t m_indexCount = 0;

    void destroyGPU();
};
