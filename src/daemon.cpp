#include "subcli/daemon.hpp"

#include <filesystem>
#include <string>
#include <vector>

#include "subcli/daemon_process.hpp"
#include "subcli/platform.hpp"
#include "subcli/util.hpp"

namespace subcli {

namespace {

std::string daemonBackgroundProcessError(const std::string& platformError) {
    const std::string createLogPrefix = "failed to create log directory:";
    if (platformError.rfind(createLogPrefix, 0) == 0) {
        return "failed to create daemon log dir:" + platformError.substr(createLogPrefix.size());
    }

    const std::string openLogPrefix = "failed to open log file:";
    if (platformError.rfind(openLogPrefix, 0) == 0) {
        return "failed to open daemon log file:" + platformError.substr(openLogPrefix.size());
    }

    return platformError;
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

    const auto started = startBackgroundProcess(binaryPath, processArgs, logPath);
    if (!started.started) {
        error = daemonBackgroundProcessError(started.error);
        return false;
    }

    DaemonProcessStatus state;
    state.hasState = true;
    state.running = true;
    state.pid = started.pid;
    state.binaryPath = binaryPath;
    state.intervalSec = options.intervalSec;
    state.exportTarget = options.exportTarget;
    state.options = options;
    if (!writeDaemonStateFile(stateDir, state, error)) {
        std::string stopError;
        (void)terminateProcess(started.pid, 2, stopError);
        removeDaemonStateFile(stateDir);
        return false;
    }
    if (!writeDaemonPidFile(stateDir, state, error)) {
        std::string stopError;
        (void)terminateProcess(started.pid, 2, stopError);
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

    if (!terminateProcess(state.pid, timeoutSec, error)) {
        return false;
    }

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
