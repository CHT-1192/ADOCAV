cmake_minimum_required(VERSION 3.20)

# Prefer GCC over Clang (both over MSVC). Must be set before project().
if(NOT CMAKE_C_COMPILER AND NOT CMAKE_CXX_COMPILER)
    find_program(GCC_C   gcc)
    find_program(GCC_CXX g++)
    find_program(CLANG_C   clang)
    find_program(CLANG_CXX clang++)
    if(GCC_C AND GCC_CXX)
        set(CMAKE_C_COMPILER   "${GCC_C}"   CACHE FILEPATH "C compiler")
        set(CMAKE_CXX_COMPILER "${GCC_CXX}" CACHE FILEPATH "C++ compiler")
    elseif(CLANG_C AND CLANG_CXX)
        set(CMAKE_C_COMPILER   "${CLANG_C}"   CACHE FILEPATH "C compiler")
        set(CMAKE_CXX_COMPILER "${CLANG_CXX}" CACHE FILEPATH "C++ compiler")
    endif()
endif()

project(ADOCAV VERSION 1.0.0 LANGUAGES CXX C)

option(ADOCAV_PORTABLE "Build a static-linked portable executable" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Maximum safe optimization (no -ffast-math — timing precision is critical)
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_options(-O3 -march=native -fomit-frame-pointer)
endif()

# ---- Find Vulkan SDK ----
find_package(Vulkan REQUIRED)
if(NOT Vulkan_FOUND)
    message(FATAL_ERROR "Vulkan SDK not found. Install from https://vulkan.lunarg.com/")
endif()

# SPIR-V compiler: prefer glslc, fall back to glslangValidator
set(SPIRV_COMPILER_FLAGS "")
if(NOT Vulkan_GLSLC_EXECUTABLE)
    find_program(GLSLC_EXECUTABLE glslc)
    if(NOT GLSLC_EXECUTABLE)
        find_program(GLSLC_EXECUTABLE glslangValidator)
        if(GLSLC_EXECUTABLE)
            set(SPIRV_COMPILER_FLAGS "-V")  # glslangValidator needs -V for Vulkan SPIR-V
        endif()
    endif()
    if(GLSLC_EXECUTABLE)
        set(Vulkan_GLSLC_EXECUTABLE ${GLSLC_EXECUTABLE})
    else()
        message(FATAL_ERROR "Neither glslc nor glslangValidator found. Install Vulkan SDK or glslang-tools.")
    endif()
endif()

# ---- Dependencies via FetchContent ----
include(FetchContent)

# GLFW
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
    GIT_SHALLOW TRUE
)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)

# glm
FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 1.0.1
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(glm)

# nlohmann/json
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

# Dear ImGui
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.9
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(imgui)

# stb_image (single-header)
FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG master
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(stb)

