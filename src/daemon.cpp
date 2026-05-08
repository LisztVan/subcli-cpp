#include "subcli/daemon.hpp"

#include <chrono>
#include <csignal>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <algorithm>
#include <thread>

#include <fcntl.h>
#include <nlohmann/json.hpp>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "subcli/daemon_process.hpp"
#include "subcli/util.hpp"

namespace subcli {

namespace {

bool updateDaemonCycleSummary(const std::filesystem::path& stateDir, const DaemonOptions& options, int exitCode, const std::string& message) {
    std::string error;
    if (!ensureDaemonDir(stateDir, error)) {
        return false;
    }
    auto state = inspectDaemonProcess(stateDir, error);
    if (!error.empty()) {
        return false;
    }
    state.hasState = true;
    state.intervalSec = options.intervalSec;
    state.exportTarget = options.exportTarget;
    state.options = options;
    state.lastCycleAt = nowIso8601();
    state.lastCycleExitCode = exitCode;
    state.lastCycleMessage = message;
    return writeDaemonStateFile(stateDir, state, error);
}

} // namespace

std::vector<std::string> buildDaemonSubUpdateArgs(const DaemonOptions& options) {
    std::vector<std::string> args = {"sub", "update"};
    if (options.strictNetwork) {
        args.push_back("--strict-network");
    }
    return args;
}

std::vector<std::string> buildDaemonExportArgs(const DaemonOptions& options) {
    std::vector<std::string> args = {"export", options.exportTarget.empty() ? "all" : options.exportTarget};
    if (options.strictNetwork) {
        args.push_back("--strict-network");
    }
    if (options.updateAssets) {
        args.push_back("--download-assets");
    }
    if (options.check) {
        args.push_back("--check");
    }
    return args;
}

std::vector<std::string> buildDaemonRunArgs(const DaemonOptions& options) {
    std::vector<std::string> args = {
        "daemon",
        "run",
        "--interval",
        std::to_string(options.intervalSec > 0 ? options.intervalSec : 3600),
        "--target",
        options.exportTarget.empty() ? "all" : options.exportTarget,
    };
    if (options.updateAssets) {
        args.push_back("--update-assets");
    }
    if (options.strictNetwork) {
        args.push_back("--strict-network");
    }
    if (options.check) {
        args.push_back("--check");
    }
    if (!options.restartRunning) {
        args.push_back("--no-restart");
    }
    if (!options.pidFile.empty()) {
        args.push_back("--pid-file");
        args.push_back(options.pidFile);
    }
    if (!options.logFile.empty()) {
        args.push_back("--log-file");
        args.push_back(options.logFile);
    }
    return args;
}

std::vector<std::string> daemonTargetsForExportTarget(const std::string& exportTarget) {
    if (exportTarget == "mihomo" || exportTarget == "sing-box" || exportTarget == "xray") {
        return {exportTarget};
    }
    return {"mihomo", "sing-box", "xray"};
}

int runDaemonCycle(const DaemonOptions& options, const DaemonCallbacks& callbacks) {
    if (!callbacks.runSubCommand || !callbacks.runExportCommand || !callbacks.isCoreRunning || !callbacks.runRestartCommand) {
        return 1;
    }

    const auto subArgs = buildDaemonSubUpdateArgs(options);
    const int subRc = callbacks.runSubCommand(subArgs);
    if (subRc != 0) {
        return subRc;
    }

    const auto exportArgs = buildDaemonExportArgs(options);
    const int exportRc = callbacks.runExportCommand(exportArgs);
    if (exportRc != 0) {
        return exportRc;
    }

    if (!options.restartRunning) {
        return 0;
    }

    for (const auto& target : daemonTargetsForExportTarget(options.exportTarget)) {
        std::string error;
        const bool running = callbacks.isCoreRunning(target, error);
        if (!error.empty()) {
            return 1;
        }
        if (!running) {
            continue;
        }
        const int restartRc = callbacks.runRestartCommand({"restart", target});
        if (restartRc != 0) {
            return restartRc;
        }
    }

    return 0;
}

DaemonProcessStatus inspectDaemonProcess(const std::filesystem::path& stateDir, std::string& error) {
    error.clear();
    DaemonProcessStatus status;

    if (!readDaemonStateFile(stateDir, status, error)) {
        return status;
    }
    status.running = isPidRunning(status.pid);
    return status;
}

bool startDaemonProcess(
    const std::filesystem::path& stateDir,
    const std::string& binaryPath,
    const std::vector<std::string>& processArgs,
    const DaemonOptions& options,
    std::string& error
) {
    error.clear();
    if (binaryPath.empty()) {
        error = "daemon binary path is empty";
        return false;
    }
    if (!ensureDaemonDir(stateDir, error)) {
        return false;
    }

    const auto current = inspectDaemonProcess(stateDir, error);
    if (!error.empty()) {
        return false;
    }
    if (current.hasState && current.running) {
        error = "daemon already running";
        return false;
    }
    if (current.hasState && !current.running) {
        cleanupDaemonStateAndPid(stateDir, current.options);
    }

    const auto logPath = configuredDaemonLogPath(stateDir, options);
    const auto logDir = logPath.parent_path();
    if (!logDir.empty()) {
        std::error_code logEc;
        std::filesystem::create_directories(logDir, logEc);
        if (logEc) {
            error = "failed to create daemon log dir: " + logEc.message();
            return false;
        }
    }
    const int logFd = open(logPath.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (logFd < 0) {
        error = "failed to open daemon log file: " + logPath.string();
        return false;
    }

    int pipeFd[2] = {-1, -1};
    if (pipe(pipeFd) != 0) {
        close(logFd);
        error = "failed to create daemon handshake pipe";
        return false;
    }
    const int flags = fcntl(pipeFd[1], F_GETFD);
    if (flags >= 0) {
        (void)fcntl(pipeFd[1], F_SETFD, flags | FD_CLOEXEC);
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(pipeFd[0]);
        close(pipeFd[1]);
        close(logFd);
        error = "failed to fork daemon process";
        return false;
    }

    if (pid == 0) {
        close(pipeFd[0]);
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
        argv.reserve(processArgs.size() + 2);
        argv.push_back(const_cast<char*>(binaryPath.c_str()));
        for (const auto& arg : processArgs) {
            argv.push_back(const_cast<char*>(arg.c_str()));
        }
        argv.push_back(nullptr);
        execv(binaryPath.c_str(), argv.data());
        const int execErr = errno;
        (void)write(pipeFd[1], &execErr, sizeof(execErr));
        close(pipeFd[1]);
        _exit(127);
    }

    close(logFd);
    close(pipeFd[1]);
    int execErr = 0;
    const ssize_t readCount = read(pipeFd[0], &execErr, sizeof(execErr));
    close(pipeFd[0]);
    if (readCount > 0) {
        int waitStatus = 0;
        (void)waitpid(pid, &waitStatus, 0);
        error = "failed to exec daemon process: " + std::string(std::strerror(execErr));
        return false;
    }

    DaemonProcessStatus state;
    state.hasState = true;
    state.running = true;
    state.pid = static_cast<int>(pid);
    state.binaryPath = binaryPath;
    state.intervalSec = options.intervalSec;
    state.exportTarget = options.exportTarget;
    state.options = options;
    if (!writeDaemonStateFile(stateDir, state, error)) {
        kill(pid, SIGTERM);
        return false;
    }
    if (!writeDaemonPidFile(stateDir, state, error)) {
        kill(pid, SIGTERM);
        removeDaemonStateFile(stateDir);
        return false;
    }
    return true;
}

bool stopDaemonProcess(const std::filesystem::path& stateDir, int timeoutSec, std::string& error) {
    error.clear();
    const auto state = inspectDaemonProcess(stateDir, error);
    if (!error.empty()) {
        return false;
    }
    if (!state.hasState) {
        return true;
    }
    if (!state.running || state.pid <= 0) {
        cleanupDaemonStateAndPid(stateDir, state.options);
        return true;
    }

    const pid_t pid = static_cast<pid_t>(state.pid);
    if (kill(pid, SIGTERM) != 0 && errno != ESRCH) {
        error = "failed to send SIGTERM to daemon";
        return false;
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(std::max(1, timeoutSec));
    while (std::chrono::steady_clock::now() < deadline) {
        if (!isPidRunning(static_cast<int>(pid))) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    if (isPidRunning(static_cast<int>(pid))) {
        if (kill(pid, SIGKILL) != 0 && errno != ESRCH) {
            error = "failed to send SIGKILL to daemon";
            return false;
        }
    }

    int waitStatus = 0;
    (void)waitpid(pid, &waitStatus, WNOHANG);
    cleanupDaemonStateAndPid(stateDir, state.options);
    return true;
}

int runDaemonCycleWithState(const std::filesystem::path& stateDir, const DaemonOptions& options, const DaemonCallbacks& callbacks) {
    if (!callbacks.runSubCommand || !callbacks.runExportCommand || !callbacks.isCoreRunning || !callbacks.runRestartCommand) {
        updateDaemonCycleSummary(stateDir, options, 1, "invalid callbacks");
        return 1;
    }

    const auto subArgs = buildDaemonSubUpdateArgs(options);
    const int subRc = callbacks.runSubCommand(subArgs);
    if (subRc != 0) {
        updateDaemonCycleSummary(stateDir, options, subRc, "sub update failed");
        return subRc;
    }

    const auto exportArgs = buildDaemonExportArgs(options);
    const int exportRc = callbacks.runExportCommand(exportArgs);
    if (exportRc != 0) {
        updateDaemonCycleSummary(stateDir, options, exportRc, "export failed");
        return exportRc;
    }

    if (options.restartRunning) {
        for (const auto& target : daemonTargetsForExportTarget(options.exportTarget)) {
            std::string error;
            const bool running = callbacks.isCoreRunning(target, error);
            if (!error.empty()) {
                updateDaemonCycleSummary(stateDir, options, 1, "restart status check failed");
                return 1;
            }
            if (!running) {
                continue;
            }
            const int restartRc = callbacks.runRestartCommand({"restart", target});
            if (restartRc != 0) {
                updateDaemonCycleSummary(stateDir, options, restartRc, "restart failed");
                return restartRc;
            }
        }
    }

    updateDaemonCycleSummary(stateDir, options, 0, "ok");
    return 0;
}

} // namespace subcli
