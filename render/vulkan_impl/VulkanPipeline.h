#pragma once

#define VK_NO_PROTOTYPES
#include <volk.h>
#include <string>
#include <vector>

// Pre-built graphics pipeline states.
// All shaders use push constants only — no descriptor sets.

class VulkanPipeline {
public:
    VulkanPipeline() = default;
    ~VulkanPipeline() { destroy(); }

    enum Type {
        Tile,       // no depth, no blend
        Planet,     // depth test, no blend
        Trail,      // no depth, alpha blend
        Icon,       // no depth, no blend (same layout as Tile)
        TileCull,   // compute
        TileOffset, // compute
        Count
    };

    struct Config {
        VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        bool depthTest = false;
        bool depthWrite = false;
        bool blend = false;
        uint32_t pushConstantSize = 128;  // bytes
        const char* vertShader = nullptr; // SPIR-V file path
        const char* fragShader = nullptr;
        const char* compShader = nullptr; // for compute pipelines
        bool instanced = false;   // true = 3 bindings (Tile/Icon), false = 1 binding (Planet/Trail)
    };

    bool createAll(VkRenderPass renderPass, VkExtent2D extent);
    void destroy();

    VkPipeline      pipeline(Type t)       const { return m_pipelines[t]; }
    VkPipelineLayout layout(Type t)        const { return m_layouts[t]; }
    VkPipelineLayout tileLayout()          const { return m_layouts[Tile]; }  // Tile + Icon share layout
    VkPipelineLayout planetLayout()        const { return m_layouts[Planet]; }
    VkPipelineLayout trailLayout()         const { return m_layouts[Trail]; }
    VkPipelineLayout tileCullLayout()      const { return m_layouts[TileCull]; }
    VkPipelineLayout tileOffsetLayout()    const { return m_layouts[TileOffset]; }

private:
    VkPipeline       m_pipelines[Count] = {};
    VkPipelineLayout m_layouts[Count] = {};

    bool createGraphicsPipeline(const Config& cfg, VkRenderPass renderPass,
                                 VkExtent2D extent, Type type);
    bool createComputePipeline(const Config& cfg, Type type);
    VkShaderModule loadShader(const std::string& path);
    VkPipelineLayout createLayout(uint32_t pushSize);
};
