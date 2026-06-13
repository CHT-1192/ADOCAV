#pragma once

#include <string>

struct LauncherConfig {
    std::string levelPath;
    std::string musicPath;
    std::string trackFillColor   = "DEBB7B";
    std::string trackStrokeColor = "6F5D3D";
    std::string backgroundColor  = "000000";
    bool   autoStroke   = true;
    bool   enableHitsounds = true;
    int  resolutionW = 1280;
    int  resolutionH = 720;
    bool fullscreen = false;
    bool showTrail = true;
    bool exportHitsounds = false;
    bool cancelled = false;
    bool gpuCulling = true;    // use GPU compute culling (default on)
    bool autoPlay = false;     // auto-start playback after loading
};

LauncherConfig showLauncher();
