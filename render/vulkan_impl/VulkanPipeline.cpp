#include "VulkanPipeline.h"
#include "VulkanCore.h"
#include "VulkanPipelineCache.h"
#include "util/Logger.h"
#include <fstream>
#include <vector>

bool VulkanPipeline::createAll(VkRenderPass renderPass, VkExtent2D extent) {
    // Tile pipeline — instanced, no depth, no blend
    Config tileCfg;
    tileCfg.instanced = true;
    tileCfg.pushConstantSize = 64;  // just mat4 uVP
    tileCfg.depthTest = false;
    tileCfg.blend = false;
    tileCfg.vertShader = "shaders/tile.vert.spv";
    tileCfg.fragShader = "shaders/tile.frag.spv";
    if (!createGraphicsPipeline(tileCfg, renderPass, extent, Tile)) return false;

    // Planet pipeline — depth test, no blend
    Config planetCfg;
    planetCfg.pushConstantSize = 128;  // mat4 uMVP (64 bytes) + vec4 uColor (16 bytes)
    planetCfg.depthTest = true;
    planetCfg.depthWrite = true;
    planetCfg.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    planetCfg.vertShader = "shaders/planet.vert.spv";
    planetCfg.fragShader = "shaders/planet.frag.spv";
    if (!createGraphicsPipeline(planetCfg, renderPass, extent, Planet)) return false;

    // Trail pipeline — no depth, alpha blend
    Config trailCfg;
    trailCfg.pushConstantSize = 128;  // mat4 uMVP + vec4 uColor
    trailCfg.depthTest = false;
    trailCfg.blend = true;
    trailCfg.vertShader = "shaders/trail.vert.spv";
    trailCfg.fragShader = "shaders/trail.frag.spv";
    if (!createGraphicsPipeline(trailCfg, renderPass, extent, Trail)) return false;

    // Icon pipeline — same layout as Tile, no depth, no blend
    Config iconCfg;
    iconCfg.instanced = true;
    iconCfg.pushConstantSize = 64;
    iconCfg.depthTest = false;
    iconCfg.blend = false;
    iconCfg.vertShader = "shaders/icon.vert.spv";
    iconCfg.fragShader = "shaders/icon.frag.spv";
    if (!createGraphicsPipeline(iconCfg, renderPass, extent, Icon)) return false;

    // Compute descriptor layout (shared by both compute pipelines)
    if (!createComputeDescriptorLayout()) return false;

    // Compute pipelines
    Config cullCfg;
    cullCfg.useDescriptors = true;
    cullCfg.pushConstantSize = 128;  // frustum[6] (96 bytes) + camWorld (8 bytes) = 104
    cullCfg.compShader = "shaders/tile_cull.comp.spv";
    if (!createComputePipeline(cullCfg, TileCull)) return false;

    Config offsetCfg;
    offsetCfg.useDescriptors = true;
    offsetCfg.pushConstantSize = 16;  // camWorld (8 bytes)
    offsetCfg.compShader = "shaders/tile_offset.comp.spv";
    if (!createComputePipeline(offsetCfg, TileOffset)) return false;

    LOG_I("VulkanPipeline: %d pipelines created", (int)Count);
    return true;
}

void VulkanPipeline::destroy() {
    auto device = VulkanCore::instance().device();
    for (int i = 0; i < Count; i++) {
        if (m_pipelines[i]) { vkDestroyPipeline(device, m_pipelines[i], nullptr); m_pipelines[i] = VK_NULL_HANDLE; }
        if (m_layouts[i])   { vkDestroyPipelineLayout(device, m_layouts[i], nullptr); m_layouts[i] = VK_NULL_HANDLE; }
    }
    destroyDescriptors();
}

