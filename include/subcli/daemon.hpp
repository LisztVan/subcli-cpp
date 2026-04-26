#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace subcli {

struct DaemonOptions {
    int intervalSec = 3600;
    bool updateAssets = false;
    bool strictNetwork = false;
    bool check = false;
    bool restartRunning = true;
    std::string exportTarget = "all";
};

struct DaemonCallbacks {
    std::function<int(const std::vector<std::string>&)> runSubCommand;
    std::function<int(const std::vector<std::string>&)> runExportCommand;
    std::function<bool(const std::string&, std::string&)> isCoreRunning;
    std::function<int(const std::vector<std::string>&)> runRestartCommand;
};

std::vector<std::string> buildDaemonSubUpdateArgs(const DaemonOptions& options);
std::vector<std::string> buildDaemonExportArgs(const DaemonOptions& options);
std::vector<std::string> buildDaemonRunArgs(const DaemonOptions& options);
std::vector<std::string> daemonTargetsForExportTarget(const std::string& exportTarget);
int runDaemonCycle(const DaemonOptions& options, const DaemonCallbacks& callbacks);

struct DaemonProcessStatus {
    bool hasState = false;
    bool running = false;
    int pid = 0;
    std::string binaryPath;
    int intervalSec = 3600;
    std::string exportTarget = "all";
    std::string lastCycleAt;
    int lastCycleExitCode = 0;
    std::string lastCycleMessage;
    DaemonOptions options;
};

bool startDaemonProcess(
    const std::filesystem::path& stateDir,
    const std::string& binaryPath,
    const std::vector<std::string>& processArgs,
    const DaemonOptions& options,
    std::string& error
);

DaemonProcessStatus inspectDaemonProcess(const std::filesystem::path& stateDir, std::string& error);
bool stopDaemonProcess(const std::filesystem::path& stateDir, int timeoutSec, std::string& error);
int runDaemonCycleWithState(const std::filesystem::path& stateDir, const DaemonOptions& options, const DaemonCallbacks& callbacks);

} // namespace subcli
