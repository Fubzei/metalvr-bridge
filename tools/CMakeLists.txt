get_filename_component(MVRVB_TOOLS_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(MVRVB_TOOLS_PROFILE_DIR "${MVRVB_TOOLS_REPO_ROOT}/profiles")

add_executable(mvrvb_runtime_plan_preview
    runtime_plan_preview.cpp
)

target_link_libraries(mvrvb_runtime_plan_preview PRIVATE mvrvb_common)

target_compile_features(mvrvb_runtime_plan_preview PRIVATE cxx_std_20)

target_compile_definitions(mvrvb_runtime_plan_preview PRIVATE
    MVRVB_TOOLS_PROFILE_DIR="${MVRVB_TOOLS_PROFILE_DIR}"
)

if(MVRVB_BUILD_TESTS)
  add_test(
    NAME runtime_plan_preview_help
    COMMAND mvrvb_runtime_plan_preview --help
  )

  add_test(
    NAME runtime_plan_preview_overwatch
    COMMAND mvrvb_runtime_plan_preview
            --profiles-dir "${MVRVB_TOOLS_PROFILE_DIR}"
            --exe "C:/Games/Overwatch/Overwatch.exe"
            --launcher "Battle.net"
            --store "battlenet"
  )
  set_tests_properties(runtime_plan_preview_overwatch PROPERTIES
    PASS_REGULAR_EXPRESSION "Selected profile: overwatch-2"
  )
endif()
