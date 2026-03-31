if(NOT DEFINED TOOL OR TOOL STREQUAL "")
  message(FATAL_ERROR "TOOL is required")
endif()

if(NOT DEFINED PROFILE_DIR OR PROFILE_DIR STREQUAL "")
  message(FATAL_ERROR "PROFILE_DIR is required")
endif()

if(NOT DEFINED OUTPUT OR OUTPUT STREQUAL "")
  message(FATAL_ERROR "OUTPUT is required")
endif()

if(NOT DEFINED EXECUTABLE OR EXECUTABLE STREQUAL "")
  message(FATAL_ERROR "EXECUTABLE is required")
endif()

if(NOT DEFINED EXPECTED_SUBSTRING OR EXPECTED_SUBSTRING STREQUAL "")
  message(FATAL_ERROR "EXPECTED_SUBSTRING is required")
endif()

get_filename_component(output_dir "${OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${output_dir}")
file(REMOVE "${OUTPUT}")

set(command "${TOOL}" "--profiles-dir" "${PROFILE_DIR}" "--exe" "${EXECUTABLE}" "--out" "${OUTPUT}")

if(DEFINED LAUNCHER AND NOT LAUNCHER STREQUAL "")
  list(APPEND command "--launcher" "${LAUNCHER}")
endif()

if(DEFINED STORE AND NOT STORE STREQUAL "")
  list(APPEND command "--store" "${STORE}")
endif()

if(DEFINED JSON AND JSON)
  list(APPEND command "--json")
endif()

if(DEFINED BASH AND BASH)
  list(APPEND command "--bash")
endif()

if(DEFINED POWERSHELL AND POWERSHELL)
  list(APPEND command "--powershell")
endif()

if(DEFINED WINE_BINARY AND NOT WINE_BINARY STREQUAL "")
  list(APPEND command "--wine-binary" "${WINE_BINARY}")
endif()

if(DEFINED PREFIX_PATH AND NOT PREFIX_PATH STREQUAL "")
  list(APPEND command "--prefix" "${PREFIX_PATH}")
endif()

if(DEFINED WORKING_DIR AND NOT WORKING_DIR STREQUAL "")
  list(APPEND command "--working-dir" "${WORKING_DIR}")
endif()

execute_process(
  COMMAND ${command}
  RESULT_VARIABLE result
  OUTPUT_VARIABLE stdout
  ERROR_VARIABLE stderr
)

if(NOT result EQUAL 0)
  message(FATAL_ERROR
    "Runtime-plan preview command failed with exit code ${result}\n"
    "stdout:\n${stdout}\n"
    "stderr:\n${stderr}")
endif()

if(NOT EXISTS "${OUTPUT}")
  message(FATAL_ERROR "Expected output file was not created: ${OUTPUT}")
endif()

file(READ "${OUTPUT}" contents)
string(FIND "${contents}" "${EXPECTED_SUBSTRING}" expected_pos)
if(expected_pos EQUAL -1)
  message(FATAL_ERROR
    "Expected substring not found in ${OUTPUT}\n"
    "Expected: ${EXPECTED_SUBSTRING}\n"
    "Contents:\n${contents}")
endif()

if(DEFINED ROUNDTRIP AND ROUNDTRIP)
  set(roundtrip_expected "${EXPECTED_SUBSTRING}")
  if(DEFINED ROUNDTRIP_EXPECTED_SUBSTRING AND NOT ROUNDTRIP_EXPECTED_SUBSTRING STREQUAL "")
    set(roundtrip_expected "${ROUNDTRIP_EXPECTED_SUBSTRING}")
  endif()

  execute_process(
    COMMAND "${TOOL}" "--input" "${OUTPUT}"
    RESULT_VARIABLE roundtrip_result
    OUTPUT_VARIABLE roundtrip_stdout
    ERROR_VARIABLE roundtrip_stderr
  )

  if(NOT roundtrip_result EQUAL 0)
    message(FATAL_ERROR
      "Runtime-plan preview roundtrip failed with exit code ${roundtrip_result}\n"
      "stdout:\n${roundtrip_stdout}\n"
      "stderr:\n${roundtrip_stderr}")
  endif()

  string(FIND "${roundtrip_stdout}" "${roundtrip_expected}" roundtrip_pos)
  if(roundtrip_pos EQUAL -1)
    message(FATAL_ERROR
      "Expected roundtrip substring not found in preview output\n"
      "Expected: ${roundtrip_expected}\n"
      "Output:\n${roundtrip_stdout}")
  endif()
endif()
