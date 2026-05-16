cmake_minimum_required(VERSION 3.20)

foreach(_required_var IN ITEMS BUILD_DIR SOURCE_DIR TEST_ROOT STABILITY_RUNNER)
    if(NOT DEFINED ${_required_var} OR "${${_required_var}}" STREQUAL "")
        message(FATAL_ERROR "${_required_var} is required")
    endif()
endforeach()

get_filename_component(BUILD_DIR "${BUILD_DIR}" ABSOLUTE)
get_filename_component(SOURCE_DIR "${SOURCE_DIR}" ABSOLUTE)
get_filename_component(TEST_ROOT "${TEST_ROOT}" ABSOLUTE)
get_filename_component(STABILITY_RUNNER "${STABILITY_RUNNER}" ABSOLUTE)

file(GLOB _packages "${BUILD_DIR}/subcli-*.zip" "${BUILD_DIR}/subcli-*.tar.gz")
list(SORT _packages)
if(NOT _packages)
    message(FATAL_ERROR "No CPack package found in ${BUILD_DIR}. Build the package target before running this test.")
endif()
list(GET _packages 0 _package)

set(_extract_dir "${TEST_ROOT}/extract")
set(_env_root "${TEST_ROOT}/env")
file(REMOVE_RECURSE "${TEST_ROOT}")
file(MAKE_DIRECTORY "${_extract_dir}" "${_env_root}")

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E tar xzf "${_package}"
    WORKING_DIRECTORY "${_extract_dir}"
    RESULT_VARIABLE _extract_result
    OUTPUT_VARIABLE _extract_stdout
    ERROR_VARIABLE _extract_stderr
)
if(NOT _extract_result STREQUAL "0")
    message(FATAL_ERROR "Failed to extract ${_package}\nstdout:\n${_extract_stdout}\nstderr:\n${_extract_stderr}")
endif()

file(GLOB_RECURSE _subcli_bins "${_extract_dir}/subcli" "${_extract_dir}/subcli.exe")
list(SORT _subcli_bins)
if(NOT _subcli_bins)
    message(FATAL_ERROR "No subcli executable found after extracting ${_package}")
endif()
list(GET _subcli_bins 0 _subcli_bin)

if(UNIX)
    execute_process(COMMAND chmod +x "${_subcli_bin}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
        "--unset=SUBCLI_WORKSPACE"
        "HOME=${_env_root}/home"
        "USERPROFILE=${_env_root}/home"
        "APPDATA=${_env_root}/appdata"
        "LOCALAPPDATA=${_env_root}/localappdata"
        "XDG_CONFIG_HOME=${_env_root}/xdg-config"
        "XDG_DATA_HOME=${_env_root}/xdg-data"
        "XDG_CACHE_HOME=${_env_root}/xdg-cache"
        "XDG_STATE_HOME=${_env_root}/xdg-state"
        "${STABILITY_RUNNER}"
        --mode package
        --subcli-bin "${_subcli_bin}"
        --source-dir "${SOURCE_DIR}"
        --test-root "${TEST_ROOT}/runner"
    RESULT_VARIABLE _result
    OUTPUT_VARIABLE _stdout
    ERROR_VARIABLE _stderr
)
if(NOT _result STREQUAL "0")
    message(FATAL_ERROR "Package journey failed\nPackage: ${_package}\nBinary: ${_subcli_bin}\nstdout:\n${_stdout}\nstderr:\n${_stderr}")
endif()

message(STATUS "Package journey passed for ${_package}")
