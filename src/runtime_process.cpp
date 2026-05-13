#include "subcli/runtime_process.hpp"

#include <nlohmann/json.hpp>
#include <system_error>

#include "subcli/platform.hpp"
#include "subcli/util.hpp"

namespace subcli {

namespace {

std::string normalizeTarget(std::string target) {
    for (char& ch : target) {
        if (ch == '-') {
            ch = '_';
        }
    }
    return target;
}

} // namespace

std::filesystem::path runtimeStatePathForTarget(const std::filesystem::path& stateDir, const std::string& target) {
    return stateDir / "runtime" / (normalizeTarget(target) + ".json");
}

std::filesystem::path runtimeLogPathForTarget(const std::filesystem::path& stateDir, const std::string& target) {
    return stateDir / "runtime" / (normalizeTarget(target) + ".log");
}

bool loadRuntimeState(const std::filesystem::path& statePath, RuntimeStatus& status, std::string& error) {
    error.clear();
    status = RuntimeStatus{};
    if (!fileExists(statePath.string())) {
        return true;
    }

    const auto parsed = nlohmann::json::parse(readFile(statePath.string()), nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) {
        error = "invalid runtime state file: " + statePath.string();
        return false;
    }

    const int pid = parsed.value("pid", 0);
    const std::string target = parsed.value("target", "");
    const std::string binaryPath = parsed.value("binary_path", "");
    const std::string configPath = parsed.value("config_path", "");
    const std::string logPath = parsed.value("log_path", "");
    const std::string startedAt = parsed.value("started_at", "");
    if (pid < 0 || binaryPath.empty()) {
        error = "invalid runtime state file: " + statePath.string();
        return false;
    }

    status.hasState = true;
    status.pid = pid;
    status.target = target;
    status.binaryPath = binaryPath;
    status.configPath = configPath;
    status.logPath = logPath;
    status.startedAt = startedAt;
    status.running = isRuntimePidRunning(pid);
    return true;
}

bool saveRuntimeState(const std::filesystem::path& statePath, const RuntimeStatus& status, std::string& error) {
    error.clear();
    nlohmann::json state = {
        {"pid", status.pid},
        {"target", status.target},
        {"binary_path", status.binaryPath},
        {"config_path", status.configPath},
        {"log_path", status.logPath},
        {"started_at", status.startedAt},
    };
    return writeFile(statePath.string(), state.dump(2), error);
}

bool isRuntimePidRunning(int pid) {
    return isProcessRunning(pid);
}

bool removeRuntimeStateFile(const std::filesystem::path& statePath, std::string& error) {
    error.clear();
    std::error_code ec;
    std::filesystem::remove(statePath, ec);
    if (ec) {
        error = "failed to remove runtime state file: " + ec.message();
        return false;
    }
    return true;
}

bool cleanupStaleRuntimeState(
    const std::filesystem::path& stateDir,
    const std::string& target,
    RuntimeStatus* observed,
    std::string& error
) {
    error.clear();
    RuntimeStatus state;
    state.target = target;
    const auto statePath = runtimeStatePathForTarget(stateDir, target);
    if (!loadRuntimeState(statePath, state, error)) {
        return false;
    }
    if (state.hasState && !state.running) {
        if (!removeRuntimeStateFile(statePath, error)) {
            return false;
        }
        state.hasState = false;
        state.running = false;
        state.pid = 0;
        state.binaryPath.clear();
        state.configPath.clear();
        state.logPath.clear();
        state.startedAt.clear();
    }
    if (observed != nullptr) {
        *observed = state;
    }
    return true;
}

bool validateRuntimeLaunchBinary(const std::string& binaryPath, std::string& error) {
    error.clear();
    if (binaryPath.empty()) {
        error = "runtime binary path is empty";
        return false;
    }
    if (!isExecutablePath(binaryPath)) {
        error = "runtime binary path is not executable: " + binaryPath;
        return false;
    }
    return true;
}

} // namespace subcli
