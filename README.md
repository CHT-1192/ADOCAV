# ADOCAV — A Dance of C++ and Vulkan

A native high-performance viewer for [A Dance of Fire and Ice](https://store.steampowered.com/app/977950/A_Dance_of_Fire_and_Ice/) custom levels (`.adofai` format), written in C++20 with Vulkan 1.2.

![C++20](https://img.shields.io/badge/C%2B%2B-20-blue)
![Vulkan](https://img.shields.io/badge/Vulkan-1.2-red)
![Platform](https://img.shields.io/badge/Platform-Windows%20%7C%20Linux-lightgrey)

Vulkan port of [ADOCAO](https://github.com/CHT-1192/ADOCAO).

## Features

- Full ADOFAI playback engine with relative angle computation
- GPU instanced tile rendering with frustum culling and visibility cache
- Pre-synthesized hitsound tracks (27 hit types)
- Music playback via miniaudio (WASAPI/PulseAudio)
- Planet movement with trail rendering (Catmull-Rom spline)
- Per-tile ColorTrack support
- `SetSpeed`, `Twirl`, `Pause`, `Midspin`, `PositionTrack` support
- Offline SPIR-V shader compilation
- Pipeline cache serialization for fast startup
- DPI-aware launcher with auto-stroke color, background color, and resolution selection

## Quick Start

### Prerequisites

- CMake 3.20+
- C++20 compiler (GCC/MinGW recommended)
- [Vulkan SDK 1.2+](https://vulkan.lunarg.com/)

### Windows

```bash
build.bat          # dynamic-link build
build.bat portable # static-link portable build
```

### Linux

```bash
chmod +x build.sh
./build.sh
```

## Build Dependencies

All fetched automatically via CMake `FetchContent`:

| Library | Version | Purpose |
|---------|---------|---------|
| GLFW | 3.4 | Window + input |
| glm | 1.0.1 | Math |
| nlohmann/json | 3.11.3 | .adofai parsing |
| Dear ImGui | 1.91.9 | Launcher UI |
| miniaudio | 0.11.22 | Audio playback |
| stb_vorbis | latest | OGG decoding |
| tinyfiledialogs | 2.9.3 | File open dialogs |
| volk | latest | Vulkan loader |
| VMA | 3.4.0 | GPU memory allocation |

## CLI Usage

```
ADOCAV.exe --level <file> --music <file> [--width N] [--height N]
           [--fullscreen] [--fill HEX] [--stroke HEX] [--bg HEX]
           [--no-auto-stroke] [--no-hitsound] [--no-trail] [--debug]
           [--auto-play] [--cpu-culling]
```

Without `--level`, falls through to the ImGui launcher.

## Project Structure

```
app/         Application layer (windows, launcher, game loop)
audio/       Music playback + hitsound synthesis
camera/      Orthographic camera with Vulkan NDC
game/        Planet rendering + playback engine
level/       .adofai parser + JSON cleaner
render/
  vulkan_impl/  Vulkan infrastructure (device, swapchain, pipelines, buffers)
  PlanetTrail.*   Catmull-Rom trail
shaders/     GLSL source (.vert/.frag/.comp) → SPIR-V via glslc
track/       Tile mesh generation + instanced rendering
util/        Logger + easing functions
assets/
  sounds/      27 hit sound .wav files
```

## Controls

| Key | Action |
|-----|--------|
| Space | Start/stop playback |
| Esc | Close game window |
| Mouse drag | Pan camera (when stopped) |
| Scroll | Zoom in/out |

## Key Design Decisions

- **Push constants only** — no descriptor sets. 128 bytes maximum
- **Single merged render pass** — tiles, trails, planets, icons all in one pass
- **2 frames in flight** for minimal latency
- **Offline SPIR-V compilation** via glslc
- **volk** for Vulkan loading, **VMA** for GPU memory allocation
- **Vulkan NDC**: Y-down via negative viewport height

## References

- [ADOFAI-JS](https://github.com/adofaiex/ADOFAI-JS) — Core angle parsing
- [Re_ADOJAS](https://github.com/adofaiex/Re_ADOJAS) — Three.js web player
- [ADOFAN_PIXI](https://github.com/AnStartist/ADOFAN_PIXI) — PixiJS web player

## License

[Apache 2.0](LICENSE)
