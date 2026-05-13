#include "subcli/core_check.hpp"
#include "subcli/platform.hpp"

#include <string>
#include <vector>

namespace subcli {

namespace {

bool isExecutable(const std::string& path) {
    return subcli::isExecutablePath(path);
}

CoreCheckResult runProcess(const std::string& binaryPath, const std::vector<std::string>& args, int timeoutSec) {
    const auto process = runProcessCapture(binaryPath, args, timeoutSec);

    CoreCheckResult result;
    result.ok = process.started && !process.timedOut && process.exitCode == 0;
    result.exitCode = process.exitCode;
    if (!result.ok) {
        if (!process.error.empty()) {
            result.message = process.error;
        } else if (!process.output.empty()) {
            result.message = process.output;
        } else {
            result.message = "command failed with exit code " + std::to_string(process.exitCode);
        }
    }
    return result;
}

} // namespace

CoreCheckResult runMihomoConfigCheck(const std::string& binaryPath, const std::string& configPath, int timeoutSec) {
    if (binaryPath.empty()) {
        return {false, -1, "mihomo binary not found; set core_paths.mihomo or install it in PATH"};
    }
    if (!isExecutable(binaryPath)) {
        return {false, -1, "mihomo binary is not executable: " + binaryPath};
    }
    return runProcess(binaryPath, {"-t", "-f", configPath}, timeoutSec);
}

CoreCheckResult runSingBoxConfigCheck(const std::string& binaryPath, const std::string& configPath, int timeoutSec) {
    if (binaryPath.empty()) {
        return {false, -1, "sing-box binary not found; set core_paths.sing_box or install it in PATH"};
    }
    if (!isExecutable(binaryPath)) {
        return {false, -1, "sing-box binary is not executable: " + binaryPath};
    }
    return runProcess(binaryPath, {"check", "-c", configPath}, timeoutSec);
}

CoreCheckResult runXrayConfigCheck(const std::string& binaryPath, const std::string& configPath, int timeoutSec) {
    if (binaryPath.empty()) {
        return {false, -1, "xray binary not found; set core_paths.xray or install it in PATH"};
    }
    if (!isExecutable(binaryPath)) {
        return {false, -1, "xray binary is not executable: " + binaryPath};
    }
    return runProcess(binaryPath, {"run", "-test", "-config", configPath}, timeoutSec);
}

} // namespace subcli
