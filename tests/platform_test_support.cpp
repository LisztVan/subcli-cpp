#include "platform_test_support.hpp"

#include <filesystem>
#include <string>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace subcli {
namespace {

#ifdef _WIN32
std::string system32Binary(const std::string& name) {
    char buffer[MAX_PATH] = {};
    const UINT len = GetSystemDirectoryA(buffer, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        return name;
    }
    return (std::filesystem::path(buffer) / name).string();
}
#endif

} // namespace

ProcessCommand testSupportSuccessCommand() {
#ifdef _WIN32
    return {system32Binary("cmd.exe"), {"/C", "exit", "0"}};
#else
    return {"/bin/sh", {"-c", "exit 0"}};
#endif
}

ProcessCommand testSupportFailureCommand() {
#ifdef _WIN32
    return {system32Binary("cmd.exe"), {"/C", "exit", "7"}};
#else
    return {"/bin/sh", {"-c", "exit 7"}};
#endif
}

ProcessCommand testSupportLongRunningCommand(int seconds) {
#ifdef _WIN32
    return {system32Binary("WindowsPowerShell\\v1.0\\powershell.exe"), {"-NoProfile", "-Command", "Start-Sleep -Seconds " + std::to_string(seconds)}};
#else
    return {"/bin/sleep", {std::to_string(seconds)}};
#endif
}

ProcessCommand testSupportShellCommand(const std::string& script) {
#ifdef _WIN32
    return {system32Binary("WindowsPowerShell\\v1.0\\powershell.exe"), {"-NoProfile", "-Command", script}};
#else
    return {"/bin/sh", {"-c", script}};
#endif
}

std::string testSupportMissingExecutablePath() {
#ifdef _WIN32
    return "C:\\definitely\\missing\\subcli-binary.exe";
#else
    return "/definitely/missing/subcli-binary";
#endif
}

} // namespace subcli
