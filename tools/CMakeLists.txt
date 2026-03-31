get_filename_component(MVRVB_TOOLS_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(MVRVB_TOOLS_PROFILE_DIR "${MVRVB_TOOLS_REPO_ROOT}/profiles")
set(MVRVB_TOOLS_VERIFY_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/verify_runtime_plan_output.cmake")

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

  add_test(
    NAME runtime_plan_preview_overwatch_json
    COMMAND mvrvb_runtime_plan_preview
            --profiles-dir "${MVRVB_TOOLS_PROFILE_DIR}"
            --exe "C:/Games/Overwatch/Overwatch.exe"
            --launcher "Battle.net"
            --store "battlenet"
            --json
  )
  set_tests_properties(runtime_plan_preview_overwatch_json PROPERTIES
    PASS_REGULAR_EXPRESSION "\"selectedProfileId\":\"overwatch-2\""
  )

  add_test(
    NAME runtime_plan_preview_overwatch_json_file
    COMMAND "${CMAKE_COMMAND}"
            -DTOOL=$<TARGET_FILE:mvrvb_runtime_plan_preview>
            -DPROFILE_DIR=${MVRVB_TOOLS_PROFILE_DIR}
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/artifacts/overwatch.launch-plan.json
            -DEXECUTABLE=C:/Games/Overwatch/Overwatch.exe
            -DLAUNCHER=Battle.net
            -DSTORE=battlenet
            -DJSON=ON
            -DEXPECTED_SUBSTRING="selectedProfileId\":\"overwatch-2"
            -P "${MVRVB_TOOLS_VERIFY_SCRIPT}"
  )

  add_test(
    NAME runtime_plan_preview_overwatch_report_file
    COMMAND "${CMAKE_COMMAND}"
            -DTOOL=$<TARGET_FILE:mvrvb_runtime_plan_preview>
            -DPROFILE_DIR=${MVRVB_TOOLS_PROFILE_DIR}
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/artifacts/overwatch.launch-plan.txt
            -DEXECUTABLE=C:/Games/Overwatch/Overwatch.exe
            -DLAUNCHER=Battle.net
            -DSTORE=battlenet
            -DEXPECTED_SUBSTRING=Selected\ profile:\ overwatch-2
            -P "${MVRVB_TOOLS_VERIFY_SCRIPT}"
  )

  add_test(
    NAME runtime_plan_preview_input_file
    COMMAND "${CMAKE_COMMAND}"
            -DTOOL=$<TARGET_FILE:mvrvb_runtime_plan_preview>
            -DPROFILE_DIR=${MVRVB_TOOLS_PROFILE_DIR}
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/artifacts/overwatch.roundtrip.json
            -DEXECUTABLE=C:/Games/Overwatch/Overwatch.exe
            -DLAUNCHER=Battle.net
            -DSTORE=battlenet
            -DJSON=ON
            -DROUNDTRIP=ON
            -DEXPECTED_SUBSTRING="selectedProfileId\":\"overwatch-2"
            -DROUNDTRIP_EXPECTED_SUBSTRING=Selected\ profile:\ overwatch-2
            -P "${MVRVB_TOOLS_VERIFY_SCRIPT}"
  )
endif()
