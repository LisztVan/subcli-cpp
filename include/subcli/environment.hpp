#pragma once

#include <string>
#include <vector>

namespace subcli {

enum class PlatformKind { Linux, MacOS, Windows };

enum class EnvironmentSource {
    CliOption,
    EnvVar,
    MarkerDiscovery,
    PersistedDefault,
    PlatformDefault,
};

struct EnvironmentPaths {
    std::string root;
    std::string configDir;
    std::string dataDir;
    std::string cacheDir;
    std::string stateDir;
    std::string outputDir;
    std::string templateDir;
    std::string profileDir;
    std::string subPath;
    std::string configPath;
};

struct EnvironmentResolveInput {
    std::string cliWorkspace;
    std::string envWorkspace;
    std::string cwd;
    std::string persistedWorkspace;
    PlatformKind platform = PlatformKind::Linux;
};

struct EnvironmentResolveResult {
    bool ok = false;
    EnvironmentSource source = EnvironmentSource::PlatformDefault;
    std::string root;
    EnvironmentPaths paths;
    std::vector<std::string> trace;
    std::string error;
};

EnvironmentResolveResult resolveEnvironment(const EnvironmentResolveInput& input);
std::string platformDefaultWorkspaceRoot(PlatformKind platform);

} // namespace subcli
