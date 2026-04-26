#pragma once

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
std::vector<std::string> daemonTargetsForExportTarget(const std::string& exportTarget);
int runDaemonCycle(const DaemonOptions& options, const DaemonCallbacks& callbacks);

} // namespace subcli
