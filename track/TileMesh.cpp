#include "TileMesh.h"
#include "TileGeometry.h"
#include "render/vulkan_impl/VulkanCore.h"
#include "render/vulkan_impl/VulkanPipeline.h"
#include "util/Logger.h"
#include <cmath>
#include <map>
#include <tuple>
#include <unordered_map>
#include <cstdio>

// Geometry cache (persists across builds)
struct CachedGeo { std::vector<float> interleaved; std::vector<unsigned> indices; unsigned idxCount = 0; };
static std::unordered_map<std::string, CachedGeo> s_geoCache;

// ---- TileMesh ----

TileMesh::~TileMesh() { destroy(); }

TileMesh::TileMesh(TileMesh&& o) noexcept
    : m_shapes(std::move(o.m_shapes)), m_iconGroups(std::move(o.m_iconGroups)),
      m_visCaches(std::move(o.m_visCaches)), m_iconVisCaches(std::move(o.m_iconVisCaches)) {}

TileMesh& TileMesh::operator=(TileMesh&& o) noexcept {
    if (this != &o) {
        destroy();
        m_shapes = std::move(o.m_shapes); m_iconGroups = std::move(o.m_iconGroups);
        m_visCaches = std::move(o.m_visCaches); m_iconVisCaches = std::move(o.m_iconVisCaches);
    }
    return *this;
}

void TileMesh::destroy() {
    auto allocator = VulkanCore::instance().allocator();
    for (auto& s : m_shapes) {
        if (s.instPosBuffer)   vmaDestroyBuffer(allocator, s.instPosBuffer, s.instPosAlloc);
        if (s.instColorBuffer) vmaDestroyBuffer(allocator, s.instColorBuffer, s.instColorAlloc);
        if (s.indexBuffer)     vmaDestroyBuffer(allocator, s.indexBuffer, s.indexAlloc);
        if (s.vertexBuffer)    vmaDestroyBuffer(allocator, s.vertexBuffer, s.vertexAlloc);
    }
    m_shapes.clear();
    for (auto& s : m_iconGroups) {
        if (s.instPosBuffer)   vmaDestroyBuffer(allocator, s.instPosBuffer, s.instPosAlloc);
        if (s.instColorBuffer) vmaDestroyBuffer(allocator, s.instColorBuffer, s.instColorAlloc);
        if (s.indexBuffer)     vmaDestroyBuffer(allocator, s.indexBuffer, s.indexAlloc);
        if (s.vertexBuffer)    vmaDestroyBuffer(allocator, s.vertexBuffer, s.vertexAlloc);
    }
    m_iconGroups.clear();
}

bool TileMesh::empty() const { return m_shapes.empty(); }

// Create a GPU buffer + its staging buffer. Returns staging buffer that must be freed after copy.
struct PendingUpload {
    VkBuffer stagingBuf;
    VmaAllocation stagingAlloc;
    VkBuffer dstBuf;
    VkDeviceSize size;
};

static void createWithStaging(VkDeviceSize size, const void* data,
                               VkBufferUsageFlags usage,
                               VkBuffer& dstBuf, VmaAllocation& dstAlloc,
                               PendingUpload& upload) {
    auto allocator = VulkanCore::instance().allocator();

    // Destination (GPU-only)
    {
        VkBufferCreateInfo ci = {};
        ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        ci.size = size;
        ci.usage = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        VmaAllocationCreateInfo aci = {};
        aci.usage = VMA_MEMORY_USAGE_GPU_ONLY;
        vmaCreateBuffer(allocator, &ci, &aci, &dstBuf, &dstAlloc, nullptr);
    }

    // Staging (CPU-visible)
    {
        VkBufferCreateInfo ci = {};
        ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        ci.size = size;
        ci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        VmaAllocationCreateInfo aci = {};
        aci.usage = VMA_MEMORY_USAGE_CPU_ONLY;
        vmaCreateBuffer(allocator, &ci, &aci, &upload.stagingBuf, &upload.stagingAlloc, nullptr);

        void* mapped;
        vmaMapMemory(allocator, upload.stagingAlloc, &mapped);
        memcpy(mapped, data, (size_t)size);
        vmaUnmapMemory(allocator, upload.stagingAlloc);
    }

    upload.dstBuf = dstBuf;
    upload.size = size;
}

