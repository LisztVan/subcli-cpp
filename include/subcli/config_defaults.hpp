#pragma once

#include <filesystem>
#include <string>

#include "subcli/environment.hpp"
#include "subcli/models.hpp"

namespace subcli {

enum class ConfigLayout {
    Portable,
    FHS,
    UserLocal,
};

AppConfig makeDefaultConfig(ConfigLayout layout, PlatformKind platform);
void resolveConfigPathsFromAppDir(AppConfig& config, const std::filesystem::path& appDir);
bool ensureConfigRuntimeFiles(const AppConfig& config, std::string& error);
bool writeInitialConfig(const std::filesystem::path& path, const AppConfig& config, bool force, std::string& error);

} // namespace subcli
