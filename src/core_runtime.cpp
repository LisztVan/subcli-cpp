#include "subcli/core_runtime.hpp"

#include <chrono>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <filesystem>
#ifndef _WIN32
#include <fcntl.h>
#include <thread>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#endif
#include <string>
#include <vector>

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
    const std::string& logPath,
    std::string& error
) {
    error.clear();
#ifdef _WIN32
    (void)stateDir;
    (void)target;
    (void)binaryPath;
    (void)args;
    (void)configPath;
    (void)logPath;
    error = "managed runtime processes are not supported on Windows";
    return false;
#else
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

    const int logFd = open(resolvedLogPath.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (logFd < 0) {
        error = "failed to open runtime log file: " + resolvedLogPath.string() + ": " + std::strerror(errno);
        return false;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(logFd);
        error = "failed to fork runtime process";
        return false;
    }

    if (pid == 0) {
        setsid();
        const int stdinFd = open("/dev/null", O_RDONLY);
        if (stdinFd >= 0) {
            dup2(stdinFd, STDIN_FILENO);
            close(stdinFd);
        }

        dup2(logFd, STDOUT_FILENO);
        dup2(logFd, STDERR_FILENO);
        close(logFd);

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
    close(logFd);

    RuntimeStatus state;
    state.hasState = true;
    state.running = true;
    state.pid = static_cast<int>(pid);
    state.target = target;
    state.binaryPath = binaryPath;
    state.configPath = configPath;
    state.logPath = resolvedLogPath.string();
    state.startedAt = nowIso8601();
    if (!saveRuntimeState(statePath, state, error)) {
        kill(pid, SIGTERM);
        return false;
    }
    return true;
#endif
}

bool stopCoreRuntime(const std::filesystem::path& stateDir, const std::string& target, int timeoutSec, std::string& error) {
    error.clear();
#ifdef _WIN32
    (void)timeoutSec;
    auto status = inspectCoreRuntime(stateDir, target, error);
    if (!error.empty()) {
        return false;
    }
    if (!status.hasState) {
        return true;
    }
    return removeRuntimeStateFile(runtimeStatePathForTarget(stateDir, target), error);
#else
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
#endif
}

} // namespace subcli
