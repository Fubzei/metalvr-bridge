cmake_minimum_required(VERSION 3.25)
project(MetalVRBridge VERSION 0.1.0 LANGUAGES CXX OBJCXX C)

# ── Language standards ──────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OBJCXX_STANDARD 20)

# ── Platform guard ───────────────────────────────────────────────────────────
if(NOT APPLE)
  message(FATAL_ERROR "MetalVR Bridge targets macOS/visionOS only.")
endif()

# ── macOS deployment target ──────────────────────────────────────────────────
set(CMAKE_OSX_DEPLOYMENT_TARGET "14.0" CACHE STRING "Minimum macOS version")

# ── Build options ────────────────────────────────────────────────────────────
option(MVRVB_BUILD_TESTS       "Build test suite"                     ON)
option(MVRVB_BUILD_TOOLS       "Build developer tools"                ON)
option(MVRVB_ENABLE_ASAN       "Enable AddressSanitizer (debug only)" OFF)
option(MVRVB_ENABLE_VALIDATION "Enable internal validation layer"     ON)
option(MVRVB_ENABLE_PROFILER   "Enable built-in GPU profiler overlay" ON)
option(MVRVB_VISION_PRO        "Build Apple Vision Pro backend"       ON)
option(MVRVB_STREAMING         "Build network streaming backend"      ON)

# ── Global compile flags ─────────────────────────────────────────────────────
add_compile_options(
  -Wall -Wextra -Wpedantic
  -Wno-unused-parameter
  $<$<CONFIG:Release>:-O3 -flto>
  $<$<CONFIG:Debug>:-O0 -g -fno-omit-frame-pointer>
)

if(MVRVB_ENABLE_ASAN AND CMAKE_BUILD_TYPE STREQUAL "Debug")
  add_compile_options(-fsanitize=address,undefined)
  add_link_options(-fsanitize=address,undefined)
endif()

# ── Find system frameworks ───────────────────────────────────────────────────
find_library(METAL_FRAMEWORK         Metal)
find_library(METALKIT_FRAMEWORK      MetalKit)
find_library(METALPERFORMANCESHADERS MetalPerformanceShaders)
find_library(FOUNDATION_FRAMEWORK    Foundation)
find_library(QUARTZCORE_FRAMEWORK    QuartzCore)
find_library(APPKIT_FRAMEWORK        AppKit)
find_library(VIDEOTOOLBOX_FRAMEWORK  VideoToolbox)
find_library(IOSURFACE_FRAMEWORK     IOSurface)
find_library(ARKIT_FRAMEWORK         ARKit)

# ── Third-party: SPIRV-Headers (header-only) ─────────────────────────────────
include(FetchContent)
FetchContent_Declare(
  spirv_headers
  GIT_REPOSITORY https://github.com/KhronosGroup/SPIRV-Headers.git
  GIT_TAG        main
  GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(spirv_headers)

# ── Third-party: Vulkan-Headers (header-only) ────────────────────────────────
FetchContent_Declare(
  vulkan_headers
  GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
  GIT_TAG        main
  GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(vulkan_headers)
add_subdirectory(src/common)
add_subdirectory(src/shader_translator)
add_subdirectory(src/vulkan_layer)

# ── Third-party: GoogleTest ───────────────────────────────────────────────────
if(MVRVB_BUILD_TESTS)
  enable_testing()
  if(EXISTS "${CMAKE_SOURCE_DIR}/tests/CMakeLists.txt")
    add_subdirectory(tests)
  endif()
endif()

if(MVRVB_BUILD_TOOLS)
  if(EXISTS "${CMAKE_SOURCE_DIR}/tools/CMakeLists.txt")
    add_subdirectory(tools)
  endif()
endif()
