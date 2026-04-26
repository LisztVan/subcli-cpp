#include "subcli/daemon.hpp"

namespace subcli {

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

} // namespace subcli
