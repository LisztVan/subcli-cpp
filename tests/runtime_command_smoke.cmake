cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED SUBCLI_BIN)
  message(FATAL_ERROR "SUBCLI_BIN is required")
endif()
if(NOT DEFINED TEST_WORK_DIR)
  message(FATAL_ERROR "TEST_WORK_DIR is required")
endif()
if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "SOURCE_DIR is required")
endif()

file(REMOVE_RECURSE "${TEST_WORK_DIR}")
file(MAKE_DIRECTORY "${TEST_WORK_DIR}")
file(COPY "${SOURCE_DIR}/templates" DESTINATION "${TEST_WORK_DIR}")
file(COPY "${SOURCE_DIR}/profiles" DESTINATION "${TEST_WORK_DIR}")

# Copy the binary into TEST_WORK_DIR so 'config init --portable' writes there
get_filename_component(LOCAL_BIN_NAME "${SUBCLI_BIN}" NAME)
set(BIN_COPY_DIR "${TEST_WORK_DIR}/subcli-bin")
file(MAKE_DIRECTORY "${BIN_COPY_DIR}")
file(COPY "${SUBCLI_BIN}" DESTINATION "${BIN_COPY_DIR}")
set(LOCAL_BIN "${BIN_COPY_DIR}/${LOCAL_BIN_NAME}")

function(run_cmd NAME EXPECTED_RESULT)
  execute_process(
    COMMAND "${LOCAL_BIN}" ${ARGN}
    WORKING_DIRECTORY "${TEST_WORK_DIR}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
    TIMEOUT 20
  )
  string(CONCAT combined "${output}" "${error}")
  message(STATUS "[${NAME}] exit=${result}")
  message(STATUS "[${NAME}] output=${combined}")

  if(EXPECTED_RESULT STREQUAL "zero")
    if(NOT result EQUAL 0)
      message(FATAL_ERROR "${NAME} expected exit 0 but got ${result}: ${combined}")
    endif()
  elseif(EXPECTED_RESULT STREQUAL "non_crash")
    if(result EQUAL 134 OR result EQUAL 139)
      message(FATAL_ERROR "${NAME} crashed with ${result}: ${combined}")
    endif()
  else()
    message(FATAL_ERROR "unknown EXPECTED_RESULT: ${EXPECTED_RESULT}")
  endif()

  set("${NAME}_OUTPUT" "${combined}" PARENT_SCOPE)
  set("${NAME}_RESULT" "${result}" PARENT_SCOPE)
endfunction()

run_cmd(config_init zero config init --portable)
run_cmd(doctor non_crash doctor --json)
run_cmd(status_without_runtime non_crash status)
run_cmd(stop_without_runtime non_crash stop)
run_cmd(daemon_status_without_runtime non_crash daemon status)
run_cmd(daemon_once_no_subscriptions non_crash daemon once --target mihomo --interval 1)
run_cmd(run_without_core non_crash run mihomo)

if(run_without_core_RESULT EQUAL 0)
  message(FATAL_ERROR "run mihomo without a configured core should not succeed")
endif()

string(FIND "${run_without_core_OUTPUT}" "not found" not_found_pos)
string(FIND "${run_without_core_OUTPUT}" "core_paths" core_paths_pos)
string(FIND "${run_without_core_OUTPUT}" "mihomo" mihomo_pos)
if(not_found_pos EQUAL -1 AND core_paths_pos EQUAL -1 AND mihomo_pos EQUAL -1)
  message(FATAL_ERROR "run mihomo error should mention missing core/mihomo/core_paths: ${run_without_core_OUTPUT}")
endif()
