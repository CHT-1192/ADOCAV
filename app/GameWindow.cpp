#include "GameWindow.h"

#define VK_NO_PROTOTYPES
#include <volk.h>
#include <vk_mem_alloc.h>

#include "render/vulkan_impl/VulkanCore.h"
#include "render/vulkan_impl/VulkanSwapchain.h"
#include "render/vulkan_impl/FrameData.h"
#include "render/vulkan_impl/VulkanRenderPass.h"
#include "render/vulkan_impl/VulkanPipeline.h"
#include "render/vulkan_impl/VulkanPipelineCache.h"
#include "render/vulkan_impl/VulkanBuffer.h"
#include "camera/Camera.h"
#include "track/TileMesh.h"
#include "game/Planet.h"
#include "render/PlanetTrail.h"
#include "util/Logger.h"

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cstdio>
#include <chrono>
#include <thread>

namespace {

struct GameInput {
    bool   dragActive = false;
    double dragStartX = 0.0;
    double dragStartY = 0.0;
    double cursorX    = 0.0;
    double cursorY    = 0.0;
    double baseTargetX = 0.0;
    double baseTargetY = 0.0;
    double offsetX = 0.0;
    double offsetY = 0.0;
    Camera* camera = nullptr;
};

struct Viewport { int x=0, y=0, w=0, h=0; };

Viewport computeLetterbox(int fbW, int fbH, float targetAspect) {
    float fbAspect = (float)fbW / (float)fbH;
    Viewport vp;
    if (targetAspect > fbAspect) {
        vp.w = fbW;
        vp.h = (int)(fbW / targetAspect);
        vp.x = 0;
        vp.y = (fbH - vp.h) / 2;
    } else {
        vp.h = fbH;
        vp.w = (int)(fbH * targetAspect);
        vp.x = (fbW - vp.w) / 2;
        vp.y = 0;
    }
    return vp;
}

} // namespace