static void* createDynamic(VkDeviceSize size, const void* data,
                           VkBufferUsageFlags usage,
                           VkBuffer& dstBuf, VmaAllocation& dstAlloc) {
    auto allocator = VulkanCore::instance().allocator();
    VkBufferCreateInfo ci = {};
    ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    ci.size = size;
    ci.usage = usage;
    VmaAllocationCreateInfo aci = {};
    aci.usage = VMA_MEMORY_USAGE_AUTO;
    aci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                VMA_ALLOCATION_CREATE_MAPPED_BIT;
    VmaAllocationInfo allocInfo;
    vmaCreateBuffer(allocator, &ci, &aci, &dstBuf, &dstAlloc, &allocInfo);
    if (data && allocInfo.pMappedData)
        memcpy(allocInfo.pMappedData, data, (size_t)size);
    return allocInfo.pMappedData;
}

// Batch upload context: one command buffer records all copies, one submit at end
struct UploadBatch {
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    std::vector<std::pair<VkBuffer, VmaAllocation>> stagingToFree;
    bool active = false;
};

static UploadBatch* s_batch = nullptr;

static void beginUploadBatch(UploadBatch& batch) {
    auto& core = VulkanCore::instance();
    VkCommandBufferAllocateInfo ai = {};
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = core.graphicsCommandPool();
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    vkAllocateCommandBuffers(core.device(), &ai, &batch.cmd);

    VkCommandBufferBeginInfo bi = {};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(batch.cmd, &bi);
    batch.active = true;
}

