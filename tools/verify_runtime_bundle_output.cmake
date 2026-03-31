if(NOT DEFINED POWERSHELL_EXECUTABLE)
  message(FATAL_ERROR "POWERSHELL_EXECUTABLE is required")
endif()

if(NOT DEFINED REPO_ROOT)
  message(FATAL_ERROR "REPO_ROOT is required")
endif()

if(NOT DEFINED BUILD_DIR)
  message(FATAL_ERROR "BUILD_DIR is required")
endif()

if(NOT DEFINED OUTPUT_DIR)
  message(FATAL_ERROR "OUTPUT_DIR is required")
endif()

file(REMOVE_RECURSE "${OUTPUT_DIR}")
file(MAKE_DIRECTORY "${OUTPUT_DIR}")

set(EXPORT_SCRIPT "${REPO_ROOT}/scripts/export_runtime_bundle.ps1")
execute_process(
  COMMAND "${POWERSHELL_EXECUTABLE}"
          -ExecutionPolicy Bypass
          -File "${EXPORT_SCRIPT}"
          -Executable "C:/Games/Overwatch/Overwatch.exe"
          -Launcher "Battle.net"
          -Store "battlenet"
          -PrefixPath "C:/Prefixes/Overwatch"
          -BuildDir "${BUILD_DIR}"
          -OutputDir "${OUTPUT_DIR}"
  RESULT_VARIABLE export_result
  OUTPUT_VARIABLE export_stdout
  ERROR_VARIABLE export_stderr
)

if(NOT export_result EQUAL 0)
  message(FATAL_ERROR
    "Runtime bundle export failed with exit code ${export_result}\n"
    "stdout:\n${export_stdout}\n"
    "stderr:\n${export_stderr}"
  )
endif()

set(MANIFEST_PATH "${OUTPUT_DIR}/bundle-manifest.json")
if(NOT EXISTS "${MANIFEST_PATH}")
  message(FATAL_ERROR "Expected runtime bundle manifest at ${MANIFEST_PATH}")
endif()

file(READ "${MANIFEST_PATH}" manifest_contents)

set(expected_pairs
  "launchPlanJson|launch-plan.json"
  "launchPlanReport|launch-plan.txt"
  "setupChecklist|launch-plan.md"
  "bashSetupScript|launch-plan.setup.sh"
  "powershellSetupScript|launch-plan.setup.ps1"
  "bashLaunchScript|launch-plan.sh"
  "powershellLaunchScript|launch-plan.ps1"
  "compatibilityCatalogJson|compatibility-catalog.json"
  "compatibilityCatalogReport|compatibility-catalog.txt"
  "compatibilityCatalogMarkdown|compatibility-catalog.md"
  "profileLintReport|profile-lint.txt"
)

foreach(pair IN LISTS expected_pairs)
  string(REPLACE "|" ";" pair_fields "${pair}")
  list(GET pair_fields 0 field_name)
  list(GET pair_fields 1 expected_value)
  string(
    REGEX MATCH
    "\"${field_name}\"[ \t\r\n]*:[ \t\r\n]*\"${expected_value}\""
    matched_pair
    "${manifest_contents}"
  )
  if(NOT matched_pair)
    message(FATAL_ERROR
      "Manifest did not contain the expected portable entry for ${field_name}: ${expected_value}\n"
      "Manifest contents:\n${manifest_contents}"
    )
  endif()

  if(NOT EXISTS "${OUTPUT_DIR}/${expected_value}")
    message(FATAL_ERROR "Expected exported runtime bundle asset ${OUTPUT_DIR}/${expected_value}")
  endif()
endforeach()

string(FIND "${manifest_contents}" "${OUTPUT_DIR}" output_dir_reference)
if(NOT output_dir_reference EQUAL -1)
  message(FATAL_ERROR
    "Manifest still contained the absolute output directory path, so it is not portable.\n"
    "Manifest contents:\n${manifest_contents}"
  )
endif()
