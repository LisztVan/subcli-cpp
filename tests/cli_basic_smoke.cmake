cmake_minimum_required(VERSION 3.20)

foreach(_required_var IN ITEMS SUBCLI_BIN TEST_WORK_DIR SOURCE_DIR)
    if(NOT DEFINED ${_required_var} OR "${${_required_var}}" STREQUAL "")
        message(FATAL_ERROR "${_required_var} is required")
    endif()
endforeach()

get_filename_component(SUBCLI_BIN "${SUBCLI_BIN}" ABSOLUTE)
get_filename_component(TEST_WORK_DIR "${TEST_WORK_DIR}" ABSOLUTE)
get_filename_component(SOURCE_DIR "${SOURCE_DIR}" ABSOLUTE)

if(NOT EXISTS "${SUBCLI_BIN}")
    message(FATAL_ERROR "SUBCLI_BIN does not exist: ${SUBCLI_BIN}")
endif()
if(NOT IS_DIRECTORY "${SOURCE_DIR}")
    message(FATAL_ERROR "SOURCE_DIR is not a directory: ${SOURCE_DIR}")
endif()
if(NOT IS_DIRECTORY "${SOURCE_DIR}/templates")
    message(FATAL_ERROR "SOURCE_DIR/templates is missing: ${SOURCE_DIR}/templates")
endif()
if(NOT EXISTS "${SOURCE_DIR}/profiles/bypass-cn.json")
    message(FATAL_ERROR "SOURCE_DIR/profiles/bypass-cn.json is missing")
endif()

set(_smoke_root "${TEST_WORK_DIR}.env")
file(REMOVE_RECURSE "${TEST_WORK_DIR}" "${_smoke_root}")
file(MAKE_DIRECTORY
    "${_smoke_root}"
    "${_smoke_root}/home"
    "${_smoke_root}/appdata"
    "${_smoke_root}/localappdata"
    "${_smoke_root}/xdg-config"
    "${_smoke_root}/xdg-data"
    "${_smoke_root}/xdg-cache"
    "${_smoke_root}/xdg-state"
)

set(_subcli_env
    "--unset=SUBCLI_WORKSPACE"
    "HOME=${_smoke_root}/home"
    "USERPROFILE=${_smoke_root}/home"
    "APPDATA=${_smoke_root}/appdata"
    "LOCALAPPDATA=${_smoke_root}/localappdata"
    "XDG_CONFIG_HOME=${_smoke_root}/xdg-config"
    "XDG_DATA_HOME=${_smoke_root}/xdg-data"
    "XDG_CACHE_HOME=${_smoke_root}/xdg-cache"
    "XDG_STATE_HOME=${_smoke_root}/xdg-state"
)

function(ensure_config)
    if(EXISTS "${TEST_WORK_DIR}/config.yaml")
        return()
    endif()
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
assets:
  paths:
    xray.geosite: ${TEST_WORK_DIR}/data/assets/xray/geosite.dat
    xray.geoip: ${TEST_WORK_DIR}/data/assets/xray/geoip.dat
  urls:
    xray.geosite: file:///dev/null
    xray.geoip: file:///dev/null
grouping:
  region_rules:
    HK: "(?i)(hong kong|hongkong|hk|香港)"
    JP: "(?i)(japan|jp|tokyo|osaka|日本)"
node_management:
  dedupe: true
  rename_template: "{name}"
  sort_by: region,name
"
    )
endfunction()

function(run_subcli _label)
    ensure_config()
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env ${_subcli_env} "${SUBCLI_BIN}" --config "${TEST_WORK_DIR}/config.yaml" ${ARGN}
        WORKING_DIRECTORY "${_smoke_root}"
        RESULT_VARIABLE _result
        OUTPUT_VARIABLE _stdout
        ERROR_VARIABLE _stderr
    )
    if(NOT _result STREQUAL "0")
        string(JOIN " " _args ${ARGN})
        message(FATAL_ERROR
            "subcli CLI smoke failed: ${_label}\n"
            "Command: ${SUBCLI_BIN} ${_args}\n"
            "Exit code: ${_result}\n"
            "stdout:\n${_stdout}\n"
            "stderr:\n${_stderr}"
        )
    endif()
endfunction()

function(run_subcli_capture _label _stdout_var)
    ensure_config()
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env ${_subcli_env} "${SUBCLI_BIN}" --config "${TEST_WORK_DIR}/config.yaml" ${ARGN}
        WORKING_DIRECTORY "${_smoke_root}"
        RESULT_VARIABLE _result
        OUTPUT_VARIABLE _stdout
        ERROR_VARIABLE _stderr
    )
    if(NOT _result STREQUAL "0")
        string(JOIN " " _args ${ARGN})
        message(FATAL_ERROR
            "subcli CLI smoke failed: ${_label}\n"
            "Command: ${SUBCLI_BIN} ${_args}\n"
            "Exit code: ${_result}\n"
            "stdout:\n${_stdout}\n"
            "stderr:\n${_stderr}"
        )
    endif()
    set(${_stdout_var} "${_stdout}" PARENT_SCOPE)
endfunction()

run_subcli("root help" --help)
run_subcli("profile validate" profile validate "${SOURCE_DIR}/profiles/bypass-cn.json")
