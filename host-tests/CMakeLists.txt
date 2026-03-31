cmake_minimum_required(VERSION 3.25)
project(MetalVRBridgeHostChecks VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

set(MVRVB_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." CACHE PATH "MetalVR Bridge repository root")

include(FetchContent)

FetchContent_Declare(
  spirv_headers
  GIT_REPOSITORY https://github.com/KhronosGroup/SPIRV-Headers.git
  GIT_TAG        main
  GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(spirv_headers)

FetchContent_Declare(
  vulkan_headers
  GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
  GIT_TAG        main
  GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(vulkan_headers)

add_subdirectory("${MVRVB_REPO_ROOT}/src/common" src/common)
add_subdirectory("${MVRVB_REPO_ROOT}/src/shader_translator" src/shader_translator)
add_subdirectory("${MVRVB_REPO_ROOT}/src/vulkan_layer" src/vulkan_layer)

enable_testing()
add_subdirectory("${MVRVB_REPO_ROOT}/tests" tests)
