#include "subcli/core_runtime.hpp"

#include <chrono>
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>
#include <sys/types.h>
#include <sys/wait.h>

#include "subcli/runtime_process.hpp"
#include "subcli/util.hpp"

namespace subcli {

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

    pid_t pid = fork();
    if (pid < 0) {
        error = "failed to fork runtime process";
        return false;
    }

    if (pid == 0) {
        setsid();
        const int devNull = open("/dev/null", O_RDWR);
        if (devNull >= 0) {
            dup2(devNull, STDIN_FILENO);
            dup2(devNull, STDOUT_FILENO);
            dup2(devNull, STDERR_FILENO);
            close(devNull);
        }

        std::vector<char*> argv;
        argv.reserve(args.size() + 2);
        argv.push_back(const_cast<char*>(binaryPath.c_str()));
        for (const auto& arg : args) {
            argv.push_back(const_cast<char*>(arg.c_str()));
        }
        argv.push_back(nullptr);
        execv(binaryPath.c_str(), argv.data());
        _exit(127);
    }

    RuntimeStatus state;
    state.hasState = true;
    state.running = true;
    state.pid = static_cast<int>(pid);
    state.target = target;
    state.binaryPath = binaryPath;
    state.configPath = configPath;
    if (!saveRuntimeState(statePath, state, error)) {
        kill(pid, SIGTERM);
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

    const pid_t pid = static_cast<pid_t>(status.pid);
    if (kill(pid, SIGTERM) != 0 && errno != ESRCH) {
        error = "failed to send SIGTERM";
        return false;
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(std::max(1, timeoutSec));
    while (std::chrono::steady_clock::now() < deadline) {
        if (!isRuntimePidRunning(pid)) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    if (isRuntimePidRunning(pid)) {
        if (kill(pid, SIGKILL) != 0 && errno != ESRCH) {
            error = "failed to send SIGKILL";
            return false;
        }
    }

    int waitStatus = 0;
    (void)waitpid(pid, &waitStatus, WNOHANG);

    return removeRuntimeStateFile(statePath, error);
}

} // namespace subcli
