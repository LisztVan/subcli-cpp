#include "subcli/environment.hpp"

#include <cstdlib>
#include <filesystem>
#include <system_error>

namespace subcli {
namespace {

namespace fs = std::filesystem;

fs::path normalizeAbsolutePath(const fs::path& path) {
    std::error_code ec;
    fs::path abs = fs::absolute(path, ec);
    if (ec) {
        return path.lexically_normal();
    }
    return abs.lexically_normal();
}

bool isValidWorkspaceRoot(const fs::path& path) {
    std::error_code ec;
    return !path.empty() && fs::exists(path, ec) && fs::is_directory(path, ec);
}

bool markerExists(const fs::path& dir) {
    std::error_code ec;
    if (fs::exists(dir / ".subcli-workspace", ec) && !ec) {
        return true;
    }
    ec.clear();
    return fs::exists(dir / "subcli.env.yaml", ec) && !ec;
}

std::string getEnvValue(const char* name) {
    const char* raw = std::getenv(name);
    if (raw && *raw) {
        return std::string(raw);
    }
    return "";
}

fs::path homeDir() {
#ifdef _WIN32
    std::string home = getEnvValue("USERPROFILE");
    if (!home.empty()) {
        return fs::path(home);
    }
    const std::string drive = getEnvValue("HOMEDRIVE");
    const std::string path = getEnvValue("HOMEPATH");
    if (!drive.empty() && !path.empty()) {
        return fs::path(drive + path);
    }
#else
    std::string home = getEnvValue("HOME");
    if (!home.empty()) {
        return fs::path(home);
    }
#endif
    return normalizeAbsolutePath(fs::current_path());
}

fs::path platformDefaultRoot(PlatformKind platform) {
    const fs::path home = homeDir();
    if (platform == PlatformKind::Windows) {
        const std::string appData = getEnvValue("APPDATA");
        if (!appData.empty()) {
            return normalizeAbsolutePath(fs::path(appData) / "subcli");
        }
        return normalizeAbsolutePath(home / "AppData" / "Roaming" / "subcli");
    }
    if (platform == PlatformKind::MacOS) {
        return normalizeAbsolutePath(home / "Library" / "Application Support" / "subcli");
    }
    return normalizeAbsolutePath(home / ".local" / "share" / "subcli");
}

EnvironmentPaths buildPaths(const fs::path& rootPath) {
    const fs::path root = normalizeAbsolutePath(rootPath);
    EnvironmentPaths paths;
    paths.root = root.string();
    paths.configDir = root.string();
    paths.dataDir = root.string();
    paths.cacheDir = (root / "cache").string();
    paths.stateDir = (root / "state").string();
    paths.outputDir = (root / "outputs").string();
    paths.templateDir = (root / "templates").string();
    paths.profileDir = (root / "profiles").string();
    paths.subPath = (root / "sub.yaml").string();
    paths.configPath = (root / "config.yaml").string();
    return paths;
}

} // namespace

EnvironmentResolveResult resolveEnvironment(const EnvironmentResolveInput& input) {
    EnvironmentResolveResult out;
    out.trace.push_back("resolution order: cli --workspace > SUBCLI_WORKSPACE > marker discovery > persisted default > platform default");

    if (!input.cliWorkspace.empty()) {
        const fs::path cliRoot = normalizeAbsolutePath(fs::path(input.cliWorkspace));
        out.trace.push_back("cli workspace provided: " + cliRoot.string());
        if (!isValidWorkspaceRoot(cliRoot)) {
            out.error = "invalid CLI workspace directory: " + cliRoot.string();
            out.trace.push_back("cli workspace invalid: fail hard");
            out.ok = false;
            return out;
        }
        out.source = EnvironmentSource::CliOption;
        out.root = cliRoot.string();
        out.paths = buildPaths(cliRoot);
        out.ok = true;
        return out;
    }

    if (!input.envWorkspace.empty()) {
        const fs::path envRoot = normalizeAbsolutePath(fs::path(input.envWorkspace));
        out.trace.push_back("SUBCLI_WORKSPACE provided: " + envRoot.string());
        if (!isValidWorkspaceRoot(envRoot)) {
            out.error = "invalid SUBCLI_WORKSPACE directory: " + envRoot.string();
            out.trace.push_back("SUBCLI_WORKSPACE invalid: fail hard");
            out.ok = false;
            return out;
        }
        out.source = EnvironmentSource::EnvVar;
        out.root = envRoot.string();
        out.paths = buildPaths(envRoot);
        out.ok = true;
        return out;
    }

    fs::path current = input.cwd.empty() ? fs::current_path() : fs::path(input.cwd);
    current = normalizeAbsolutePath(current);
    out.trace.push_back("marker discovery start: " + current.string());
    while (true) {
        if (markerExists(current)) {
            out.trace.push_back("marker discovered at: " + current.string());
            out.source = EnvironmentSource::MarkerDiscovery;
            out.root = current.string();
            out.paths = buildPaths(current);
            out.ok = true;
            return out;
        }
        const fs::path parent = current.parent_path();
        if (parent.empty() || parent == current) {
            break;
        }
        current = parent;
    }
    out.trace.push_back("no workspace marker found from cwd upward");

    if (!input.persistedWorkspace.empty()) {
        const fs::path persisted = normalizeAbsolutePath(fs::path(input.persistedWorkspace));
        out.trace.push_back("persisted workspace candidate: " + persisted.string());
        if (isValidWorkspaceRoot(persisted)) {
            out.source = EnvironmentSource::PersistedDefault;
            out.root = persisted.string();
            out.paths = buildPaths(persisted);
            out.ok = true;
            return out;
        }
        out.trace.push_back("persisted workspace invalid: falling through to platform default");
    }

    const fs::path platformRoot = platformDefaultRoot(input.platform);
    out.trace.push_back("platform default selected: " + platformRoot.string());
    out.source = EnvironmentSource::PlatformDefault;
    out.root = platformRoot.string();
    out.paths = buildPaths(platformRoot);
    out.ok = true;
    return out;
}

} // namespace subcli
