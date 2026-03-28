# ── OpenVR shim ────────────────────────────────────────────────────────────────
add_library(mvrvb_openvr_shim SHARED
    openvr_shim/openvr_shim.mm
    openvr_shim/vr_system.mm
    openvr_shim/vr_compositor.mm
    openvr_shim/vr_input.mm
    openvr_shim/vr_overlay.mm
    openvr_shim/vr_chaperone.mm
    openvr_shim/vr_settings.mm
    openvr_shim/vr_rendermodels.mm
)

set_target_properties(mvrvb_openvr_shim PROPERTIES
    OUTPUT_NAME        "openvr_api"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

target_include_directories(mvrvb_openvr_shim PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party/openvr/headers
)

target_link_libraries(mvrvb_openvr_shim PRIVATE
    mvrvb_common
    "-framework Metal"
    "-framework Foundation"
    "-framework CoreMotion"
    "-framework IOSurface"
)

target_compile_features(mvrvb_openvr_shim PRIVATE cxx_std_20)
target_compile_options(mvrvb_openvr_shim PRIVATE
    -Wall -Wextra -Wno-unused-parameter
    -fvisibility=hidden
)

# ── VR Compositor ──────────────────────────────────────────────────────────────
add_library(mvrvb_compositor STATIC
    compositor/compositor.mm
    compositor/layer_manager.mm
    compositor/distortion_mesh.mm
)

target_include_directories(mvrvb_compositor PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(mvrvb_compositor PUBLIC
    mvrvb_common
    "-framework Metal"
    "-framework MetalKit"
    "-framework QuartzCore"
    "-framework IOSurface"
)

target_compile_features(mvrvb_compositor PUBLIC cxx_std_20)

# ── Async Timewarp ─────────────────────────────────────────────────────────────
add_library(mvrvb_timewarp STATIC
    timewarp/async_timewarp.mm
)

target_include_directories(mvrvb_timewarp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(mvrvb_timewarp PUBLIC
    mvrvb_common
    mvrvb_compositor
    "-framework Metal"
    "-framework CoreFoundation"
)

target_compile_features(mvrvb_timewarp PUBLIC cxx_std_20)

# ── HMD backends ───────────────────────────────────────────────────────────────
# Vision Pro backend (CompositorServices + ARKit, macOS/visionOS only)
if(MVRVB_VISION_PRO)
    add_library(mvrvb_hmd_vision_pro STATIC
        hmd/vision_pro/vision_pro_backend.mm
        hmd/vision_pro/vision_pro_tracking.mm
    )
    target_include_directories(mvrvb_hmd_vision_pro PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(mvrvb_hmd_vision_pro PUBLIC
        mvrvb_common
        mvrvb_compositor
        "-framework ARKit"
        "-framework CompositorServices"
        "-framework Metal"
    )
    target_compile_features(mvrvb_hmd_vision_pro PUBLIC cxx_std_20)
endif()

# Quest streaming backend (VideoToolbox H.265 + UDP)
if(MVRVB_STREAMING)
    add_library(mvrvb_hmd_quest STATIC
        hmd/quest/quest_backend.mm
        hmd/quest/h265_encoder.mm
        hmd/quest/udp_stream.mm
    )
    target_include_directories(mvrvb_hmd_quest PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(mvrvb_hmd_quest PUBLIC
        mvrvb_common
        mvrvb_compositor
        "-framework VideoToolbox"
        "-framework CoreMedia"
        "-framework CoreVideo"
        "-framework Network"
    )
    target_compile_features(mvrvb_hmd_quest PUBLIC cxx_std_20)
endif()
