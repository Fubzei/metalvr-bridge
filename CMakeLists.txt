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

# ── Third-party: GoogleTest ───────────────────────────────────────────────────
if(MVRVB_BUILD_TESTS)
  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        main
    GIT_SHALLOW    TRUE
  )
  FetchContent_MakeAvailable(googletest)
endif()

# ── Metal shader compilation ──────────────────────────────────────────────────
# Compile all .metal files in the shaders/ directory into a single metallib
# that is embedded in the application bundle as default.metallib.
file(GLOB METAL_SOURCES "${CMAKE_SOURCE_DIR}/shaders/*.metal")
if(METAL_SOURCES)
  set(METALLIB_OUTPUT "${CMAKE_BINARY_DIR}/default.metallib")
  set(METAL_INTERMEDIATE_DIR "${CMAKE_BINARY_DIR}/metal_ir")
  file(MAKE_DIRECTORY "${METAL_INTERMEDIATE_DIR}")

  set(METAL_AIR_FILES)
  foreach(METAL_SRC ${METAL_SOURCES})
    get_filename_component(METAL_NAME "${METAL_SRC}" NAME_WE)
    set(AIR_FILE "${METAL_INTERMEDIATE_DIR}/${METAL_NAME}.air")
    add_custom_command(
      OUTPUT "${AIR_FILE}"
      COMMAND xcrun -sdk macosx metal
              -std=metal2.4
              -O2
              -target air64-apple-macos14.0
              -c "${METAL_SRC}"
              -o "${AIR_FILE}"
      DEPENDS "${METAL_SRC}"
      COMMENT "Compiling ${METAL_NAME}.metal"
    )
    list(APPEND METAL_AIR_FILES "${AIR_FILE}")
  endforeach()

  add_custom_command(
    OUTPUT "${METALLIB_OUTPUT}"
    COMMAND xcrun -sdk macosx metallib ${METAL_AIR_FILES} -o "${METALLIB_OUTPUT}"
    DEPENDS ${METAL_AIR_FILES}
    COMMENT "Linking default.metallib"
  )
  add_custom_target(MetalShaders ALL DEPENDS "${METALLIB_OUTPUT}")
endif()

# ── Subdirectories ───────────────────────────────────────────────────────────
add_subdirectory(src/common)
add_subdirectory(src/shader_translator)
add_subdirectory(src/vulkan_layer)
add_subdirectory(src/vr_runtime)

# Optional subdirectories (wine_bridge, tools, tests may not exist yet)
foreach(_subdir src/wine_bridge)
  if(EXISTS "${CMAKE_SOURCE_DIR}/${_subdir}/CMakeLists.txt")
    add_subdirectory(${_subdir})
  endif()
endforeach()

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