static void endUploadBatch(UploadBatch& batch) {
    auto& core = VulkanCore::instance();
    if (!batch.active) return;

    vkEndCommandBuffer(batch.cmd);
    VkSubmitInfo si = {};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &batch.cmd;
    vkQueueSubmit(core.graphicsQueue(), 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(core.graphicsQueue());

    vkFreeCommandBuffers(core.device(), core.graphicsCommandPool(), 1, &batch.cmd);
    batch.cmd = VK_NULL_HANDLE;

    // Free all staging buffers
    for (auto& [buf, alloc] : batch.stagingToFree) {
        vmaDestroyBuffer(core.allocator(), buf, alloc);
    }
    batch.stagingToFree.clear();
    batch.active = false;
}

static void stageAndCopy(UploadBatch& batch, VkBuffer dst, VkDeviceSize size, const void* data) {
    auto& core = VulkanCore::instance();
    auto allocator = core.allocator();

    VkBuffer staging;
    VmaAllocation sAlloc;
    VkBufferCreateInfo sCI = {};
    sCI.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    sCI.size = size;
    sCI.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    VmaAllocationCreateInfo sACI = {};
    sACI.usage = VMA_MEMORY_USAGE_CPU_ONLY;
    vmaCreateBuffer(allocator, &sCI, &sACI, &staging, &sAlloc, nullptr);

    void* m;
    vmaMapMemory(allocator, sAlloc, &m);
    memcpy(m, data, (size_t)size);
    vmaUnmapMemory(allocator, sAlloc);

    VkBufferCopy region = {0, 0, size};
    vkCmdCopyBuffer(batch.cmd, staging, dst, 1, &region);

    batch.stagingToFree.push_back({staging, sAlloc});
}

void TileMesh::uploadShapeGroupBuffers(ShapeGroup& sg,
                                        const std::vector<float>& interleavedVerts,
                                        const std::vector<unsigned>& idxCopy,
                                        const std::vector<float>& ipos,
                                        const std::vector<float>& icols) {
    auto allocator = VulkanCore::instance().allocator();

    // Vertex buffer
    {
        VkBufferCreateInfo ci = {};
        ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        ci.size = interleavedVerts.size() * sizeof(float);
        ci.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        VmaAllocationCreateInfo aci = {};
        aci.usage = VMA_MEMORY_USAGE_GPU_ONLY;
        vmaCreateBuffer(allocator, &ci, &aci, &sg.vertexBuffer, &sg.vertexAlloc, nullptr);
        if (s_batch) stageAndCopy(*s_batch, sg.vertexBuffer, ci.size, interleavedVerts.data());
    }

    // Index buffer
    {
        VkBufferCreateInfo ci = {};
        ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        ci.size = idxCopy.size() * sizeof(unsigned);
        ci.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        VmaAllocationCreateInfo aci = {};
        aci.usage = VMA_MEMORY_USAGE_GPU_ONLY;
        vmaCreateBuffer(allocator, &ci, &aci, &sg.indexBuffer, &sg.indexAlloc, nullptr);
        if (s_batch) stageAndCopy(*s_batch, sg.indexBuffer, ci.size, idxCopy.data());
    }

    // Instance position buffer (dynamic, per-frame)
    sg.instPosMapped = createDynamic(ipos.size() * sizeof(float), ipos.data(),
                  VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                  sg.instPosBuffer, sg.instPosAlloc);

    // Instance color buffer
    {
        VkBufferCreateInfo ci = {};
        ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        ci.size = icols.size() * sizeof(float);
        ci.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        VmaAllocationCreateInfo aci = {};
        aci.usage = VMA_MEMORY_USAGE_GPU_ONLY;
        vmaCreateBuffer(allocator, &ci, &aci, &sg.instColorBuffer, &sg.instColorAlloc, nullptr);
        if (s_batch) stageAndCopy(*s_batch, sg.instColorBuffer, ci.size, icols.data());
    }
}

void TileMesh::build(const LevelData& level, const std::string& fillColorHex, const std::string& strokeColorHex) {
    destroy();
    const auto& tiles = level.tiles;
    if (tiles.size() < 2) return;

    int n = (int)tiles.size() - 1;
    LOG_I("TileMesh: building %d tiles...", n);

    auto hexToColor = [](const std::string& hex) -> std::tuple<float, float, float> {
        unsigned v = hexToUInt(hex);
        return {((v >> 16) & 0xFF) / 255.0f, ((v >> 8) & 0xFF) / 255.0f, (v & 0xFF) / 255.0f};
    };
    auto [fillR, fillG, fillB] = hexToColor(fillColorHex);
    auto [outR, outG, outB] = hexToColor(strokeColorHex);

    std::map<std::tuple<int, int, bool>, std::vector<int>> shapeGroups;
    for (int i = 0; i < n; i++) {
        float startAngle = (i == 0) ? -180.0f : tiles[i - 1].direction - 180.0f;
        float endAngle = tiles[i].direction;
        bool midspin = (i < (int)level.angleData.size() && level.angleData[i] == 999.0);
        auto key = std::make_tuple((int)std::round(startAngle * 100), (int)std::round(endAngle * 100), midspin);
        shapeGroups[key].push_back(i);
    }

    LOG_I("TileMesh: %zu shape groups", shapeGroups.size());

    Scratch& sc = g_sc;
    m_shapes.resize(shapeGroups.size());
    size_t shapeIdx = 0;

    // Batch all GPU uploads into one command buffer
    UploadBatch batch;
    beginUploadBatch(batch);
    s_batch = &batch;

    for (auto& [key, tileIndices] : shapeGroups) {
        std::sort(tileIndices.begin(), tileIndices.end(), std::greater<int>());
        auto [sa, ea, mid] = key;
        float startAngle = sa / 100.0f, endAngle = ea / 100.0f;

        char kbuf[64];
        snprintf(kbuf, sizeof(kbuf), "tile_%d_%d_%d", sa, ea, (int)mid);
        std::string cacheKey(kbuf);

        std::vector<float> interleaved;
        std::vector<unsigned> idxCopy;
        unsigned idxCount;

        auto cit = s_geoCache.find(cacheKey);
        if (cit != s_geoCache.end()) {
            interleaved = cit->second.interleaved;
            idxCopy = cit->second.indices;
            idxCount = cit->second.idxCount;
        } else {
            sc.clear();
            if (mid) createMidSpinMesh(endAngle, sc);
            else     createTileMesh(startAngle, endAngle, sc);

            for (size_t vi = 0; vi < sc.types.size(); vi++)
                if (sc.types[vi] == 0.0f) sc.verts[vi * 3 + 2] += 0.001f;

            size_t vc = sc.verts.size() / 3;
            interleaved.reserve(vc * 4);
            for (size_t vi = 0; vi < vc; vi++) {
                interleaved.push_back(sc.verts[vi * 3]);
                interleaved.push_back(sc.verts[vi * 3 + 1]);
                interleaved.push_back(sc.verts[vi * 3 + 2]);
                interleaved.push_back(sc.types[vi]);
            }
            idxCopy.assign(sc.indices.begin(), sc.indices.end());
            idxCount = (unsigned)sc.indices.size();
            s_geoCache[cacheKey] = {interleaved, idxCopy, idxCount};
        }

        std::vector<float> ipos;
        std::vector<float> icols;
        std::vector<TileInstance> instances;
        ipos.reserve(tileIndices.size() * 3);
        icols.reserve(tileIndices.size() * 7);
        instances.reserve(tileIndices.size());

        for (int i : tileIndices) {
            double wx = tiles[i].position[0], wy = tiles[i].position[1];
            float fr = fillR, fg = fillG, fb = fillB;
            float sr = outR, sg = outG, sb = outB;
            if (i < (int)level.tileFillColors.size() && !level.tileFillColors[i].empty()) {
                unsigned fv = hexToUInt(level.tileFillColors[i]);
                fr = ((fv >> 16) & 0xFF) / 255.0f; fg = ((fv >> 8) & 0xFF) / 255.0f; fb = (fv & 0xFF) / 255.0f;
            }
            if (i < (int)level.tileStrokeColors.size() && !level.tileStrokeColors[i].empty()) {
                unsigned sv = hexToUInt(level.tileStrokeColors[i]);
                sr = ((sv >> 16) & 0xFF) / 255.0f; sg = ((sv >> 8) & 0xFF) / 255.0f; sb = (sv & 0xFF) / 255.0f;
            }
            ipos.push_back((float)wx); ipos.push_back((float)wy); ipos.push_back(0.0f);
            icols.insert(icols.end(), {fr, fg, fb, sr, sg, sb, 1.0f});

            double minX = 1e99, minY = 1e99, maxX = -1e99, maxY = -1e99;
            size_t vertCount = interleaved.size() / 4;
            for (size_t vi = 0; vi < vertCount; vi++) {
                double lx = (double)interleaved[vi * 4] + wx;
                double ly = (double)interleaved[vi * 4 + 1] + wy;
                if (lx < minX) minX = lx; if (lx > maxX) maxX = lx;
                if (ly < minY) minY = ly; if (ly > maxY) maxY = ly;
            }
            instances.push_back({wx, wy, 0.0f, fr, fg, fb, sr, sg, sb, 1.0f, minX, minY, maxX, maxY});
        }

        ShapeGroup& sg = m_shapes[shapeIdx];
        sg.indexCount = idxCount;
        sg.instanceCount = (uint32_t)tileIndices.size();
        sg.instances = std::move(instances);

        uploadShapeGroupBuffers(sg, interleaved, idxCopy, ipos, icols);
        shapeIdx++;
    }

    // Submit all queued uploads in one batch
    endUploadBatch(batch);
    s_batch = nullptr;

    LOG_I("Built track: %d tiles → %zu shape groups", n, m_shapes.size());
    m_visCaches.resize(m_shapes.size());
    buildIcons(level);
}

bool TileMesh::frustumChanged(const VisibilityCache& cache, float vl, float vr, float vb, float vt) {
    if (!cache.valid) return true;
    return std::abs((float)cache.vl - vl) > 0.5f || std::abs((float)cache.vr - vr) > 0.5f
        || std::abs((float)cache.vb - vb) > 0.5f || std::abs((float)cache.vt - vt) > 0.5f;
}

void TileMesh::shapeDrawCommands(VkCommandBuffer cmd, const VulkanPipeline& pipelines,
                                   const std::vector<ShapeGroup>& groups,
                                   std::vector<VisibilityCache>& caches,
                                   float viewL, float viewR, float viewB, float viewT,
                                   double camX, double camY, bool isIcon) const {
    double margin = 20.0;
    double vl = viewL - margin, vr = viewR + margin;
    double vb = viewB - margin, vt = viewT + margin;

    auto pipeType = isIcon ? VulkanPipeline::Icon : VulkanPipeline::Tile;
    auto layout = isIcon ? pipelines.tileLayout() : pipelines.tileLayout();

    for (size_t si = 0; si < groups.size(); si++) {
        const auto& sg = groups[si];
        auto& cache = caches[si];

        if (!cache.valid || frustumChanged(cache, (float)vl, (float)vr, (float)vb, (float)vt)) {
            cache.indices.clear();
            cache.indices.reserve(sg.instances.size());
            for (int ii = (int)sg.instances.size() - 1; ii >= 0; ii--) {
                const auto& inst = sg.instances[ii];
                if (inst.maxX < vl || inst.minX > vr || inst.maxY < vb || inst.minY > vt)
                    continue;
                cache.indices.push_back((uint32_t)ii);
            }
            cache.vl = vl; cache.vr = vr; cache.vb = vb; cache.vt = vt;
            cache.valid = true;
        }

        if (cache.indices.empty()) continue;

        cache.offsets.resize(cache.indices.size() * 3);
        for (size_t i = 0; i < cache.indices.size(); i++) {
            const auto& inst = sg.instances[cache.indices[i]];
            cache.offsets[i * 3]     = (float)(inst.offX - camX);
            cache.offsets[i * 3 + 1] = (float)(inst.offY - camY);
            cache.offsets[i * 3 + 2] = inst.offZ;
        }

        if (sg.instPosMapped) {
            memcpy(sg.instPosMapped, cache.offsets.data(),
                   cache.offsets.size() * sizeof(float));
        }

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.pipeline(pipeType));

        VkDeviceSize vbsOffsets[] = {0, 0, 0};
        vkCmdBindVertexBuffers(cmd, 0, 1, &sg.vertexBuffer, &vbsOffsets[0]);
        vkCmdBindVertexBuffers(cmd, 1, 1, &sg.instPosBuffer, &vbsOffsets[1]);
        vkCmdBindVertexBuffers(cmd, 2, 1, &sg.instColorBuffer, &vbsOffsets[2]);
        vkCmdBindIndexBuffer(cmd, sg.indexBuffer, 0, VK_INDEX_TYPE_UINT32);

        vkCmdDrawIndexed(cmd, sg.indexCount, (uint32_t)cache.indices.size(), 0, 0, 0);
    }
}