VkShaderModule VulkanPipeline::loadShader(const std::string& path) {
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        LOG_E("VulkanPipeline: Cannot open shader: %s", path.c_str());
        return VK_NULL_HANDLE;
    }

    size_t fileSize = (size_t)file.tellg();
    std::vector<char> buffer(fileSize);
    file.seekg(0);
    file.read(buffer.data(), fileSize);

    VkShaderModuleCreateInfo ci = {};
    ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = fileSize;
    ci.pCode = reinterpret_cast<const uint32_t*>(buffer.data());

    VkShaderModule module;
    if (vkCreateShaderModule(VulkanCore::instance().device(), &ci, nullptr, &module) != VK_SUCCESS) {
        LOG_E("VulkanPipeline: Failed to create shader module: %s", path.c_str());
        return VK_NULL_HANDLE;
    }
    return module;
}

VkPipelineLayout VulkanPipeline::createLayout(uint32_t pushSize) {
    VkPushConstantRange pushRange = {};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;
    pushRange.offset = 0;
    pushRange.size = pushSize;

    VkPipelineLayoutCreateInfo ci = {};
    ci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    ci.pushConstantRangeCount = 1;
    ci.pPushConstantRanges = &pushRange;

    VkPipelineLayout layout;
    vkCreatePipelineLayout(VulkanCore::instance().device(), &ci, nullptr, &layout);
    return layout;
}

// Overload with descriptor set layouts (for compute pipelines)
static VkPipelineLayout createLayoutWithDS(uint32_t pushSize, VkDescriptorSetLayout dsLayout) {
    VkPushConstantRange pushRange = {};
    pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushRange.offset = 0;
    pushRange.size = pushSize;

    VkPipelineLayoutCreateInfo ci = {};
    ci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    ci.setLayoutCount = 1;
    ci.pSetLayouts = &dsLayout;
    ci.pushConstantRangeCount = 1;
    ci.pPushConstantRanges = &pushRange;

    VkPipelineLayout layout;
    if (vkCreatePipelineLayout(VulkanCore::instance().device(), &ci, nullptr, &layout) != VK_SUCCESS) {
        LOG_E("VulkanPipeline: Failed to create pipeline layout with descriptor set");
        return VK_NULL_HANDLE;
    }
    return layout;
}

