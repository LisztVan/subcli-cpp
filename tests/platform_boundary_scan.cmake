cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED SOURCE_DIR OR "${SOURCE_DIR}" STREQUAL "")
    message(FATAL_ERROR "SOURCE_DIR is required")
endif()
get_filename_component(SOURCE_DIR "${SOURCE_DIR}" ABSOLUTE)

set(_tokens
    "#include <windows.h>"
    "#include <winsock2.h>"
    "#include <unistd.h>"
    "#include <sys/wait.h>"
    "#include <sys/socket.h>"
    "CreateProcess"
    "TerminateProcess"
    "WaitForSingleObject"
    "fork("
    "waitpid"
    "kill("
    "socket("
    "bind("
    "listen("
    "accept("
)

set(_allowed
    "src/platform_posix.cpp"
    "src/platform_windows.cpp"
    "tests/platform_test_support.cpp"
    "tests/stability_http_server.cpp"
)

file(GLOB_RECURSE _files
    "${SOURCE_DIR}/include/*.hpp"
    "${SOURCE_DIR}/src/*.cpp"
    "${SOURCE_DIR}/tests/*.cpp"
    "${SOURCE_DIR}/tests/*.hpp"
)

set(_violations "")
foreach(_file IN LISTS _files)
    file(RELATIVE_PATH _rel "${SOURCE_DIR}" "${_file}")
    list(FIND _allowed "${_rel}" _is_allowed)
    if(NOT _is_allowed EQUAL -1)
        continue()
    endif()
    file(READ "${_file}" _content)
    foreach(_token IN LISTS _tokens)
        string(FIND "${_content}" "${_token}" _pos)
        if(NOT _pos EQUAL -1)
            string(APPEND _violations "${_rel}: ${_token}\n")
        endif()
    endforeach()
endforeach()

if(NOT _violations STREQUAL "")
    message(FATAL_ERROR "Platform primitive leakage detected:\n${_violations}")
endif()

message(STATUS "Platform boundary scan passed")
