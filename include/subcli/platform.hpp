#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace subcli {

struct ProcessCommand {
    std::string binary;
    std::vector<std::string> args;
};

struct ProcessRunResult {
    bool started = false;
    bool timedOut = false;
    int exitCode = -1;
    std::string output;
    std::string error;
};

struct BackgroundProcessResult {
    bool started = false;
    int pid = 0;
    std::string error;
};

std::string currentExecutablePath(const std::string& argv0);
bool isExecutablePath(const std::string& path);
std::string findExecutableOnPath(const std::string& name);

ProcessRunResult runProcessCapture(const std::string& binaryPath, const std::vector<std::string>& args, int timeoutSec);
int runProcessForeground(const std::string& binaryPath, const std::vector<std::string>& args, std::string& error);
BackgroundProcessResult startBackgroundProcess(
    const std::string& binaryPath,
    const std::vector<std::string>& args,
    const std::filesystem::path& logPath
);
bool isProcessRunning(int pid);
bool terminateProcess(int pid, int timeoutSec, std::string& error);

std::string platformPathListSeparator();
std::vector<std::string> executableNameCandidates(const std::string& name);

} // namespace subcli
