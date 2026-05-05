#pragma once

#include <string>
#include <vector>

#include "subcli/environment.hpp"

namespace subcli {

int runDoctorCommand(const EnvironmentPaths& paths, const std::vector<std::string>& args);

} // namespace subcli