void showGameWindow(const LauncherConfig& cfg, LoadResult& result) {
    auto& level = result.level;
    auto& playback = result.playback;
    auto& hitsoundMgr = result.hitsounds;
    auto& audioEngine = result.audio;

    LOG_I("GameWindow: starting...");
    float targetAspect = (float)cfg.resolutionW / (float)cfg.resolutionH;

    // ---- Create GLFW window (Vulkan, no OpenGL context) ----
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    GLFWwindow* window;
    if (cfg.fullscreen) {
        GLFWmonitor* targetMonitor = glfwGetPrimaryMonitor();
        const GLFWvidmode* mode = glfwGetVideoMode(targetMonitor);
        window = glfwCreateWindow(mode->width, mode->height, "ADOCAV", targetMonitor, nullptr);
    } else {
        window = glfwCreateWindow(cfg.resolutionW, cfg.resolutionH, "ADOCAV", nullptr, nullptr);
        GLFWmonitor* monitor = glfwGetPrimaryMonitor();
        if (monitor) {
            const GLFWvidmode* mode = glfwGetVideoMode(monitor);
            if (mode) {
                glfwSetWindowPos(window,
                    (mode->width  - cfg.resolutionW) / 2,
                    (mode->height - cfg.resolutionH) / 2);
            }
        }
    }

    if (!window) { LOG_E("Failed to create game window"); return; }
    LOG_I("GameWindow: window created, initializing Vulkan...");

    // ---- Initialize Vulkan instance first (needed before surface creation) ----
    auto& core = VulkanCore::instance();
    bool enableValidation = true;
#ifdef NDEBUG
    enableValidation = false;
#endif
    if (!core.initInstance(enableValidation)) {
        LOG_E("Failed to initialize Vulkan instance");
        glfwDestroyWindow(window);
        return;
    }

    // ---- Create Vulkan surface from GLFW window (needs VkInstance) ----
    VkSurfaceKHR surface;
    if (glfwCreateWindowSurface(core.vkInstance(), window, nullptr, &surface) != VK_SUCCESS) {
        LOG_E("Failed to create window surface");
        core.shutdown();
        glfwDestroyWindow(window);
        return;
    }

    // ---- Initialize Vulkan device + VMA (needs surface for queue selection) ----
    if (!core.initDevice(surface)) {
        LOG_E("Failed to initialize Vulkan device");
        glfwDestroyWindow(window);
        return;
    }

    // ---- Swapchain ----
    VulkanSwapchain swapchain;
    int fbW, fbH;
    glfwGetFramebufferSize(window, &fbW, &fbH);
    if (!swapchain.create(fbW, fbH)) {
        LOG_E("Failed to create swapchain");
        core.shutdown();
        glfwDestroyWindow(window);
        return;
    }

    // ---- Render pass ----
    VulkanRenderPass renderPass;
    renderPass.create(swapchain.imageFormat(), swapchain.depthFormat());

    // Create depth resources + framebuffers
    auto& sc = swapchain;
    // Depth is handled by VulkanSwapchain
    std::vector<VkImageView> imageViews;
    for (uint32_t i = 0; i < sc.imageCount(); i++)
        imageViews.push_back(sc.imageView(i));
    renderPass.recreateFramebuffers(sc.extent(), imageViews, sc.depthView());

    // ---- Pipeline cache ----
    VulkanPipelineCache pipelineCache;
    pipelineCache.load("assets/pipeline_cache.bin");

    // ---- Pipelines ----
    VulkanPipeline pipelines;
    if (!pipelines.createAll(renderPass.renderPass(), sc.extent())) {
        LOG_E("Failed to create pipelines");
        core.shutdown();
        glfwDestroyWindow(window);
        return;
    }

    // Save pipeline cache for next run
    pipelineCache.save("assets/pipeline_cache.bin");

    // ---- Frame data (2 frames in flight) ----
    FrameData frameData;
    frameData.create();

    // ---- Shaders loaded, now build scene ----
    // Track
    TileMesh tileMesh;
    LOG_I("GameWindow: building tile mesh...");
    tileMesh.build(*level, cfg.trackFillColor, cfg.trackStrokeColor);
    LOG_I("GameWindow: tile mesh built");

    // Create compute descriptor set for GPU culling
    VkDescriptorSet computeDS = VK_NULL_HANDLE;
    if (cfg.gpuCulling && tileMesh.totalTileCount() > 0) {
        computeDS = pipelines.createComputeDescriptorSet(
            tileMesh.debugTileBoundsBuf(),
            tileMesh.debugTilePositionsBuf(),
            tileMesh.debugVisibleFlagsBuf(),
            tileMesh.debugInstanceOffsetsBuf(),
            tileMesh.debugIndirectBuf()
        );
        LOG_I("GameWindow: GPU culling enabled (%u tiles)", tileMesh.totalTileCount());
    } else {
        LOG_I("GameWindow: CPU culling (%u tiles)", tileMesh.totalTileCount());
    }

    // Camera
    Camera camera;
    GameInput input; input.camera = &camera;
    float bgR=0, bgG=0, bgB=0;
    {
        unsigned r,g,b; sscanf(cfg.backgroundColor.c_str(),"%02x%02x%02x",&r,&g,&b);
        bgR=r/255.0f;bgG=g/255.0f;bgB=b/255.0f;
    }
    camera.setZoom(level->settings.zoom);
    if (!level->tiles.empty()) {
        auto& t = level->tiles[0];
        camera.setTarget(t.position[0], t.position[1]);
        input.baseTargetX = t.position[0];
        input.baseTargetY = t.position[1];
    }

    // Planets
    if (playback.redPlanet()) {
        playback.redPlanet()->buildGPU();
        playback.bluePlanet()->buildGPU();
    }

    // Audio: attach hitsound buffer
    if (hitsoundMgr.isSynthesized()) {
        audioEngine.attachExternal(hitsoundMgr.buffer(), hitsoundMgr.totalFrames(),
                                   hitsoundMgr.channels(), hitsoundMgr.sampleRate(),
                                   hitsoundMgr.cursor(), hitsoundMgr.playing());
    }

    // ---- Input callbacks ----
    glfwSetMouseButtonCallback(window, [](GLFWwindow* w, int button, int action, int) {
        auto* in = static_cast<GameInput*>(glfwGetWindowUserPointer(w));
        if (button == GLFW_MOUSE_BUTTON_LEFT) {
            if (action == GLFW_PRESS) {
                in->dragActive=true; in->dragStartX=in->cursorX; in->dragStartY=in->cursorY;
            } else {
                in->dragActive=false; in->baseTargetX+=in->offsetX; in->baseTargetY+=in->offsetY;
                in->offsetX=0; in->offsetY=0;
            }
        }
    });
    glfwSetCursorPosCallback(window, [](GLFWwindow* w, double x, double y) {
        auto* in = static_cast<GameInput*>(glfwGetWindowUserPointer(w));
        in->cursorX=x; in->cursorY=y;
    });
    glfwSetScrollCallback(window, [](GLFWwindow* w, double, double dy) {
        auto* in = static_cast<GameInput*>(glfwGetWindowUserPointer(w));
        float z = in->camera->zoom() * (1.0f + (float)dy * 0.1f);
        if (z<5)z=5; if (z>1000)z=1000;
        in->camera->setZoom(z);
    });
    glfwSetWindowUserPointer(window, &input);

    // ---- Main rendering loop ----
    double lastFrameTime = glfwGetTime();
    constexpr double targetFrameTime = 1.0 / 320.0;  // 320 FPS soft cap
    bool wasSpacePressed = false;
    double autoPlayTriggerTime = glfwGetTime() + (cfg.autoPlay ? 0.5 : 999999.0);

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
            glfwSetWindowShouldClose(window, GLFW_TRUE);

        // Delta time with sleep-based frame pacing (not busy-wait)
        double now = glfwGetTime();
        double elapsed = now - lastFrameTime;
        if (elapsed < targetFrameTime && elapsed > 0) {
            double remaining = targetFrameTime - elapsed;
            if (remaining > 0.002) {
                std::this_thread::sleep_for(
                    std::chrono::duration<double>(remaining - 0.001));
            }
            while ((now = glfwGetTime()) < lastFrameTime + targetFrameTime) {
                // Spin last ~1ms for precision
            }
            elapsed = targetFrameTime;
        }
        float deltaMs = (float)(elapsed * 1000.0);
        lastFrameTime = now;
        if (deltaMs > 500.0f) deltaMs = 0.0f;
        else if (deltaMs > 100.0f) deltaMs = 100.0f;

        // Space toggles playback (or auto-play trigger)
        bool spacePressed = (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS)
                         || (cfg.autoPlay && now >= autoPlayTriggerTime && !playback.isPlaying());
        if (spacePressed && !wasSpacePressed) {
            autoPlayTriggerTime = 999999.0;  // only fire once
            if (!playback.isPlaying()) {
                playback.start(glfwGetTime());
                float bpm = playback.tileBPMPerTile().size() > 0
                    ? playback.tileBPMPerTile()[0] : level->settings.bpm;
                float offsetSec = level->settings.offset / 1000.0f;
                hitsoundMgr.reset();
                if (audioEngine.hasMusic()) {
                    audioEngine.seek(offsetSec);
                    audioEngine.play();
                } else {
                    audioEngine.play();
                }
            } else {
                playback.stop();
                audioEngine.pause();
            }
        }
        wasSpacePressed = spacePressed;

        // Update playback
        if (playback.isPlaying()) {
            if (audioEngine.hasMusic() && audioEngine.isPlaying()) {
                float offsetSec = level->settings.offset / 1000.0f;
                playback.syncToAudio(audioEngine.position(), offsetSec);
            } else {
                playback.updateWallClock(now);
            }
        }

        // Camera: follow pivot during playback
        if (playback.isPlaying()) {
            int tileIdx = playback.currentTileIndex();
            int pivotIdx = (tileIdx >= 0) ? tileIdx : 0;
            if (pivotIdx < (int)level->tiles.size()) {
                auto& p = level->tiles[pivotIdx].position;
                camera.setTarget(p[0], p[1]);
                input.baseTargetX = p[0];
                input.baseTargetY = p[1];
                input.offsetX = 0;
                input.offsetY = 0;
            }
        }

        // Letterbox
        glfwGetFramebufferSize(window, &fbW, &fbH);
        int winW, winH;
        glfwGetWindowSize(window, &winW, &winH);
        Viewport vp = computeLetterbox(fbW, fbH, targetAspect);

        // Drag (only when not playing)
        if (!playback.isPlaying() && input.dragActive && vp.w>0 && vp.h>0) {
            double halfH = 6.0/(camera.zoom()/100.0);
            double halfW = halfH*(double)vp.w/(double)vp.h;
            double pxToWorldX = (2.0*halfW)/(double)vp.w;
            double pxToWorldY = (2.0*halfH)/(double)vp.h;
            input.offsetX = -(input.cursorX-input.dragStartX)*pxToWorldX;
            input.offsetY =  (input.cursorY-input.dragStartY)*pxToWorldY;
        }
        if (!playback.isPlaying()) {
            camera.setTarget(input.baseTargetX+input.offsetX, input.baseTargetY+input.offsetY);
        }

        camera.setAspect((float)vp.w, (float)vp.h);
        float vl,vr,vb,vt;
        camera.frustumBounds(vl,vr,vb,vt);

        // ---- Begin frame ----
        uint32_t imageIndex;
        bool recreated;
        VkSemaphore imageAvailable;
        frameData.beginFrame(imageIndex, recreated, swapchain.swapchain(), imageAvailable);

        if (recreated) {
            // Handle window resize
            glfwGetFramebufferSize(window, &fbW, &fbH);
            if (fbW > 0 && fbH > 0) {
                vkDeviceWaitIdle(core.device());
                swapchain.recreate(fbW, fbH);
                imageViews.clear();
                for (uint32_t i = 0; i < swapchain.imageCount(); i++)
                    imageViews.push_back(swapchain.imageView(i));
                renderPass.recreateFramebuffers(swapchain.extent(), imageViews, swapchain.depthView());
            }
            continue;
        }

        auto& frame = frameData.current();
        VkCommandBuffer cmd = frame.commandBuffer;

        // Begin command buffer
        VkCommandBufferBeginInfo beginI = {};
        beginI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginI.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &beginI);

        // Begin render pass
        VkClearValue clearValues[2] = {};
        clearValues[0].color = {{bgR, bgG, bgB, 1.0f}};
        clearValues[1].depthStencil = {1.0f, 0};

        VkRenderPassBeginInfo rpI = {};
        rpI.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpI.renderPass = renderPass.renderPass();
        rpI.framebuffer = renderPass.framebuffers()[imageIndex];
        rpI.renderArea.extent = swapchain.extent();
        rpI.clearValueCount = (swapchain.depthView() != VK_NULL_HANDLE) ? 2 : 1;
        rpI.pClearValues = clearValues;

        vkCmdBeginRenderPass(cmd, &rpI, VK_SUBPASS_CONTENTS_INLINE);

        // Viewport/scissor MUST be set inside render pass
        VkRect2D scissor = {{vp.x, vp.y}, {(uint32_t)vp.w, (uint32_t)vp.h}};
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        VkViewport viewport = {};
        viewport.x = (float)vp.x;
        viewport.y = (float)(vp.y + vp.h);   // Vulkan Y-down: flip via viewport
        viewport.width = (float)vp.w;
        viewport.height = -(float)vp.h;       // negative height flips Y
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &viewport);

        // ---- Draw tiles (far to near, no depth test) ----
        auto vpMatrix = camera.viewProj();

        if (cfg.gpuCulling && computeDS) {
            // GPU culling: compute shader determines visibility + offsets
            tileMesh.recordCullDispatch(cmd, pipelines, computeDS, vl, vr, vb, vt,
                                         camera.targetX(), camera.targetY());
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.pipeline(VulkanPipeline::Tile));
            vkCmdPushConstants(cmd, pipelines.tileLayout(),
                               VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), glm::value_ptr(vpMatrix));
            tileMesh.recordIndirectDraws(cmd, pipelines);
        } else {
            // CPU culling path (original)
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.pipeline(VulkanPipeline::Tile));
            vkCmdPushConstants(cmd, pipelines.tileLayout(),
                               VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), glm::value_ptr(vpMatrix));
            tileMesh.recordTileDrawCommands(cmd, pipelines, vl, vr, vb, vt,
                                             camera.targetX(), camera.targetY());
        }

        // ---- Draw trails (blend enabled, no depth test) ----
        if (playback.isPlaying() && playback.redPlanet() && playback.redPlanet()->trail) {
            playback.redPlanet()->trail->recordDrawCommands(cmd, pipelines, camera,
                                                             camera.targetX(), camera.targetY());
            playback.bluePlanet()->trail->recordDrawCommands(cmd, pipelines, camera,
                                                              camera.targetX(), camera.targetY());
        }

        // ---- Draw planets (depth test enabled) ----
        if (playback.isPlaying() && playback.redPlanet() && playback.redPlanet()->gpuBuilt()) {
            playback.redPlanet()->recordDrawCommands(cmd, pipelines, camera,
                                                      camera.targetX(), camera.targetY());
            playback.bluePlanet()->recordDrawCommands(cmd, pipelines, camera,
                                                       camera.targetX(), camera.targetY());
        }

        // ---- Draw event icons (no depth test) ----
        // Re-push VP: trail/planet push constants overwrote the tile VP bytes
        vkCmdPushConstants(cmd, pipelines.tileLayout(),
                           VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), glm::value_ptr(vpMatrix));
        tileMesh.recordIconDrawCommands(cmd, pipelines, vl, vr, vb, vt,
                                         camera.targetX(), camera.targetY());

        vkCmdEndRenderPass(cmd);

        if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
            LOG_E("Failed to record command buffer");
        }

        // ---- Submit and present ----
        frameData.submitAndPresent(core.graphicsQueue(), core.presentQueue(),
                                    swapchain.swapchain(), imageIndex);
    }

    // ---- Cleanup ----
    vkDeviceWaitIdle(core.device());
    audioEngine.shutdown();
    tileMesh.~TileMesh();  // explicit destroy before core shutdown
    frameData.destroy();
    pipelines.destroy();
    pipelineCache.destroy();
    renderPass.destroy();
    swapchain.destroy();
    core.shutdown();
    glfwDestroyWindow(window);
}