# miniaudio (single-header)
FetchContent_Declare(
    miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG 0.11.22
    GIT_SHALLOW TRUE
)
set(MINIAUDIO_NO_LIBVORBIS ON CACHE BOOL "" FORCE)
set(MINIAUDIO_NO_LIBOPUS ON CACHE BOOL "" FORCE)
set(MINIAUDIO_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(MINIAUDIO_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SDL2_LIBRARY "SDL2_LIBRARY-NOTFOUND" CACHE FILEPATH "" FORCE)
set(STEAMAUDIO_LIBRARY "STEAMAUDIO_LIBRARY-NOTFOUND" CACHE FILEPATH "" FORCE)
FetchContent_MakeAvailable(miniaudio)

# tinyfiledialogs (single-header C library)
FetchContent_Declare(
    tinyfiledialogs
    GIT_REPOSITORY https://github.com/native-toolkit/tinyfiledialogs.git
    GIT_TAG 2.9.3
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(tinyfiledialogs)

# volk (Vulkan loader)
FetchContent_Declare(
    volk
    GIT_REPOSITORY https://github.com/zeux/volk.git
    GIT_TAG vulkan-sdk-1.4.350.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(volk)

# VMA (Vulkan Memory Allocator)
FetchContent_Declare(
    vma
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
    GIT_TAG v3.4.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(vma)

# ---- SPIR-V Shader Compilation ----
set(SHADER_SRC_DIR ${CMAKE_SOURCE_DIR}/shaders)
set(SHADER_BIN_DIR ${CMAKE_BINARY_DIR}/shaders)

file(MAKE_DIRECTORY ${SHADER_BIN_DIR})

set(SHADER_SOURCES
    ${SHADER_SRC_DIR}/tile.vert
    ${SHADER_SRC_DIR}/tile.frag
    ${SHADER_SRC_DIR}/planet.vert
    ${SHADER_SRC_DIR}/planet.frag
    ${SHADER_SRC_DIR}/trail.vert
    ${SHADER_SRC_DIR}/trail.frag
    ${SHADER_SRC_DIR}/icon.vert
    ${SHADER_SRC_DIR}/icon.frag
    ${SHADER_SRC_DIR}/tile_cull.comp
    ${SHADER_SRC_DIR}/tile_offset.comp
)

set(SHADER_SPV_FILES "")
foreach(SHADER_FILE ${SHADER_SOURCES})
    get_filename_component(SHADER_NAME ${SHADER_FILE} NAME_WE)
    get_filename_component(SHADER_EXT ${SHADER_FILE} EXT)
    if(${SHADER_EXT} STREQUAL ".vert")
        set(STAGE "vert")
    elseif(${SHADER_EXT} STREQUAL ".frag")
        set(STAGE "frag")
    elseif(${SHADER_EXT} STREQUAL ".comp")
        set(STAGE "comp")
    else()
        message(FATAL_ERROR "Unknown shader extension: ${SHADER_EXT}")
    endif()
    set(SPV_FILE ${SHADER_BIN_DIR}/${SHADER_NAME}.${STAGE}.spv)
    add_custom_command(
        OUTPUT ${SPV_FILE}
        COMMAND ${Vulkan_GLSLC_EXECUTABLE} ${SPIRV_COMPILER_FLAGS} ${SHADER_FILE} -o ${SPV_FILE}
        DEPENDS ${SHADER_FILE}
        COMMENT "Compiling ${SHADER_NAME}.${STAGE} → SPIR-V"
    )
    list(APPEND SHADER_SPV_FILES ${SPV_FILE})
endforeach()

# Unified SPIR-V target (one custom target to build all shaders)
add_custom_target(spirv ALL DEPENDS ${SHADER_SPV_FILES})

# ---- Source files ----
set(SOURCES
    # Application layer
    app/main.cpp
    app/Application.h
    app/Application.cpp
    app/LauncherWindow.h
    app/LauncherWindow.cpp
    app/LoadingWindow.h
    app/LoadingWindow.cpp
    app/LevelLoader.h
    app/LevelLoader.cpp
    app/GameWindow.h
    app/GameWindow.cpp

    # Vulkan loader + memory allocator
    render/vulkan_impl/volk_impl.cpp
    render/vulkan_impl/vma_impl.cpp

    # Level parser (unchanged from ADOCAO)
    level/LevelData.h
    level/LevelData.cpp
    level/JsonCleaner.h
    level/JsonCleaner.cpp

    # Easing (unchanged from ADOCAO)
    util/Easing.h

    # Logger (unchanged from ADOCAO)
    util/Logger.h
    util/Logger.cpp

    # Vulkan renderer
    render/vulkan_impl/VulkanCore.h
    render/vulkan_impl/VulkanCore.cpp
    render/vulkan_impl/VulkanSwapchain.h
    render/vulkan_impl/VulkanSwapchain.cpp
    render/vulkan_impl/FrameData.h
    render/vulkan_impl/FrameData.cpp
    render/vulkan_impl/VulkanRenderPass.h
    render/vulkan_impl/VulkanRenderPass.cpp
    render/vulkan_impl/VulkanPipeline.h
    render/vulkan_impl/VulkanPipeline.cpp
    render/vulkan_impl/VulkanBuffer.h
    render/vulkan_impl/VulkanBuffer.cpp
    render/vulkan_impl/VulkanPipelineCache.h
    render/vulkan_impl/VulkanPipelineCache.cpp

    # Camera (adapted for Vulkan NDC)
    camera/Camera.h
    camera/Camera.cpp

    # Track (geometry unchanged, mesh adapted for Vulkan)
    track/TileGeometry.h
    track/TileGeometry.cpp
    track/TileMesh.h
    track/TileMesh.cpp

    # Game
    game/Planet.h
    game/Planet.cpp
    game/PlaybackEngine.h
    game/PlaybackEngine.cpp

    # Render objects
    render/PlanetTrail.h
    render/PlanetTrail.cpp

    # Audio (unchanged from ADOCAO)
    audio/AudioEngine.h
    audio/AudioEngine.cpp
    audio/HitsoundManager.h
    audio/HitsoundManager.cpp
    audio/stb_vorbis_impl.cpp
)

# ImGui sources (both backends: OpenGL3 for launcher, Vulkan for game)
set(IMGUI_SOURCES
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
)

# stb_image (header-only)
add_library(stb_image STATIC util/stb_impl.cpp)
target_include_directories(stb_image PUBLIC ${stb_SOURCE_DIR})

# tinyfiledialogs
add_library(tinyfiledialogs STATIC ${tinyfiledialogs_SOURCE_DIR}/tinyfiledialogs.c)
target_include_directories(tinyfiledialogs PUBLIC ${tinyfiledialogs_SOURCE_DIR})

# ---- Main executable ----
add_executable(adocav ${SOURCES} ${IMGUI_SOURCES})
set_target_properties(adocav PROPERTIES WIN32_EXECUTABLE TRUE OUTPUT_NAME "ADOCAV")
add_dependencies(adocav spirv)

# VMA must use dynamic Vulkan function loading (volk compatibility)
target_compile_definitions(adocav PRIVATE VMA_STATIC_VULKAN_FUNCTIONS=0)

target_include_directories(adocav PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_BINARY_DIR}/shaders
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${miniaudio_SOURCE_DIR}
    ${volk_SOURCE_DIR}
    ${vma_SOURCE_DIR}/include
)

target_link_libraries(adocav PRIVATE
    glfw
    glm::glm
    nlohmann_json::nlohmann_json
    stb_image
    tinyfiledialogs
    Vulkan::Vulkan
)


# Platform-specific
if(WIN32)
    target_link_libraries(adocav PRIVATE ole32 opengl32)
    target_compile_definitions(adocav PRIVATE NOMINMAX UNICODE _UNICODE VK_USE_PLATFORM_WIN32_KHR VMA_STATIC_VULKAN_FUNCTIONS=0)
elseif(UNIX AND NOT APPLE)
    find_package(OpenGL REQUIRED)
    target_link_libraries(adocav PRIVATE OpenGL::GL ${CMAKE_DL_LIBS} pthread)
    target_compile_definitions(adocav PRIVATE VK_USE_PLATFORM_XLIB_KHR)
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(WAYLAND wayland-client wayland-egl IMPORTED_TARGET QUIET)
    endif()
    if(WAYLAND_FOUND)
        target_compile_definitions(adocav PRIVATE VK_USE_PLATFORM_WAYLAND_KHR)
        message(STATUS "Wayland found — GLFW will support Wayland")
    endif()
endif()

# Static linking for portable builds
if(ADOCAV_PORTABLE)
    if(MINGW)
        target_link_options(adocav PRIVATE -static -static-libgcc -static-libstdc++)
    elseif(MSVC)
        set_property(TARGET adocav PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
    set_target_properties(adocav PROPERTIES OUTPUT_NAME "ADOCAV-Portable")
endif()

# ---- Install ----
install(TARGETS adocav RUNTIME DESTINATION bin)

# ---- Copy assets to build directory ----
add_custom_command(TARGET adocav POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_SOURCE_DIR}/assets"
    "${CMAKE_BINARY_DIR}/assets"
    COMMENT "Copying assets to build directory"
)

# ---- Copy SPIR-V shaders to build directory ----
add_custom_command(TARGET adocav POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${SHADER_BIN_DIR}"
    "${CMAKE_BINARY_DIR}/shaders"
    COMMENT "Copying SPIR-V shaders to build directory"
)

# ---- Copy example level (if available) ----
if(EXISTS "${CMAKE_SOURCE_DIR}/../re_adojas/lib/example/level.adofai")
    file(COPY ${CMAKE_SOURCE_DIR}/../re_adojas/lib/example/level.adofai
         DESTINATION ${CMAKE_BINARY_DIR})
endif()
