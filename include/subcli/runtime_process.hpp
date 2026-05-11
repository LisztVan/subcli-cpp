#pragma once

#include <filesystem>
#include <string>

#include "subcli/core_runtime.hpp"

namespace subcli {

std::filesystem::path runtimeStatePathForTarget(const std::filesystem::path& stateDir, const std::string& target);

std::filesystem::path runtimeLogPathForTarget(const std::filesystem::path& stateDir, const std::string& target);

bool loadRuntimeState(const std::filesystem::path& statePath, RuntimeStatus& status, std::string& error);

bool saveRuntimeState(const std::filesystem::path& statePath, const RuntimeStatus& status, std::string& error);

bool isRuntimePidRunning(int pid);

bool removeRuntimeStateFile(const std::filesystem::path& statePath, std::string& error);

bool cleanupStaleRuntimeState(
    const std::filesystem::path& stateDir,
    const std::string& target,
    RuntimeStatus* observed,
    std::string& error
);

bool validateRuntimeLaunchBinary(const std::string& binaryPath, std::string& error);

} // namespace subcli
