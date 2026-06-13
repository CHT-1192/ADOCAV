#include "LoadingWindow.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"

#define IMGUI_IMPL_OPENGL_LOADER_GLFW
#include "imgui_impl_opengl3.h"

#include <GLFW/glfw3.h>
#include <thread>
#include <cstdio>

static constexpr int LOADING_W = 480;
static constexpr int LOADING_H = 160;

void showLoadingWindow(std::function<void(LoadingProgress&)> loader) {
    float dpiScale = 1.0f;
    {
        GLFWmonitor* mon = glfwGetPrimaryMonitor();
        if (mon) {
            float sx, sy;
            glfwGetMonitorContentScale(mon, &sx, &sy);
            dpiScale = std::max(sx, sy);
        }
    }
    int winW = (int)(LOADING_W * dpiScale);
    int winH = (int)(LOADING_H * dpiScale);

    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    glfwWindowHint(GLFW_DECORATED, GLFW_TRUE);
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);

    GLFWwindow* window = glfwCreateWindow(winW, winH, "ADOCAV - Loading", nullptr, nullptr);
    if (!window) {
        fprintf(stderr, "Failed to create loading window\n");
        return;
    }

    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    if (monitor) {
        const GLFWvidmode* mode = glfwGetVideoMode(monitor);
        if (mode) {
            glfwSetWindowPos(window,
                (mode->width - winW) / 2,
                (mode->height - winH) / 2);
        }
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui::GetStyle().ScaleAllSizes(dpiScale);
    float fontSize = 16.0f * dpiScale;

    {
        ImGuiIO& io = ImGui::GetIO();
        const char* latinFonts[] = {
            "C:/Windows/Fonts/segoeui.ttf", "C:/Windows/Fonts/arial.ttf",
        };
        bool latinLoaded = false;
        for (const char* path : latinFonts) {
            FILE* f = fopen(path, "rb");
            if (f) { fclose(f); io.Fonts->AddFontFromFileTTF(path, fontSize); latinLoaded = true; break; }
        }
        if (!latinLoaded) io.Fonts->AddFontDefault();
    }

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330 core");

    LoadingProgress progress;
    std::thread loaderThread([&]() {
        loader(progress);
        progress.percent.store(100.0f);
    });

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2((float)winW, (float)winH));
        ImGui::Begin("Loading", nullptr,
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar);

        float S = dpiScale;
        ImGui::Spacing();
        ImGui::Text("Loading...");
        ImGui::Separator();
        ImGui::Spacing();

        float pct = progress.percent.load();
        ImGui::SetNextItemWidth(440 * S);
        ImGui::ProgressBar(pct / 100.0f, ImVec2(0, 24 * S));

        ImGui::Spacing();
        ImGui::Text("%s", progress.stageText);
        ImGui::End();

        ImGui::Render();
        int displayW, displayH;
        glfwGetFramebufferSize(window, &displayW, &displayH);
        glViewport(0, 0, displayW, displayH);
        glClearColor(0.12f, 0.12f, 0.14f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);

        if (pct >= 100.0f && !loaderThread.joinable()) break;
        if (pct >= 100.0f && loaderThread.joinable()) { loaderThread.join(); break; }
    }

    if (loaderThread.joinable()) loaderThread.join();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
}
