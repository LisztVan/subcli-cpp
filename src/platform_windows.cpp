#include "subcli/platform.hpp"

#include <filesystem>
#include <string>

namespace subcli {

std::string currentExecutablePath(const std::string& argv0) {
    if (argv0.empty()) {
        return "";
    }
    std::error_code ec;
    const auto absolute = std::filesystem::absolute(std::filesystem::path(argv0), ec);
    if (ec) {
        return argv0;
    }
    return absolute.lexically_normal().string();
}

bool isExecutablePath(const std::string& path) {
    if (path.empty()) {
        return false;
    }
    std::error_code ec;
    return std::filesystem::is_regular_file(std::filesystem::path(path), ec) && !ec;
}

ProcessRunResult runProcessCapture(const std::string& binaryPath, const std::vector<std::string>&, int) {
    ProcessRunResult result;
    result.error = "Windows process capture is not implemented yet: " + binaryPath;
    return result;
}

int runProcessForeground(const std::string& binaryPath, const std::vector<std::string>&, std::string& error) {
    error = "Windows foreground process launch is not implemented yet: " + binaryPath;
    return 1;
}

BackgroundProcessResult startBackgroundProcess(
    const std::string& binaryPath,
    const std::vector<std::string>&,
    const std::filesystem::path&
) {
    BackgroundProcessResult result;
    result.error = "Windows background process launch is not implemented yet: " + binaryPath;
    return result;
}

bool isProcessRunning(int) {
    return false;
}

bool terminateProcess(int, int, std::string& error) {
    error.clear();
    return true;
}

} // namespace subcli
