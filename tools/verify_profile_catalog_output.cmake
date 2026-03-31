if(NOT DEFINED TOOL OR TOOL STREQUAL "")
  message(FATAL_ERROR "TOOL is required")
endif()

if(NOT DEFINED PROFILE_DIR OR PROFILE_DIR STREQUAL "")
  message(FATAL_ERROR "PROFILE_DIR is required")
endif()

if(NOT DEFINED OUTPUT OR OUTPUT STREQUAL "")
  message(FATAL_ERROR "OUTPUT is required")
endif()

if(NOT DEFINED EXPECTED_SUBSTRING OR EXPECTED_SUBSTRING STREQUAL "")
  message(FATAL_ERROR "EXPECTED_SUBSTRING is required")
endif()

get_filename_component(output_dir "${OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${output_dir}")
file(REMOVE "${OUTPUT}")

set(command "${TOOL}" "--profiles-dir" "${PROFILE_DIR}" "--out" "${OUTPUT}")

if(DEFINED JSON AND JSON)
  list(APPEND command "--json")
endif()

execute_process(
  COMMAND ${command}
  RESULT_VARIABLE result
  OUTPUT_VARIABLE stdout
  ERROR_VARIABLE stderr
)

if(NOT result EQUAL 0)
  message(FATAL_ERROR
    "Profile catalog command failed with exit code ${result}\n"
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
