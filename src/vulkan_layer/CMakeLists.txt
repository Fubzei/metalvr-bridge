# ── Format table (pure C++) ────────────────────────────────────────────────────
add_library(mvrvb_format_table STATIC
    format_table/format_table.cpp
)
target_include_directories(mvrvb_format_table PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mvrvb_format_table PUBLIC Vulkan::Headers)
target_compile_features(mvrvb_format_table PUBLIC cxx_std_20)
target_compile_options(mvrvb_format_table PRIVATE -Wall -Wextra -Wno-unused-parameter -fvisibility=hidden)

# ── The main Vulkan ICD dylib ──────────────────────────────────────────────────
# All translation sources compiled into one shared library that the Vulkan
# loader discovers via the ICD JSON manifest.
add_library(MetalVRBridge_ICD SHARED
    # ICD loader negotiation
    icd/vulkan_icd.cpp

    # Device / instance management (Obj-C++)
    device/vk_device.mm

    # Memory management (Obj-C++)
    memory/vk_memory.mm

    # Resources: buffer, image, sampler, shader module (Obj-C++)
    resources/vk_resources.mm

    # Descriptor set lifecycle (Obj-C++)
    descriptors/vk_descriptors.mm

    # Pipeline: shader modules, graphics/compute pipelines (Obj-C++)
    pipeline/vk_pipeline.mm

    # Command recording (Obj-C++)
    commands/vk_commands.mm

    # Synchronisation: fence, semaphore, event (Obj-C++)
    sync/vk_sync.mm

    # Swapchain + surface (Obj-C++)
    swapchain/vk_swapchain.mm
)

set_target_properties(MetalVRBridge_ICD PROPERTIES
    OUTPUT_NAME        "MetalVRBridge"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    # The Vulkan loader requires the ICD dylib to have a specific install name
    # that matches the path in the JSON manifest.
    BUILD_WITH_INSTALL_RPATH YES
    INSTALL_RPATH        "@loader_path/../lib"
)

target_include_directories(MetalVRBridge_ICD PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(MetalVRBridge_ICD PRIVATE
    mvrvb_common
    mvrvb_format_table
    mvrvb_shader_cache       # pulls in spirv_parser + msl_emitter
    Vulkan::Headers
    "-framework Metal"
    "-framework MetalKit"
    "-framework Foundation"
    "-framework QuartzCore"
    "-framework AppKit"
)

target_compile_features(MetalVRBridge_ICD PRIVATE cxx_std_20)
target_compile_options(MetalVRBridge_ICD PRIVATE
    -Wall -Wextra -Wno-unused-parameter
    -fvisibility=hidden
    # The three ICD entry points must be exported:
    -fvisibility-inlines-hidden
)

# Export ONLY the three ICD negotiation symbols + vkGetDeviceProcAddr
# Everything else is hidden — the loader reaches it through the dispatch table.
set_target_properties(MetalVRBridge_ICD PROPERTIES
    LINK_FLAGS "-exported_symbols_list ${CMAKE_CURRENT_SOURCE_DIR}/icd/exported_symbols.txt"
)

# ── Install ────────────────────────────────────────────────────────────────────
install(TARGETS MetalVRBridge_ICD DESTINATION lib)
install(FILES ${CMAKE_SOURCE_DIR}/vulkan_icd.json DESTINATION share/vulkan/icd.d)
