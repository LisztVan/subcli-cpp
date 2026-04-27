#include "subcli/daemon.hpp"

#include <chrono>
#include <csignal>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <thread>

#include <fcntl.h>
#include <nlohmann/json.hpp>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "subcli/util.hpp"

namespace subcli {

namespace {

std::filesystem::path daemonStatePath(const std::filesystem::path& stateDir) {
    return stateDir / "daemon" / "daemon.json";
}

std::filesystem::path defaultDaemonPidPath(const std::filesystem::path& stateDir) {
    return stateDir / "daemon" / "daemon.pid";
}

std::filesystem::path daemonLogPath(const std::filesystem::path& stateDir) {
    return stateDir / "daemon" / "daemon.log";
}

std::filesystem::path configuredDaemonPidPath(const std::filesystem::path& stateDir, const DaemonOptions& options) {
    return options.pidFile.empty() ? defaultDaemonPidPath(stateDir) : std::filesystem::path(options.pidFile);
}

std::filesystem::path configuredDaemonLogPath(const std::filesystem::path& stateDir, const DaemonOptions& options) {
    return options.logFile.empty() ? daemonLogPath(stateDir) : std::filesystem::path(options.logFile);
}

bool isPidRunning(pid_t pid) {
    if (pid <= 0) {
        return false;
    }
    if (kill(pid, 0) == 0) {
        return true;
    }
    return errno != ESRCH;
}

bool ensureDaemonDir(const std::filesystem::path& stateDir, std::string& error) {
    std::error_code ec;
    std::filesystem::create_directories((stateDir / "daemon"), ec);
    if (ec) {
        error = "failed to create daemon state dir: " + ec.message();
        return false;
    }
    return true;
}

bool writeDaemonState(const std::filesystem::path& stateDir, const DaemonProcessStatus& status, std::string& error) {
    const nlohmann::json state = {
        {"pid", status.pid},
        {"binary_path", status.binaryPath},
        {"interval_sec", status.options.intervalSec},
        {"export_target", status.options.exportTarget},
        {"update_assets", status.options.updateAssets},
        {"strict_network", status.options.strictNetwork},
        {"check", status.options.check},
        {"restart_running", status.options.restartRunning},
        {"pid_file", status.options.pidFile},
        {"log_file", status.options.logFile},
        {"last_cycle_at", status.lastCycleAt},
        {"last_cycle_exit_code", status.lastCycleExitCode},
        {"last_cycle_message", status.lastCycleMessage},
    };
    const auto path = daemonStatePath(stateDir);
    std::ofstream out(path);
    if (!out) {
        error = "failed to write daemon state file: " + path.string();
        return false;
    }
    out << state.dump(2);
    return true;
}

bool writeDaemonPidFile(const std::filesystem::path& stateDir, const DaemonProcessStatus& status, std::string& error) {
    const auto pidPath = configuredDaemonPidPath(stateDir, status.options);
    std::error_code ec;
    std::filesystem::create_directories(pidPath.parent_path(), ec);
    if (ec) {
        error = "failed to create daemon pid dir: " + ec.message();
        return false;
    }
    std::ofstream out(pidPath);
    if (!out) {
        error = "failed to write daemon pid file: " + pidPath.string();
        return false;
    }
    out << status.pid;
    return true;
}

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
    return writeDaemonState(stateDir, state, error);
}

void removeDaemonState(const std::filesystem::path& stateDir) {
    std::error_code ec;
    std::filesystem::remove(daemonStatePath(stateDir), ec);
}

void removeDaemonPidFile(const std::filesystem::path& stateDir, const DaemonOptions& options) {
    std::error_code ec;
    std::filesystem::remove(configuredDaemonPidPath(stateDir, options), ec);
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

    const auto path = daemonStatePath(stateDir);
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec) || ec) {
        return status;
    }

    std::ifstream in(path);
    if (!in) {
        error = "failed to read daemon state file: " + path.string();
        return status;
    }

    nlohmann::json parsed;
    in >> parsed;
    if (!parsed.is_object()) {
        error = "invalid daemon state file";
        return status;
    }

    status.hasState = true;
    status.pid = parsed.value("pid", 0);
    status.binaryPath = parsed.value("binary_path", "");
    status.intervalSec = parsed.value("interval_sec", 3600);
    status.exportTarget = parsed.value("export_target", "all");
    status.options.intervalSec = status.intervalSec;
    status.options.exportTarget = status.exportTarget;
    status.options.updateAssets = parsed.value("update_assets", false);
    status.options.strictNetwork = parsed.value("strict_network", false);
    status.options.check = parsed.value("check", false);
    status.options.restartRunning = parsed.value("restart_running", true);
    status.options.pidFile = parsed.value("pid_file", "");
    status.options.logFile = parsed.value("log_file", "");
    status.lastCycleAt = parsed.value("last_cycle_at", "");
    status.lastCycleExitCode = parsed.value("last_cycle_exit_code", 0);
    status.lastCycleMessage = parsed.value("last_cycle_message", "");
    status.running = isPidRunning(static_cast<pid_t>(status.pid));
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
        removeDaemonState(stateDir);
    }

    int pipeFd[2] = {-1, -1};
    if (pipe(pipeFd) != 0) {
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

        const auto logPath = configuredDaemonLogPath(stateDir, options);
        std::error_code logEc;
        std::filesystem::create_directories(logPath.parent_path(), logEc);
        const int logFd = open(logPath.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (logFd >= 0) {
            dup2(logFd, STDOUT_FILENO);
            dup2(logFd, STDERR_FILENO);
            close(logFd);
        }

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
    if (!writeDaemonState(stateDir, state, error)) {
        kill(pid, SIGTERM);
        return false;
    }
    if (!writeDaemonPidFile(stateDir, state, error)) {
        kill(pid, SIGTERM);
        removeDaemonState(stateDir);
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
        removeDaemonPidFile(stateDir, state.options);
        removeDaemonState(stateDir);
        return true;
    }

    const pid_t pid = static_cast<pid_t>(state.pid);
    if (kill(pid, SIGTERM) != 0 && errno != ESRCH) {
        error = "failed to send SIGTERM to daemon";
        return false;
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(std::max(1, timeoutSec));
    while (std::chrono::steady_clock::now() < deadline) {
        if (!isPidRunning(pid)) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    if (isPidRunning(pid)) {
        if (kill(pid, SIGKILL) != 0 && errno != ESRCH) {
            error = "failed to send SIGKILL to daemon";
            return false;
        }
    }

    int waitStatus = 0;
    (void)waitpid(pid, &waitStatus, WNOHANG);
    removeDaemonPidFile(stateDir, state.options);
    removeDaemonState(stateDir);
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
