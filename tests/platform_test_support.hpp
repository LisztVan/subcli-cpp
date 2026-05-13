#pragma once

#include <string>

#include "subcli/platform.hpp"

namespace subcli {

ProcessCommand testSupportSuccessCommand();
ProcessCommand testSupportFailureCommand();
ProcessCommand testSupportLongRunningCommand(int seconds);
ProcessCommand testSupportShellCommand(const std::string& script);
std::string testSupportMissingExecutablePath();

} // namespace subcli
