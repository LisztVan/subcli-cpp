#include "subcli/daemon_process.hpp"

#include <cerrno>
#include <csignal>
#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

namespace subcli {

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

bool isPidRunning(int pid) {
    if (pid <= 0) {
        return false;
    }
#ifdef _WIN32
    return false;
#else
    if (kill(static_cast<pid_t>(pid), 0) == 0) {
        return true;
    }
    return errno != ESRCH;
#endif
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

bool readDaemonStateFile(const std::filesystem::path& stateDir, DaemonProcessStatus& status, std::string& error) {
    error.clear();
    const auto path = daemonStatePath(stateDir);
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec) || ec) {
        status = DaemonProcessStatus();
        return true;
    }

    std::ifstream in(path);
    if (!in) {
        error = "failed to read daemon state file: " + path.string();
        return false;
    }

    nlohmann::json parsed;
    in >> parsed;
    if (!parsed.is_object()) {
        error = "invalid daemon state file";
        return false;
    }

    status = DaemonProcessStatus();
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
    return true;
}

bool writeDaemonStateFile(const std::filesystem::path& stateDir, const DaemonProcessStatus& status, std::string& error) {
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

void removeDaemonStateFile(const std::filesystem::path& stateDir) {
    std::error_code ec;
    std::filesystem::remove(daemonStatePath(stateDir), ec);
}

void removeDaemonPidFile(const std::filesystem::path& stateDir, const DaemonOptions& options) {
    std::error_code ec;
    std::filesystem::remove(configuredDaemonPidPath(stateDir, options), ec);
}

void cleanupDaemonStateAndPid(const std::filesystem::path& stateDir, const DaemonOptions& options) {
    removeDaemonPidFile(stateDir, options);
    removeDaemonStateFile(stateDir);
}

} // namespace subcli
