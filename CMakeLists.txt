cmake_minimum_required(VERSION 3.20)

set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

project(subcli VERSION 0.2.7 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(WIN32)
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

include(GNUInstallDirs)

include(FetchContent)

FetchContent_Declare(
    yaml-cpp
    GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
    GIT_TAG yaml-cpp-0.9.0
)

FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)

FetchContent_Declare(
    CLI11
    GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
    GIT_TAG v2.4.2
)

FetchContent_Declare(
    openssl
    GIT_REPOSITORY https://github.com/janbar/openssl-cmake.git
    GIT_TAG 1.1.1w-20231130
)

FetchContent_Declare(
    curl
    GIT_REPOSITORY https://github.com/curl/curl.git
    GIT_TAG curl-8_10_1
)

set(WITH_APPS OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(YAML_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

if(WIN32)
    set(CURL_USE_OPENSSL OFF CACHE BOOL "" FORCE)
    set(CURL_USE_SCHANNEL ON CACHE BOOL "" FORCE)
else()
    FetchContent_MakeAvailable(openssl)

    if(TARGET ssl AND TARGET crypto)
        add_library(OpenSSL::SSL ALIAS ssl)
        add_library(OpenSSL::Crypto ALIAS crypto)
    endif()

    set(OPENSSL_ROOT_DIR "${openssl_BINARY_DIR}" CACHE PATH "" FORCE)
    set(OPENSSL_INCLUDE_DIR "${openssl_BINARY_DIR}/include" CACHE PATH "" FORCE)
    set(OPENSSL_SSL_LIBRARY "${openssl_BINARY_DIR}/ssl/libssl${CMAKE_STATIC_LIBRARY_SUFFIX}" CACHE FILEPATH "" FORCE)
    set(OPENSSL_CRYPTO_LIBRARY "${openssl_BINARY_DIR}/crypto/libcrypto${CMAKE_STATIC_LIBRARY_SUFFIX}" CACHE FILEPATH "" FORCE)
    set(CURL_USE_OPENSSL ON CACHE BOOL "" FORCE)
    set(CURL_USE_SCHANNEL OFF CACHE BOOL "" FORCE)
endif()

set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(CURL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
set(CURL_ENABLE_EXPORT_TARGET OFF CACHE BOOL "" FORCE)
set(CURL_USE_MBEDTLS OFF CACHE BOOL "" FORCE)
set(CURL_USE_BEARSSL OFF CACHE BOOL "" FORCE)
set(CURL_USE_GNUTLS OFF CACHE BOOL "" FORCE)
set(CURL_USE_RUSTLS OFF CACHE BOOL "" FORCE)
set(CURL_USE_SECTRANSP OFF CACHE BOOL "" FORCE)
set(CURL_ENABLE_SSL ON CACHE BOOL "" FORCE)
set(CURL_USE_LIBPSL OFF CACHE BOOL "" FORCE)
set(CURL_ZSTD OFF CACHE BOOL "" FORCE)
set(CURL_BROTLI OFF CACHE BOOL "" FORCE)
set(USE_NGHTTP2 OFF CACHE BOOL "" FORCE)
set(USE_LIBIDN2 OFF CACHE BOOL "" FORCE)
set(USE_LIBSSH2 OFF CACHE BOOL "" FORCE)
set(HTTP_ONLY ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(yaml-cpp nlohmann_json CLI11 curl)

set(SUBCLI_SOURCES
    src/store.cpp
    src/fetch.cpp
    src/parser.cpp
    src/parser_common.cpp
    src/parser_uri.cpp
    src/parser_mihomo.cpp
    src/parser_singbox.cpp
    src/parser_xray.cpp
    src/exporter.cpp
    src/exporter_common.cpp
    src/exporter_mihomo.cpp
    src/exporter_singbox.cpp
    src/exporter_xray.cpp
    src/profile.cpp
    src/profile_explain.cpp
    src/protocol_registry.cpp
    src/registry.cpp
    src/config_service.cpp
    src/subscription_service.cpp
    src/diagnostic_service.cpp
    src/commands/config_command.cpp
    src/commands/sub_command.cpp
    src/commands/doctor_command.cpp
    src/node.cpp
    src/capabilities.cpp
    src/capability_matrix.cpp
    src/core_discovery.cpp
    src/core_check.cpp
    src/core_runtime.cpp
    src/runtime_process.cpp
    src/daemon_process.cpp
    src/platform_common.cpp
    src/daemon.cpp
    src/logs.cpp
    src/cli_completion.cpp
    src/cli_output.cpp
    src/environment.cpp
    src/assets.cpp
    src/workspace.cpp
    src/tag_utils.cpp
    src/util.cpp
)

if(WIN32)
    list(APPEND SUBCLI_SOURCES src/platform_windows.cpp)
else()
    list(APPEND SUBCLI_SOURCES src/platform_posix.cpp)
endif()

add_executable(subcli src/main.cpp ${SUBCLI_SOURCES})

target_include_directories(subcli PRIVATE include)
target_link_libraries(subcli PRIVATE yaml-cpp nlohmann_json::nlohmann_json CLI11::CLI11 libcurl)
target_compile_definitions(subcli PRIVATE SUBCLI_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

install(TARGETS subcli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY templates/ DESTINATION ${CMAKE_INSTALL_DATADIR}/subcli/templates)
install(DIRECTORY profiles/ DESTINATION ${CMAKE_INSTALL_DATADIR}/subcli/profiles)
install(FILES packaging/systemd/subcli-daemon.service DESTINATION ${CMAKE_INSTALL_DATADIR}/subcli/systemd)
install(FILES README.subcli.md DESTINATION ${CMAKE_INSTALL_DOCDIR})

enable_testing()

add_executable(subcli_tests tests/subcli_tests.cpp tests/platform_test_support.cpp tests/stability_http_server.cpp ${SUBCLI_SOURCES})
target_include_directories(subcli_tests PRIVATE include src tests)
target_link_libraries(subcli_tests PRIVATE yaml-cpp nlohmann_json::nlohmann_json libcurl)
target_compile_definitions(subcli_tests PRIVATE SUBCLI_SOURCE_DIR="${CMAKE_SOURCE_DIR}" SUBCLI_TEST_BINARY_PATH="$<TARGET_FILE:subcli_tests>")
if(WIN32)
    target_link_libraries(subcli_tests PRIVATE ws2_32)
endif()

add_executable(subcli_stability_runner tests/stability_runner.cpp tests/stability_http_server.cpp tests/platform_test_support.cpp ${SUBCLI_SOURCES})
target_include_directories(subcli_stability_runner PRIVATE include src tests)
target_link_libraries(subcli_stability_runner PRIVATE yaml-cpp nlohmann_json::nlohmann_json libcurl)
target_compile_definitions(subcli_stability_runner PRIVATE SUBCLI_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
if(WIN32)
    target_link_libraries(subcli_stability_runner PRIVATE ws2_32)
endif()

add_test(NAME subcli_tests COMMAND subcli_tests)
add_test(
    NAME subcli_cli_basic
    COMMAND ${CMAKE_COMMAND}
        "-DSUBCLI_BIN=$<TARGET_FILE:subcli>"
        "-DTEST_WORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/subcli-cli-basic-$<CONFIG>"
        "-DSOURCE_DIR=${CMAKE_SOURCE_DIR}"
        -P "${CMAKE_SOURCE_DIR}/tests/cli_basic_smoke.cmake"
)
set(SUBCLI_STABILITY_ENV_ROOT "${CMAKE_CURRENT_BINARY_DIR}/subcli-stability-env-$<CONFIG>")
add_test(
    NAME subcli_stability_user_journey
    COMMAND ${CMAKE_COMMAND} -E env
        "--unset=SUBCLI_WORKSPACE"
        "HOME=${SUBCLI_STABILITY_ENV_ROOT}/home"
        "USERPROFILE=${SUBCLI_STABILITY_ENV_ROOT}/home"
        "APPDATA=${SUBCLI_STABILITY_ENV_ROOT}/appdata"
        "LOCALAPPDATA=${SUBCLI_STABILITY_ENV_ROOT}/localappdata"
        "XDG_CONFIG_HOME=${SUBCLI_STABILITY_ENV_ROOT}/xdg-config"
        "XDG_DATA_HOME=${SUBCLI_STABILITY_ENV_ROOT}/xdg-data"
        "XDG_CACHE_HOME=${SUBCLI_STABILITY_ENV_ROOT}/xdg-cache"
        "XDG_STATE_HOME=${SUBCLI_STABILITY_ENV_ROOT}/xdg-state"
        $<TARGET_FILE:subcli_stability_runner>
        --mode user
        --subcli-bin $<TARGET_FILE:subcli>
        --source-dir ${CMAKE_SOURCE_DIR}
        --test-root "${CMAKE_CURRENT_BINARY_DIR}/subcli-stability-user-$<CONFIG>"
)

add_test(
    NAME subcli_stability_package_journey
    COMMAND ${CMAKE_COMMAND}
        "-DBUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}"
        "-DSOURCE_DIR=${CMAKE_SOURCE_DIR}"
        "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/subcli-stability-package-$<CONFIG>"
        "-DSTABILITY_RUNNER=$<TARGET_FILE:subcli_stability_runner>"
        -P "${CMAKE_SOURCE_DIR}/tests/stability_package_journey.cmake"
)

add_test(
    NAME subcli_platform_boundary_scan
    COMMAND ${CMAKE_COMMAND}
        "-DSOURCE_DIR=${CMAKE_SOURCE_DIR}"
        -P "${CMAKE_SOURCE_DIR}/tests/platform_boundary_scan.cmake"
)

if(UNIX)
    add_test(NAME subcli_cli_smoke COMMAND bash ${CMAKE_SOURCE_DIR}/tests/cli_smoke.sh $<TARGET_FILE:subcli>)
endif()

set(CPACK_PACKAGE_NAME "subcli")
set(CPACK_PACKAGE_VENDOR "subcli")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Subscription CLI exporter")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_FILE_NAME "subcli-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
set(CPACK_GENERATOR "TGZ;ZIP")
include(CPack)