bool VulkanPipeline::createGraphicsPipeline(const Config& cfg, VkRenderPass renderPass,
                                              VkExtent2D extent, Type type) {
    auto device = VulkanCore::instance().device();

    VkShaderModule vert = loadShader(cfg.vertShader);
    VkShaderModule frag = loadShader(cfg.fragShader);
    if (!vert || !frag) return false;

    VkPipelineShaderStageCreateInfo vertStage = {};
    vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertStage.module = vert;
    vertStage.pName = "main";

    VkPipelineShaderStageCreateInfo fragStage = {};
    fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragStage.module = frag;
    fragStage.pName = "main";

    VkPipelineShaderStageCreateInfo stages[] = {vertStage, fragStage};

    // Vertex input
    VkVertexInputBindingDescription bindings[3] = {};
    VkVertexInputAttributeDescription attrs[6] = {};
    VkPipelineVertexInputStateCreateInfo vertexInput = {};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    if (cfg.instanced) {
        // 4 bindings for instanced rendering (Tile, Icon)
        // Binding 0: vertex data [x, y, z, type] — 4 floats
        bindings[0].binding = 0;
        bindings[0].stride = 4 * sizeof(float);
        bindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        // Binding 1: instance offsets [offX, offY, offZ] — 3 floats
        bindings[1].binding = 1;
        bindings[1].stride = 3 * sizeof(float);
        bindings[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

        // Binding 2: instance colors — 7 floats
        bindings[2].binding = 2;
        bindings[2].stride = 7 * sizeof(float);
        bindings[2].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

        // Binding 3: visibility flag (uint, per-instance, GPU culling)
        bindings[3].binding = 3;
        bindings[3].stride = sizeof(uint32_t);
        bindings[3].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

        attrs[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0};
        attrs[1] = {1, 0, VK_FORMAT_R32_SFLOAT, 3 * sizeof(float)};
        attrs[2] = {2, 1, VK_FORMAT_R32G32B32_SFLOAT, 0};
        attrs[3] = {3, 2, VK_FORMAT_R32G32B32_SFLOAT, 0};
        attrs[4] = {4, 2, VK_FORMAT_R32G32B32_SFLOAT, 3 * sizeof(float)};
        attrs[5] = {5, 2, VK_FORMAT_R32_SFLOAT, 6 * sizeof(float)};
        attrs[6] = {6, 3, VK_FORMAT_R32_UINT, 0};

        vertexInput.vertexBindingDescriptionCount = 4;
        vertexInput.vertexAttributeDescriptionCount = 7;
    } else {
        // 1 binding for simple rendering (Planet, Trail)
        // Binding 0: vertex position [x, y, z] — 3 floats
        bindings[0].binding = 0;
        bindings[0].stride = 3 * sizeof(float);
        bindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        attrs[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0};

        vertexInput.vertexBindingDescriptionCount = 1;
        vertexInput.vertexAttributeDescriptionCount = 1;
    }

    vertexInput.pVertexBindingDescriptions = bindings;
    vertexInput.pVertexAttributeDescriptions = attrs;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = cfg.topology;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // Dynamic viewport + scissor for letterboxing
    VkPipelineViewportStateCreateInfo viewportState = {};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer = {};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_TRUE;  // allow z outside [0,1] (OpenGL proj → Vulkan NDC)
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;  // match OpenGL

    VkPipelineMultisampleStateCreateInfo multisampling = {};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil = {};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = cfg.depthTest ? VK_TRUE : VK_FALSE;
    depthStencil.depthWriteEnable = cfg.depthWrite ? VK_TRUE : VK_FALSE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

    VkPipelineColorBlendAttachmentState colorBlendAtt = {};
    colorBlendAtt.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                    VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    if (cfg.blend) {
        colorBlendAtt.blendEnable = VK_TRUE;
        colorBlendAtt.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        colorBlendAtt.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        colorBlendAtt.colorBlendOp = VK_BLEND_OP_ADD;
        colorBlendAtt.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlendAtt.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        colorBlendAtt.alphaBlendOp = VK_BLEND_OP_ADD;
    }

    VkPipelineColorBlendStateCreateInfo colorBlending = {};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAtt;

    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState = {};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    // Layout
    m_layouts[type] = createLayout(cfg.pushConstantSize);

    VkGraphicsPipelineCreateInfo ci = {};
    ci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    ci.stageCount = 2;
    ci.pStages = stages;
    ci.pVertexInputState = &vertexInput;
    ci.pInputAssemblyState = &inputAssembly;
    ci.pViewportState = &viewportState;
    ci.pRasterizationState = &rasterizer;
    ci.pMultisampleState = &multisampling;
    ci.pDepthStencilState = &depthStencil;
    ci.pColorBlendState = &colorBlending;
    ci.pDynamicState = &dynamicState;
    ci.layout = m_layouts[type];
    ci.renderPass = renderPass;
    ci.subpass = 0;
    ci.basePipelineHandle = VK_NULL_HANDLE;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &ci, nullptr, &m_pipelines[type]) != VK_SUCCESS) {
        LOG_E("VulkanPipeline: Failed to create graphics pipeline type=%d", (int)type);
        return false;
    }

    vkDestroyShaderModule(device, vert, nullptr);
    vkDestroyShaderModule(device, frag, nullptr);
    return true;
}

bool VulkanPipeline::createComputePipeline(const Config& cfg, Type type) {
    auto device = VulkanCore::instance().device();

    VkShaderModule comp = loadShader(cfg.compShader);
    if (!comp) return false;

    VkPipelineShaderStageCreateInfo stage = {};
    stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stage.module = comp;
    stage.pName = "main";

    if (cfg.useDescriptors)
        m_layouts[type] = createLayoutWithDS(cfg.pushConstantSize, m_computeDescriptorLayout);
    else
        m_layouts[type] = createLayout(cfg.pushConstantSize);

    VkComputePipelineCreateInfo ci = {};
    ci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    ci.stage = stage;
    ci.layout = m_layouts[type];

    if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &ci, nullptr, &m_pipelines[type]) != VK_SUCCESS) {
        LOG_E("VulkanPipeline: Failed to create compute pipeline type=%d", (int)type);
        return false;
    }

    vkDestroyShaderModule(device, comp, nullptr);
    return true;
}

