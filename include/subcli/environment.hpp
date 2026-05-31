#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace subcli {

enum class PlatformKind { Linux, MacOS, Windows };

enum class ConfigMode {
    Explicit,
    Portable,
    FHS,
    UserLocal,
    Missing,
};

struct EnvironmentInfo {
    bool ok = false;
    ConfigMode mode = ConfigMode::Missing;
    std::filesystem::path appDir;
    std::filesystem::path configPath;
    std::filesystem::path configDir;
    std::vector<std::string> trace;
    std::string error;
};

struct EnvironmentDetectInput {
    std::string argv0;
    std::string configOption;
    std::string envConfig;
    std::string cwd;
    std::string exeDirOverride;
    std::string fhsConfigOverride;
    std::string userConfigOverride;
    PlatformKind platform = PlatformKind::Linux;
};

struct EnvironmentPaths {
    std::string root;
    std::string appDir;
    std::string configDir;
    std::string dataDir;
    std::string cacheDir;
    std::string stateDir;
    std::string outputDir;
    std::string templateDir;
    std::string profileDir;
    std::string logDir;
    std::string assetDir;
    std::string subPath;
    std::string configPath;
};

EnvironmentInfo detectEnvironment(const EnvironmentDetectInput& input);
std::filesystem::path resolvePathFromAppDir(const std::filesystem::path& appDir, const std::string& value);
std::filesystem::path platformFhsConfigPath(PlatformKind platform);
std::filesystem::path platformUserConfigPath(PlatformKind platform);
bool shouldInitializeFhsConfig(const std::filesystem::path& appDir, PlatformKind platform);
std::string configModeName(ConfigMode mode);

} // namespace subcli
