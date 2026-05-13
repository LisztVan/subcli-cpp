#include "subcli/core_runtime.hpp"

#include <filesystem>
#include <string>
#include <vector>

#include "subcli/platform.hpp"
#include "subcli/runtime_process.hpp"
#include "subcli/util.hpp"

namespace subcli {

namespace {

std::string runtimeBackgroundProcessError(const std::string& platformError) {
    const std::string createLogPrefix = "failed to create log directory:";
    if (platformError.rfind(createLogPrefix, 0) == 0) {
        return "failed to create runtime log directory:" + platformError.substr(createLogPrefix.size());
    }

    const std::string openLogPrefix = "failed to open log file:";
    if (platformError.rfind(openLogPrefix, 0) == 0) {
        return "failed to open runtime log file:" + platformError.substr(openLogPrefix.size());
    }

    return platformError;
}

} // namespace

RuntimeStatus inspectCoreRuntime(const std::filesystem::path& stateDir, const std::string& target, std::string& error) {
    error.clear();
    RuntimeStatus status;
    status.target = target;

    if (!cleanupStaleRuntimeState(stateDir, target, &status, error)) {
        return status;
    }
    if (status.target.empty()) {
        status.target = target;
    }
    return status;
}

bool startCoreRuntime(
    const std::filesystem::path& stateDir,
    const std::string& target,
    const std::string& binaryPath,
    const std::vector<std::string>& args,
    const std::string& configPath,
    const std::string& logPath,
    std::string& error
) {
    error.clear();
    if (target.empty()) {
        error = "runtime target is empty";
        return false;
    }
    if (!validateRuntimeLaunchBinary(binaryPath, error)) {
        return false;
    }

    const auto current = inspectCoreRuntime(stateDir, target, error);
    if (!error.empty()) {
        return false;
    }
    if (current.hasState && current.running) {
        error = "runtime already running for " + target;
        return false;
    }

    const auto statePath = runtimeStatePathForTarget(stateDir, target);
    std::error_code ec;
    std::filesystem::create_directories(statePath.parent_path(), ec);
    if (ec) {
        error = "failed to create runtime state directory: " + ec.message();
        return false;
    }

    const std::filesystem::path resolvedLogPath = logPath.empty() ? runtimeLogPathForTarget(stateDir, target) : std::filesystem::path(logPath);
    const auto logDir = resolvedLogPath.parent_path();
    if (!logDir.empty()) {
        std::error_code logEc;
        std::filesystem::create_directories(logDir, logEc);
        if (logEc) {
            error = "failed to create runtime log directory: " + logEc.message();
            return false;
        }
    }

    const auto started = startBackgroundProcess(binaryPath, args, resolvedLogPath);
    if (!started.started) {
        error = runtimeBackgroundProcessError(started.error);
        return false;
    }

    RuntimeStatus state;
    state.hasState = true;
    state.running = true;
    state.pid = started.pid;
    state.target = target;
    state.binaryPath = binaryPath;
    state.configPath = configPath;
    state.logPath = resolvedLogPath.string();
    state.startedAt = nowIso8601();
    if (!saveRuntimeState(statePath, state, error)) {
        std::string stopError;
        (void)terminateProcess(started.pid, 2, stopError);
        return false;
    }
    return true;
}

bool stopCoreRuntime(const std::filesystem::path& stateDir, const std::string& target, int timeoutSec, std::string& error) {
    error.clear();
    auto status = inspectCoreRuntime(stateDir, target, error);
    if (!error.empty()) {
        return false;
    }
    if (!status.hasState) {
        return true;
    }

    const auto statePath = runtimeStatePathForTarget(stateDir, target);
    if (!status.running || status.pid <= 0) {
        return removeRuntimeStateFile(statePath, error);
    }

    if (!terminateProcess(status.pid, timeoutSec, error)) {
        return false;
    }
    return removeRuntimeStateFile(statePath, error);
}

} // namespace subcli
