#include "subcli/environment.hpp"

#include <cstdlib>
#include <filesystem>
#include <system_error>

namespace subcli {
namespace fs = std::filesystem;

namespace {

fs::path normalizeAbsolutePath(const fs::path& path) {
    std::error_code ec;
    fs::path abs = fs::absolute(path, ec);
    if (ec) {
        return path.lexically_normal();
    }
    return abs.lexically_normal();
}

std::string getEnvValue(const char* name) {
    const char* raw = std::getenv(name);
    return raw && *raw ? std::string(raw) : std::string();
}

fs::path homeDir(PlatformKind platform) {
#ifdef _WIN32
    const std::string userProfile = getEnvValue("USERPROFILE");
    if (!userProfile.empty()) {
        return fs::path(userProfile);
    }
#endif
    const std::string home = getEnvValue("HOME");
    if (!home.empty()) {
        return fs::path(home);
    }
    if (platform == PlatformKind::Windows) {
        const std::string appData = getEnvValue("APPDATA");
        if (!appData.empty()) {
            return fs::path(appData).parent_path();
        }
    }
    std::error_code ec;
    return fs::current_path(ec);
}

fs::path exeDirFromArgv0(const std::string& argv0, const std::string& overrideDir, const std::string& cwd) {
    if (!overrideDir.empty()) {
        return normalizeAbsolutePath(overrideDir);
    }
    if (!argv0.empty()) {
        fs::path exe(argv0);
        if (exe.has_parent_path()) {
            return normalizeAbsolutePath(exe.parent_path());
        }
    }
    if (!cwd.empty()) {
        return normalizeAbsolutePath(cwd);
    }
    std::error_code ec;
    return normalizeAbsolutePath(fs::current_path(ec));
}

bool existsRegularFile(const fs::path& path) {
    std::error_code ec;
    return fs::exists(path, ec) && !ec && fs::is_regular_file(path, ec) && !ec;
}

EnvironmentInfo makeInfo(ConfigMode mode, const fs::path& appDir, const fs::path& configPath, std::vector<std::string> trace) {
    EnvironmentInfo info;
    info.ok = true;
    info.mode = mode;
    info.appDir = normalizeAbsolutePath(appDir);
    info.configPath = normalizeAbsolutePath(configPath);
    info.configDir = info.configPath.parent_path();
    info.trace = std::move(trace);
    return info;
}

EnvironmentInfo makeMissingInfo(const fs::path& appDir, std::vector<std::string> trace) {
    EnvironmentInfo info;
    info.ok = false;
    info.mode = ConfigMode::Missing;
    info.appDir = normalizeAbsolutePath(appDir);
    info.trace = std::move(trace);
    info.error = "config.yaml not found; run 'subcli config init --portable' or 'subcli config init --path <path>' first";
    return info;
}

} // namespace

std::filesystem::path resolvePathFromAppDir(const std::filesystem::path& appDir, const std::string& value) {
    if (value.empty()) {
        return {};
    }
    fs::path path(value);
    if (path.is_absolute()) {
        return path.lexically_normal();
    }
    return (appDir / path).lexically_normal();
}

std::filesystem::path platformFhsConfigPath(PlatformKind platform) {
    if (platform == PlatformKind::MacOS) {
        return fs::path("/usr/local/etc/subcli/config.yaml");
    }
    if (platform == PlatformKind::Windows) {
        return {};
    }
    return fs::path("/etc/subcli/config.yaml");
}

std::filesystem::path platformUserConfigPath(PlatformKind platform) {
    if (platform == PlatformKind::Windows) {
        const std::string appData = getEnvValue("APPDATA");
        if (!appData.empty()) {
            return fs::path(appData) / "subcli" / "config.yaml";
        }
        return homeDir(platform) / "AppData" / "Roaming" / "subcli" / "config.yaml";
    }
    if (platform == PlatformKind::MacOS) {
        return homeDir(platform) / "Library" / "Application Support" / "subcli" / "config.yaml";
    }
    const std::string xdgConfig = getEnvValue("XDG_CONFIG_HOME");
    if (!xdgConfig.empty()) {
        return fs::path(xdgConfig) / "subcli" / "config.yaml";
    }
    return homeDir(platform) / ".config" / "subcli" / "config.yaml";
}

std::string configModeName(ConfigMode mode) {
    switch (mode) {
    case ConfigMode::Explicit:
        return "explicit";
    case ConfigMode::Portable:
        return "portable";
    case ConfigMode::FHS:
        return "fhs";
    case ConfigMode::UserLocal:
        return "user_local";
    case ConfigMode::Missing:
        return "missing";
    }
    return "unknown";
}

EnvironmentInfo detectEnvironment(const EnvironmentDetectInput& input) {
    std::vector<std::string> trace;
    trace.push_back("resolution order: --config > SUBCLI_CONFIG > exe-dir config.yaml > FHS config > user-local config");

    const std::string explicitPath = !input.configOption.empty() ? input.configOption : input.envConfig;
    if (!explicitPath.empty()) {
        const fs::path configPath = normalizeAbsolutePath(explicitPath);
        const fs::path appDir = exeDirFromArgv0(input.argv0, input.exeDirOverride, input.cwd);
        if (!existsRegularFile(configPath)) {
            trace.push_back(input.configOption.empty() ? "selected SUBCLI_CONFIG (new)" : "selected --config (new)");
            return makeInfo(ConfigMode::Explicit, appDir, configPath, std::move(trace));
        }
        trace.push_back(input.configOption.empty() ? "selected SUBCLI_CONFIG" : "selected --config");
        return makeInfo(ConfigMode::Explicit, appDir, configPath, std::move(trace));
    }

    const fs::path appDir = exeDirFromArgv0(input.argv0, input.exeDirOverride, input.cwd);
    const fs::path portableConfig = appDir / "config.yaml";
    if (existsRegularFile(portableConfig)) {
        trace.push_back("selected portable config next to executable");
        return makeInfo(ConfigMode::Portable, appDir, portableConfig, std::move(trace));
    }

    const fs::path fhsConfig = input.fhsConfigOverride.empty() ? platformFhsConfigPath(input.platform) : fs::path(input.fhsConfigOverride);
    if (!fhsConfig.empty() && existsRegularFile(fhsConfig)) {
        trace.push_back("selected FHS config");
        return makeInfo(ConfigMode::FHS, appDir, fhsConfig, std::move(trace));
    }

    const fs::path userConfig = input.userConfigOverride.empty() ? platformUserConfigPath(input.platform) : fs::path(input.userConfigOverride);
    if (existsRegularFile(userConfig)) {
        trace.push_back("selected user-local config");
        return makeInfo(ConfigMode::UserLocal, appDir, userConfig, std::move(trace));
    }

    trace.push_back("no config found; user must run config init");
    return makeMissingInfo(appDir, std::move(trace));
}

} // namespace subcli