void TileMesh::recordTileDrawCommands(VkCommandBuffer cmd, const VulkanPipeline& pipelines,
                                       float viewL, float viewR, float viewB, float viewT,
                                       double camX, double camY) const {
    shapeDrawCommands(cmd, pipelines, m_shapes, m_visCaches,
                      viewL, viewR, viewB, viewT, camX, camY, false);
}

void TileMesh::recordIconDrawCommands(VkCommandBuffer cmd, const VulkanPipeline& pipelines,
                                       float viewL, float viewR, float viewB, float viewT,
                                       double camX, double camY) const {
    shapeDrawCommands(cmd, pipelines, m_iconGroups, m_iconVisCaches,
                      viewL, viewR, viewB, viewT, camX, camY, true);
}

// ---- Event icons ----

static constexpr float ICON_RADIUS = 0.11f;
static constexpr int   ICON_SEGMENTS = 16;
static constexpr float DECO_Z = 0.01f;
static constexpr float DECO_Z_EXTRA = 0.005f;
static const float TWIRL_COLOR[3]      = {0.502f, 0.0f, 0.502f};
static const float SPEED_UP_COLOR[3]   = {1.0f, 0.0f, 0.0f};
static const float SPEED_DOWN_COLOR[3] = {0.0f, 0.0f, 1.0f};

