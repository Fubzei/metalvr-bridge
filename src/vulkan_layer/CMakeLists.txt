add_library(mvrvb_format_table STATIC
    format_table/format_table.cpp
)
target_include_directories(mvrvb_format_table PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mvrvb_format_table PUBLIC Vulkan::Headers)
target_compile_features(mvrvb_format_table PUBLIC cxx_std_20)
target_compile_options(mvrvb_format_table PRIVATE -Wall -Wextra -Wno-unused-parameter -fvisibility=hidden)

# Pure transfer helpers that can be tested off-Mac.
add_library(mvrvb_transfer_utils STATIC
    commands/transfer_utils.cpp
)
target_include_directories(mvrvb_transfer_utils PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mvrvb_transfer_utils PUBLIC Vulkan::Headers)
target_compile_features(mvrvb_transfer_utils PUBLIC cxx_std_20)
target_compile_options(mvrvb_transfer_utils PRIVATE -Wall -Wextra -Wno-unused-parameter -fvisibility=hidden)

if(APPLE)
    # The main Vulkan ICD dylib.
    add_library(MetalVRBridge_ICD SHARED
        icd/vulkan_icd.cpp
        device/vk_device.mm
        memory/vk_memory.mm
        resources/vk_resources.mm
        descriptors/vk_descriptors.mm
        pipeline/vk_pipeline.mm
        commands/vk_commands.mm
        sync/vk_sync.mm
        swapchain/vk_swapchain.mm
    )

    set_target_properties(MetalVRBridge_ICD PROPERTIES
        OUTPUT_NAME "MetalVRBridge"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
        BUILD_WITH_INSTALL_RPATH YES
        INSTALL_RPATH "@loader_path/../lib"
    )

    target_include_directories(MetalVRBridge_ICD PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src
    )

    target_link_libraries(MetalVRBridge_ICD PRIVATE
        mvrvb_common
        mvrvb_format_table
        mvrvb_transfer_utils
        mvrvb_shader_cache
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
        -fvisibility-inlines-hidden
    )

    set_target_properties(MetalVRBridge_ICD PROPERTIES
        LINK_FLAGS "-exported_symbols_list ${CMAKE_CURRENT_SOURCE_DIR}/icd/exported_symbols.txt"
    )

    install(TARGETS MetalVRBridge_ICD DESTINATION lib)
    install(FILES ${CMAKE_SOURCE_DIR}/vulkan_icd.json DESTINATION share/vulkan/icd.d)
endif()