// ---- Descriptor support (for compute shaders) ----

bool VulkanPipeline::createComputeDescriptorLayout() {
    auto device = VulkanCore::instance().device();

    VkDescriptorSetLayoutBinding bindings[5] = {};

    // Binding 0: TileBounds (read-only SSBO)
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Binding 1: TilePositions (read-only SSBO)
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Binding 2: VisibleFlags (writable SSBO)
    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Binding 3: InstanceOffsets (writable SSBO)
    bindings[3].binding = 3;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Binding 4: IndirectCommands (writable SSBO)
    bindings[4].binding = 4;
    bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[4].descriptorCount = 1;
    bindings[4].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo layoutCI = {};
    layoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutCI.bindingCount = 5;
    layoutCI.pBindings = bindings;

    if (vkCreateDescriptorSetLayout(device, &layoutCI, nullptr, &m_computeDescriptorLayout) != VK_SUCCESS) {
        LOG_E("VulkanPipeline: Failed to create compute descriptor set layout");
        return false;
    }

    // Descriptor pool (1 set, 4 storage buffers)
    VkDescriptorPoolSize poolSize = {};
    poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSize.descriptorCount = 5;

    VkDescriptorPoolCreateInfo poolCI = {};
    poolCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolCI.maxSets = 1;
    poolCI.poolSizeCount = 1;
    poolCI.pPoolSizes = &poolSize;

    if (vkCreateDescriptorPool(device, &poolCI, nullptr, &m_descriptorPool) != VK_SUCCESS) {
        LOG_E("VulkanPipeline: Failed to create descriptor pool");
        return false;
    }

    return true;
}

void VulkanPipeline::destroyDescriptors() {
    auto device = VulkanCore::instance().device();
    if (m_descriptorPool)            { vkDestroyDescriptorPool(device, m_descriptorPool, nullptr); m_descriptorPool = VK_NULL_HANDLE; }
    if (m_computeDescriptorLayout)   { vkDestroyDescriptorSetLayout(device, m_computeDescriptorLayout, nullptr); m_computeDescriptorLayout = VK_NULL_HANDLE; }
}

VkDescriptorSet VulkanPipeline::createComputeDescriptorSet(
    VkBuffer tileBoundsBuf, VkBuffer tilePositionsBuf,
    VkBuffer visibleFlagsBuf, VkBuffer instanceOffsetsBuf,
    VkBuffer indirectBuf) const {
    auto device = VulkanCore::instance().device();

    VkDescriptorSet ds;
    VkDescriptorSetAllocateInfo allocI = {};
    allocI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocI.descriptorPool = m_descriptorPool;
    allocI.descriptorSetCount = 1;
    allocI.pSetLayouts = &m_computeDescriptorLayout;

    if (vkAllocateDescriptorSets(device, &allocI, &ds) != VK_SUCCESS) {
        LOG_E("VulkanPipeline: Failed to allocate compute descriptor set");
        return VK_NULL_HANDLE;
    }

    VkDescriptorBufferInfo bufInfos[5] = {};
    VkWriteDescriptorSet writes[5] = {};

    for (int i = 0; i < 5; i++) {
        bufInfos[i].offset = 0;
        bufInfos[i].range = VK_WHOLE_SIZE;
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = ds;
        writes[i].dstBinding = i;
        writes[i].dstArrayElement = 0;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    }

    bufInfos[0].buffer = tileBoundsBuf;       writes[0].pBufferInfo = &bufInfos[0];
    bufInfos[1].buffer = tilePositionsBuf;    writes[1].pBufferInfo = &bufInfos[1];
    bufInfos[2].buffer = visibleFlagsBuf;      writes[2].pBufferInfo = &bufInfos[2];
    bufInfos[3].buffer = instanceOffsetsBuf;  writes[3].pBufferInfo = &bufInfos[3];
    bufInfos[4].buffer = indirectBuf;         writes[4].pBufferInfo = &bufInfos[4];

    vkUpdateDescriptorSets(device, 5, writes, 0, nullptr);
    return ds;
}
