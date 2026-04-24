#pragma once

#include <string>
#include <filesystem>

#include "subcli/models.hpp"

namespace subcli {

struct CorePaths {
    std::string mihomo;
    std::string singBox;
    std::string xray;
};

std::string findExecutableInPath(const std::string& name);
bool isExecutableFile(const std::filesystem::path& path);
CorePaths discoverCorePaths(const AppConfig& cfg);

} // namespace subcli
