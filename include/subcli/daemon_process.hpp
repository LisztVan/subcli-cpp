#pragma once

#include <filesystem>
#include <string>

#include "subcli/daemon.hpp"

namespace subcli {

std::filesystem::path daemonStatePath(const std::filesystem::path& stateDir);
std::filesystem::path defaultDaemonPidPath(const std::filesystem::path& stateDir);
std::filesystem::path daemonLogPath(const std::filesystem::path& stateDir);
std::filesystem::path configuredDaemonPidPath(const std::filesystem::path& stateDir, const DaemonOptions& options);
std::filesystem::path configuredDaemonLogPath(const std::filesystem::path& stateDir, const DaemonOptions& options);

bool isPidRunning(int pid);
bool ensureDaemonDir(const std::filesystem::path& stateDir, std::string& error);

bool readDaemonStateFile(const std::filesystem::path& stateDir, DaemonProcessStatus& status, std::string& error);
bool writeDaemonStateFile(const std::filesystem::path& stateDir, const DaemonProcessStatus& status, std::string& error);
bool writeDaemonPidFile(const std::filesystem::path& stateDir, const DaemonProcessStatus& status, std::string& error);

void removeDaemonStateFile(const std::filesystem::path& stateDir);
void removeDaemonPidFile(const std::filesystem::path& stateDir, const DaemonOptions& options);
void cleanupDaemonStateAndPid(const std::filesystem::path& stateDir, const DaemonOptions& options);

} // namespace subcli
