# ── SPIR-V parser (pure C++) ───────────────────────────────────────────────────
add_library(mvrvb_spirv_parser STATIC
    spirv/spirv_parser.cpp
)
target_include_directories(mvrvb_spirv_parser PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mvrvb_spirv_parser PUBLIC SPIRV-Headers mvrvb_common)
target_compile_features(mvrvb_spirv_parser PUBLIC cxx_std_20)
target_compile_options(mvrvb_spirv_parser PRIVATE -Wall -Wextra -Wno-unused-parameter -fvisibility=hidden)

# ── MSL emitter (pure C++) ─────────────────────────────────────────────────────
add_library(mvrvb_msl_emitter STATIC
    msl_emitter/spirv_to_msl.cpp
)
target_include_directories(mvrvb_msl_emitter PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mvrvb_msl_emitter PUBLIC mvrvb_spirv_parser mvrvb_common)
target_compile_features(mvrvb_msl_emitter PUBLIC cxx_std_20)
target_compile_options(mvrvb_msl_emitter PRIVATE -Wall -Wextra -Wno-unused-parameter -fvisibility=hidden)

# ── Shader cache (Obj-C++ for MTLLibrary) ─────────────────────────────────────
add_library(mvrvb_shader_cache STATIC
    cache/shader_cache.mm
)
target_include_directories(mvrvb_shader_cache PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mvrvb_shader_cache PUBLIC mvrvb_msl_emitter "-framework Metal")
target_compile_features(mvrvb_shader_cache PUBLIC cxx_std_20)
target_compile_options(mvrvb_shader_cache PRIVATE -Wall -Wextra -Wno-unused-parameter -fvisibility=hidden)

# ── Geometry shader emulator (compute-based) ──────────────────────────────────
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/geometry_emu/geometry_emu.mm")
    add_library(mvrvb_geometry_emu STATIC
        geometry_emu/geometry_emu.mm
    )
    target_include_directories(mvrvb_geometry_emu PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(mvrvb_geometry_emu PUBLIC mvrvb_common "-framework Metal")
    target_compile_features(mvrvb_geometry_emu PUBLIC cxx_std_20)
endif()
