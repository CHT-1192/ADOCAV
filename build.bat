@echo off
setlocal
set "PORTABLE=0"
if /I "%~1"=="portable" set "PORTABLE=1"

:: Find MinGW
set "MINGW64=%LocalAppData%\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT.LLVM_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64"
if not exist "%MINGW64%\bin\g++.exe" (
    for /d %%d in ("%LocalAppData%\Microsoft\WinGet\Packages\BrechtSanders.WinLibs*") do set "MINGW64=%%d\mingw64"
)
if not exist "%MINGW64%\bin\g++.exe" (
    echo Cannot find MinGW. Install via: winget install BrechtSanders.WinLibs.POSIX.UCRT.LLVM
    exit /b 1
)
set "PATH=%MINGW64%\bin;%PATH%"
cd /d "%~dp0"

:: Find Vulkan SDK
if not defined VULKAN_SDK (
    for /d %%d in ("C:\VulkanSDK\*") do set "VULKAN_SDK=%%d"
)
if not defined VULKAN_SDK (
    echo VULKAN_SDK not set and Vulkan SDK not found in C:\VulkanSDK
    echo Install from https://vulkan.lunarg.com/
    exit /b 1
)
echo Using Vulkan SDK: %VULKAN_SDK%

:: First compile SPIR-V shaders
echo === Compiling SPIR-V shaders ===
call "%VULKAN_SDK%\Bin\glslc.exe" shaders\tile.vert -o build\shaders\tile.vert.spv || exit /b 1
call "%VULKAN_SDK%\Bin\glslc.exe" shaders\tile.frag -o build\shaders\tile.frag.spv || exit /b 1
call "%VULKAN_SDK%\Bin\glslc.exe" shaders\planet.vert -o build\shaders\planet.vert.spv || exit /b 1
call "%VULKAN_SDK%\Bin\glslc.exe" shaders\planet.frag -o build\shaders\planet.frag.spv || exit /b 1
call "%VULKAN_SDK%\Bin\glslc.exe" shaders\trail.vert -o build\shaders\trail.vert.spv || exit /b 1
call "%VULKAN_SDK%\Bin\glslc.exe" shaders\trail.frag -o build\shaders\trail.frag.spv || exit /b 1
call "%VULKAN_SDK%\Bin\glslc.exe" shaders\icon.vert -o build\shaders\icon.vert.spv || exit /b 1
call "%VULKAN_SDK%\Bin\glslc.exe" shaders\icon.frag -o build\shaders\icon.frag.spv || exit /b 1
call "%VULKAN_SDK%\Bin\glslc.exe" shaders\tile_cull.comp -o build\shaders\tile_cull.comp.spv || exit /b 1
call "%VULKAN_SDK%\Bin\glslc.exe" shaders\tile_offset.comp -o build\shaders\tile_offset.comp.spv || exit /b 1

:: CMake build
if not exist build mkdir build
cd build

if "%PORTABLE%"=="1" (
    echo === Building ADOCAV-Portable (static linked) ===
    cmake .. -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DADOCAV_PORTABLE=ON -DCMAKE_C_COMPILER="%MINGW64%\bin\gcc.exe" -DCMAKE_CXX_COMPILER="%MINGW64%\bin\g++.exe"
) else (
    cmake .. -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DADOCAV_PORTABLE=OFF -DCMAKE_C_COMPILER="%MINGW64%\bin\gcc.exe" -DCMAKE_CXX_COMPILER="%MINGW64%\bin\g++.exe"
)

cmake --build . --parallel
if %ERRORLEVEL% NEQ 0 (
    taskkill /f /im ADOCAV.exe >nul 2>&1
    cmake --build . --parallel
)
echo.
if "%PORTABLE%"=="1" (
    echo Build done: %cd%\ADOCAV-Portable.exe
) else (
    echo Build done: %cd%\ADOCAV.exe
)
endlocal
