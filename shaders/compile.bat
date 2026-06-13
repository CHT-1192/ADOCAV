@echo off
:: Compile all GLSL shaders to SPIR-V using glslc from Vulkan SDK
:: Usage: compile.bat [Vulkan SDK version]
setlocal

if not defined VULKAN_SDK (
    echo VULKAN_SDK not set, searching for Vulkan SDK...
    for /d %%d in ("C:\VulkanSDK\*") do set "VULKAN_SDK=%%d"
)
if not defined VULKAN_SDK (
    echo Vulkan SDK not found. Install from https://vulkan.lunarg.com/
    exit /b 1
)
echo Using Vulkan SDK: %VULKAN_SDK%

set "GLSLC=%VULKAN_SDK%\Bin\glslc.exe"
if not exist "%GLSLC%" (
    echo glslc not found at %GLSLC%
    exit /b 1
)

set "SRC=%~dp0"
set "DEST=%~dp0..\build\shaders"
if not exist "%DEST%" mkdir "%DEST%"

echo === Compiling shaders ===

:: Tile
echo   tile
"%GLSLC%" "%SRC%tile.vert" -o "%DEST%\tile.vert.spv" || exit /b 1
"%GLSLC%" "%SRC%tile.frag" -o "%DEST%\tile.frag.spv" || exit /b 1

:: Planet
echo   planet
"%GLSLC%" "%SRC%planet.vert" -o "%DEST%\planet.vert.spv" || exit /b 1
"%GLSLC%" "%SRC%planet.frag" -o "%DEST%\planet.frag.spv" || exit /b 1

:: Trail
echo   trail
"%GLSLC%" "%SRC%trail.vert" -o "%DEST%\trail.vert.spv" || exit /b 1
"%GLSLC%" "%SRC%trail.frag" -o "%DEST%\trail.frag.spv" || exit /b 1

:: Icon
echo   icon
"%GLSLC%" "%SRC%icon.vert" -o "%DEST%\icon.vert.spv" || exit /b 1
"%GLSLC%" "%SRC%icon.frag" -o "%DEST%\icon.frag.spv" || exit /b 1

:: Compute
echo   tile_cull
"%GLSLC%" "%SRC%tile_cull.comp" -o "%DEST%\tile_cull.comp.spv" || exit /b 1
echo   tile_offset
"%GLSLC%" "%SRC%tile_offset.comp" -o "%DEST%\tile_offset.comp.spv" || exit /b 1

echo === Done: %DEST% ===
endlocal
