#pragma once

#include <string>
#include <functional>
#include <atomic>

struct LoadingProgress {
    std::atomic<float> percent{0.0f};
    std::atomic<int>   stage{0};
    char stageText[256] = "Initializing...";
};

void showLoadingWindow(std::function<void(LoadingProgress&)> loader);
