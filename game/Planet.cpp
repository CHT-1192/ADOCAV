#include "Planet.h"
#include "render/PlanetTrail.h"
#include "render/vulkan_impl/VulkanPipeline.h"
#include "render/vulkan_impl/VulkanCore.h"
#include "camera/Camera.h"
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <vector>

Planet::Planet(const glm::vec3& color, bool showTrail, float r)
    : color(color), radius(r) {
    if (showTrail)
        trail = new PlanetTrail(color, radius);
}

Planet::~Planet() {
    destroyGPU();
    delete trail;
}

Planet::Planet(Planet&& o) noexcept
    : position(o.position), color(o.color), radius(o.radius), trail(o.trail),
      m_vertexBuffer(o.m_vertexBuffer), m_vertexAlloc(o.m_vertexAlloc),
      m_indexBuffer(o.m_indexBuffer), m_indexAlloc(o.m_indexAlloc),
      m_indexCount(o.m_indexCount) {
    o.trail = nullptr;
    o.m_vertexBuffer = VK_NULL_HANDLE;
    o.m_indexBuffer = VK_NULL_HANDLE;
    o.m_indexCount = 0;
}

Planet& Planet::operator=(Planet&& o) noexcept {
    if (this != &o) {
        destroyGPU();
        delete trail;
        position = o.position; color = o.color; radius = o.radius;
        trail = o.trail; m_vertexBuffer = o.m_vertexBuffer; m_indexBuffer = o.m_indexBuffer;
        m_vertexAlloc = o.m_vertexAlloc; m_indexAlloc = o.m_indexAlloc;
        m_indexCount = o.m_indexCount;
        o.trail = nullptr;
        o.m_vertexBuffer = VK_NULL_HANDLE; o.m_indexBuffer = VK_NULL_HANDLE;
        o.m_indexCount = 0;
    }
    return *this;
}

void Planet::update(float currentTime) {
    if (trail)
        trail->update(glm::vec2(position.x, position.y), currentTime);
}

void Planet::setTrailPoints(const float* xy, int count) {
    if (trail)
        trail->setPoints(xy, count);
}

void Planet::clearTrail() {
    if (trail)
        trail->clear();
}

void Planet::destroyGPU() {
    auto allocator = VulkanCore::instance().allocator();
    if (m_indexBuffer) { vmaDestroyBuffer(allocator, m_indexBuffer, m_indexAlloc); m_indexBuffer = VK_NULL_HANDLE; }
    if (m_vertexBuffer) { vmaDestroyBuffer(allocator, m_vertexBuffer, m_vertexAlloc); m_vertexBuffer = VK_NULL_HANDLE; }
}

