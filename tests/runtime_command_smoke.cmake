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

get_filename_component(SUBCLI_BIN "${SUBCLI_BIN}" ABSOLUTE)
get_filename_component(TEST_WORK_DIR "${TEST_WORK_DIR}" ABSOLUTE)
get_filename_component(SOURCE_DIR "${SOURCE_DIR}" ABSOLUTE)

file(REMOVE_RECURSE "${TEST_WORK_DIR}")
file(MAKE_DIRECTORY "${TEST_WORK_DIR}/data")
file(MAKE_DIRECTORY "${TEST_WORK_DIR}/data/assets")
file(MAKE_DIRECTORY "${TEST_WORK_DIR}/cache")
file(MAKE_DIRECTORY "${TEST_WORK_DIR}/outputs")
file(MAKE_DIRECTORY "${TEST_WORK_DIR}/logs")

# Write a minimal config.yaml following cli_basic_smoke.cmake pattern
file(WRITE "${TEST_WORK_DIR}/config.yaml"
"version: 1
data_dir: ${TEST_WORK_DIR}/data
cache_dir: ${TEST_WORK_DIR}/cache
asset_dir: ${TEST_WORK_DIR}/data/assets
template_dir: ${SOURCE_DIR}/templates
profile_dir: ${SOURCE_DIR}/profiles
output_dir: ${TEST_WORK_DIR}/outputs
state_dir: ${TEST_WORK_DIR}/data/state
log_dir: ${TEST_WORK_DIR}/logs
sub_file: ${TEST_WORK_DIR}/data/sub.yaml
profile: bypass-cn
tun: false
log_level: info
parallelism: 4
timeout: 15
retry: 2
fetch_max_bytes: 10485760
templates:
  mihomo:
    normal: ${SOURCE_DIR}/templates/mihomo_base.yaml
    tun: ${SOURCE_DIR}/templates/mihomo_tun.yaml
  sing-box:
    normal: ${SOURCE_DIR}/templates/singbox_base.json
    tun: ${SOURCE_DIR}/templates/singbox_tun.json
  xray:
    normal: ${SOURCE_DIR}/templates/xray_base.json
    tun: ${SOURCE_DIR}/templates/xray_tun.json
grouping:
  region_rules:
    HK: \"(?i)(hong kong|hongkong|hk|香港)\"
    JP: \"(?i)(japan|jp|tokyo|osaka|日本)\"
node_management:
  dedupe: true
  rename_template: \"{name}\"
  sort_by: region,name
"
)

set(CFG_FLAG --config "${TEST_WORK_DIR}/config.yaml")

function(run_cmd NAME EXPECTED_RESULT)
  execute_process(
    COMMAND "${SUBCLI_BIN}" ${CFG_FLAG} ${ARGN}
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

run_cmd(doctor zero doctor --json)
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
