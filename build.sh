#!/bin/bash
set -e

if [ -z "$VULKAN_SDK" ]; then
    echo "VULKAN_SDK not set. Install Vulkan SDK from https://vulkan.lunarg.com/"
    exit 1
fi

# Compile SPIR-V shaders
echo "=== Compiling SPIR-V shaders ==="
mkdir -p build/shaders
"$VULKAN_SDK/bin/glslc" shaders/tile.vert -o build/shaders/tile.vert.spv
"$VULKAN_SDK/bin/glslc" shaders/tile.frag -o build/shaders/tile.frag.spv
"$VULKAN_SDK/bin/glslc" shaders/planet.vert -o build/shaders/planet.vert.spv
"$VULKAN_SDK/bin/glslc" shaders/planet.frag -o build/shaders/planet.frag.spv
"$VULKAN_SDK/bin/glslc" shaders/trail.vert -o build/shaders/trail.vert.spv
"$VULKAN_SDK/bin/glslc" shaders/trail.frag -o build/shaders/trail.frag.spv
"$VULKAN_SDK/bin/glslc" shaders/icon.vert -o build/shaders/icon.vert.spv
"$VULKAN_SDK/bin/glslc" shaders/icon.frag -o build/shaders/icon.frag.spv
"$VULKAN_SDK/bin/glslc" shaders/tile_cull.comp -o build/shaders/tile_cull.comp.spv
"$VULKAN_SDK/bin/glslc" shaders/tile_offset.comp -o build/shaders/tile_offset.comp.spv

# CMake build
mkdir -p build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --parallel
echo "Build done: $(pwd)/ADOCAV"