// Generate UV sphere vertices and indices, upload to GPU via VMA
bool Planet::buildGPU() {
    destroyGPU();

    constexpr int rings = 24, sectors = 24;
    std::vector<float> verts;
    std::vector<unsigned> indices;

    for (int r = 0; r <= rings; r++) {
        float phi = 3.14159265f * (float)r / (float)rings;
        float sinPhi = std::sin(phi), cosPhi = std::cos(phi);
        for (int s = 0; s <= sectors; s++) {
            float theta = 2.0f * 3.14159265f * (float)s / (float)sectors;
            float sinTheta = std::sin(theta), cosTheta = std::cos(theta);
            verts.insert(verts.end(), {
                cosTheta * sinPhi * radius,
                cosPhi * radius,
                sinTheta * sinPhi * radius
            });
        }
    }

    for (int r = 0; r < rings; r++) {
        for (int s = 0; s <= sectors; s++) {
            unsigned cur = r * (sectors + 1) + s;
            unsigned next = (r + 1) * (sectors + 1) + s;
            indices.push_back(cur);
            indices.push_back(next);
        }
        if (r < rings - 1) {
            unsigned nextStart = (r + 1) * (sectors + 1) + sectors;
            unsigned nextNext = (r + 2) * (sectors + 1);
            indices.push_back(nextStart);
            indices.push_back(nextNext);
        }
    }

    m_indexCount = (uint32_t)indices.size();

    // Upload via VMA staging
    auto& core = VulkanCore::instance();
    auto allocator = core.allocator();

    // Vertex buffer
    {
        VkBufferCreateInfo ci = {};
        ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        ci.size = verts.size() * sizeof(float);
        ci.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

        VmaAllocationCreateInfo aci = {};
        aci.usage = VMA_MEMORY_USAGE_GPU_ONLY;

        vmaCreateBuffer(allocator, &ci, &aci, &m_vertexBuffer, &m_vertexAlloc, nullptr);

        // Staging upload
        VkBuffer staging;
        VmaAllocation stagingAlloc;
        VkBufferCreateInfo sCI = {};
        sCI.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        sCI.size = verts.size() * sizeof(float);
        sCI.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        VmaAllocationCreateInfo sACI = {};
        sACI.usage = VMA_MEMORY_USAGE_CPU_ONLY;
        vmaCreateBuffer(allocator, &sCI, &sACI, &staging, &stagingAlloc, nullptr);

        void* mapped;
        vmaMapMemory(allocator, stagingAlloc, &mapped);
        memcpy(mapped, verts.data(), verts.size() * sizeof(float));
        vmaUnmapMemory(allocator, stagingAlloc);

        // Immediate copy
        VkCommandPool pool;
        VkCommandPoolCreateInfo poolCI = {};
        poolCI.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolCI.queueFamilyIndex = core.queueFamilies().graphics;
        vkCreateCommandPool(core.device(), &poolCI, nullptr, &pool);

        VkCommandBuffer cmd;
        VkCommandBufferAllocateInfo allocI = {};
        allocI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocI.commandPool = pool;
        allocI.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocI.commandBufferCount = 1;
        vkAllocateCommandBuffers(core.device(), &allocI, &cmd);

        VkCommandBufferBeginInfo beginI = {};
        beginI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginI.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &beginI);
        VkBufferCopy region = {0, 0, verts.size() * sizeof(float)};
        vkCmdCopyBuffer(cmd, staging, m_vertexBuffer, 1, &region);
        vkEndCommandBuffer(cmd);

        VkSubmitInfo submitI = {};
        submitI.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitI.commandBufferCount = 1;
        submitI.pCommandBuffers = &cmd;
        vkQueueSubmit(core.graphicsQueue(), 1, &submitI, VK_NULL_HANDLE);
        vkQueueWaitIdle(core.graphicsQueue());

        vkFreeCommandBuffers(core.device(), pool, 1, &cmd);
        vkDestroyCommandPool(core.device(), pool, nullptr);
        vmaDestroyBuffer(allocator, staging, stagingAlloc);
    }

    // Index buffer
    {
        VkBufferCreateInfo ci = {};
        ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        ci.size = indices.size() * sizeof(unsigned);
        ci.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

        VmaAllocationCreateInfo aci = {};
        aci.usage = VMA_MEMORY_USAGE_GPU_ONLY;

        vmaCreateBuffer(allocator, &ci, &aci, &m_indexBuffer, &m_indexAlloc, nullptr);

        VkBuffer staging;
        VmaAllocation stagingAlloc;
        VkBufferCreateInfo sCI = {};
        sCI.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        sCI.size = indices.size() * sizeof(unsigned);
        sCI.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        VmaAllocationCreateInfo sACI = {};
        sACI.usage = VMA_MEMORY_USAGE_CPU_ONLY;
        vmaCreateBuffer(allocator, &sCI, &sACI, &staging, &stagingAlloc, nullptr);

        void* mapped;
        vmaMapMemory(allocator, stagingAlloc, &mapped);
        memcpy(mapped, indices.data(), indices.size() * sizeof(unsigned));
        vmaUnmapMemory(allocator, stagingAlloc);

        VkCommandPool pool;
        VkCommandPoolCreateInfo poolCI = {};
        poolCI.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolCI.queueFamilyIndex = core.queueFamilies().graphics;
        vkCreateCommandPool(core.device(), &poolCI, nullptr, &pool);

        VkCommandBuffer cmd;
        VkCommandBufferAllocateInfo allocI = {};
        allocI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocI.commandPool = pool;
        allocI.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocI.commandBufferCount = 1;
        vkAllocateCommandBuffers(core.device(), &allocI, &cmd);

        VkCommandBufferBeginInfo beginI = {};
        beginI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginI.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &beginI);
        VkBufferCopy region = {0, 0, indices.size() * sizeof(unsigned)};
        vkCmdCopyBuffer(cmd, staging, m_indexBuffer, 1, &region);
        vkEndCommandBuffer(cmd);

        VkSubmitInfo submitI = {};
        submitI.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitI.commandBufferCount = 1;
        submitI.pCommandBuffers = &cmd;
        vkQueueSubmit(core.graphicsQueue(), 1, &submitI, VK_NULL_HANDLE);
        vkQueueWaitIdle(core.graphicsQueue());

        vkFreeCommandBuffers(core.device(), pool, 1, &cmd);
        vkDestroyCommandPool(core.device(), pool, nullptr);
        vmaDestroyBuffer(allocator, staging, stagingAlloc);
    }

    return true;
}

void Planet::recordDrawCommands(VkCommandBuffer cmd, const VulkanPipeline& pipelines,
                                 const Camera& camera, double camX, double camY) const {
    if (!m_vertexBuffer) return;

    // Compute camera-relative model matrix + MVP in double→float
    glm::vec3 relPos((float)((double)position.x - camX),
                     (float)((double)position.y - camY), position.z);
    auto model = glm::translate(glm::mat4(1.0f), relPos);
    auto mvp = camera.viewProj() * model;

    struct PlanetPush { glm::mat4 mvp; glm::vec4 color; };
    PlanetPush pc;
    pc.mvp = mvp;
    pc.color = glm::vec4(color, 1.0f);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.pipeline(VulkanPipeline::Planet));
    vkCmdPushConstants(cmd, pipelines.planetLayout(),
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(PlanetPush), &pc);

    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &m_vertexBuffer, &offset);
    vkCmdBindIndexBuffer(cmd, m_indexBuffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, m_indexCount, 1, 0, 0, 0);
}