void TileMesh::buildIcons(const LevelData& level) {
    auto allocator = VulkanCore::instance().allocator();
    for (auto& s : m_iconGroups) {
        if (s.instPosBuffer)   vmaDestroyBuffer(allocator, s.instPosBuffer, s.instPosAlloc);
        if (s.instColorBuffer) vmaDestroyBuffer(allocator, s.instColorBuffer, s.instColorAlloc);
        if (s.indexBuffer)     vmaDestroyBuffer(allocator, s.indexBuffer, s.indexAlloc);
        if (s.vertexBuffer)    vmaDestroyBuffer(allocator, s.vertexBuffer, s.vertexAlloc);
    }
    m_iconGroups.clear();

    const auto& tiles = level.tiles;
    int n = (int)tiles.size() - 1;
    if (n <= 0) return;

    struct IconInst { int tileIdx; float zOff; };
    std::vector<IconInst> colorGroups[3];

    for (int i = 0; i < n; i++) {
        bool hasTwirl = i < (int)level.tileHasTwirl.size() && level.tileHasTwirl[i];
        bool hasSetSpeed = i < (int)level.tileHasSetSpeed.size() && level.tileHasSetSpeed[i];

        float tileZ = 1.0f - (float)i / (float)n * 0.5f;

        if (hasTwirl) colorGroups[0].push_back({i, tileZ + DECO_Z});
        if (hasSetSpeed && i > 0 && i < (int)level.tileBPMs.size()) {
            float ratio = level.tileBPMs[i] / level.tileBPMs[i - 1];
            if (ratio > 1.05f || ratio < 0.95f) {
                int cg = (ratio > 1.05f) ? 1 : 2;
                float extraZ = hasTwirl ? DECO_Z_EXTRA : 0.0f;
                colorGroups[cg].push_back({i, tileZ + DECO_Z + extraZ});
            }
        }
    }

    const float* colors[3] = {TWIRL_COLOR, SPEED_UP_COLOR, SPEED_DOWN_COLOR};
    Scratch& sc = g_sc;

    UploadBatch iconBatch;
    beginUploadBatch(iconBatch);
    s_batch = &iconBatch;

    for (int cg = 0; cg < 3; cg++) {
        if (colorGroups[cg].empty()) continue;

        auto& grp = colorGroups[cg];
        float cr = colors[cg][0], cgv = colors[cg][1], cb = colors[cg][2];

        sc.clear();
        createCircle(0.0f, 0.0f, ICON_RADIUS, 1.0f, sc, ICON_SEGMENTS);

        size_t vc = sc.verts.size() / 3;
        std::vector<float> interleaved;
        interleaved.reserve(vc * 4);
        for (size_t vi = 0; vi < vc; vi++) {
            interleaved.push_back(sc.verts[vi * 3]);
            interleaved.push_back(sc.verts[vi * 3 + 1]);
            interleaved.push_back(sc.verts[vi * 3 + 2]);
            interleaved.push_back(sc.types[vi]);
        }

        std::vector<float> ipos;
        std::vector<float> icols;
        std::vector<TileInstance> instances;
        ipos.reserve(grp.size() * 3);
        icols.reserve(grp.size() * 7);
        instances.reserve(grp.size());

        for (auto& icon : grp) {
            double wx = tiles[icon.tileIdx].position[0];
            double wy = tiles[icon.tileIdx].position[1];
            float wz = icon.zOff;
            ipos.push_back((float)wx); ipos.push_back((float)wy); ipos.push_back(wz);
            icols.insert(icols.end(), {cr, cgv, cb, cr, cgv, cb, 1.0f});
            instances.push_back({wx, wy, wz, cr, cgv, cb, cr, cgv, cb, 1.0f,
                wx - ICON_RADIUS, wy - ICON_RADIUS, wx + ICON_RADIUS, wy + ICON_RADIUS});
        }

        ShapeGroup sg;
        sg.indexCount = (uint32_t)sc.indices.size();
        sg.instanceCount = (uint32_t)grp.size();
        sg.instances = std::move(instances);

        uploadShapeGroupBuffers(sg, interleaved,
                                 std::vector<unsigned>(sc.indices.begin(), sc.indices.end()),
                                 ipos, icols);
        m_iconGroups.push_back(std::move(sg));
    }

    endUploadBatch(iconBatch);
    s_batch = nullptr;

    m_iconVisCaches.resize(m_iconGroups.size());
    LOG_I("Built event icons: %zu icon groups", m_iconGroups.size());
}

unsigned int TileMesh::hexToUInt(const std::string& hex) {
    unsigned v = 0;
    for (char c : hex) {
        v <<= 4;
        if (c >= '0' && c <= '9') v |= c - '0';
        else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
        else break;
    }
    return v;
}
