#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <curl/curl.h>
#include <CLI/CLI.hpp>
#include <exception>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <set>
#include <sstream>
#include <thread>
#include <system_error>
#include <map>
#include <tuple>

#include <yaml-cpp/yaml.h>
#include <nlohmann/json.hpp>

#include "subcli/assets.hpp"
#include "subcli/core_check.hpp"
#include "subcli/core_discovery.hpp"
#include "subcli/core_runtime.hpp"
#include "subcli/daemon.hpp"
#include "subcli/cli_completion.hpp"
#include "subcli/cli_output.hpp"
#include "subcli/capability_matrix.hpp"
#include "subcli/environment.hpp"
#include "subcli/exporter.hpp"
#include "subcli/fetch.hpp"
#include "subcli/parser.hpp"
#include "subcli/profile.hpp"
#include "subcli/profile_explain.hpp"
#include "subcli/store.hpp"
#include "subcli/tag_utils.hpp"
#include "subcli/util.hpp"
#include "subcli/workspace.hpp"
#include "exporter_internal.hpp"

using namespace subcli;

namespace {

constexpr int ExitOk = 0;
constexpr int ExitError = 1;
constexpr int ExitUsage = 2;

struct RuntimePaths {
    std::filesystem::path root;
    std::filesystem::path dataDir;
    std::filesystem::path cacheDir;
    std::filesystem::path stateDir;
    std::filesystem::path configDir;
    std::filesystem::path subPath;
    std::filesystem::path configPath;
    std::filesystem::path templateDir;
    std::filesystem::path profileDir;
    std::filesystem::path outputDir;
};

RuntimePaths gPaths;
std::string gExecutablePath;
EnvironmentResolveResult gEnvResult;

PlatformKind detectPlatformKind() {
#ifdef _WIN32
    return PlatformKind::Windows;
#elif defined(__APPLE__)
    return PlatformKind::MacOS;
#else
    return PlatformKind::Linux;
#endif
}

std::string environmentSourceToString(EnvironmentSource source) {
    switch (source) {
    case EnvironmentSource::CliOption:
        return "cli_option";
    case EnvironmentSource::EnvVar:
        return "env_var";
    case EnvironmentSource::MarkerDiscovery:
        return "marker_discovery";
    case EnvironmentSource::PersistedDefault:
        return "persisted_default";
    case EnvironmentSource::PlatformDefault:
        return "platform_default";
    }
    return "unknown";
}

std::string readPersistedDefaultWorkspace() {
    const auto status = workspaceStatus();
    if (!status.ok || !status.hasDefault) {
        return "";
    }
    return status.defaultRoot;
}

std::filesystem::path normalizeAbsolutePath(const std::filesystem::path& path) {
    std::error_code ec;
    auto abs = std::filesystem::absolute(path, ec);
    if (ec) {
        return path;
    }
    return abs.lexically_normal();
}

std::string detectExecutablePath(const std::string& argv0) {
    std::error_code ec;
    const auto procSelf = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (!ec && !procSelf.empty()) {
        return normalizeAbsolutePath(procSelf).string();
    }
    if (!argv0.empty()) {
        return normalizeAbsolutePath(argv0).string();
    }
    return "";
}

bool looksLikeAppRoot(const std::filesystem::path& path) {
    std::error_code ec;
    if (path.empty() || !std::filesystem::exists(path, ec) || ec) {
        return false;
    }
    return std::filesystem::exists(path / "templates", ec) || std::filesystem::exists(path / "CMakeLists.txt", ec);
}

std::filesystem::path homeDir() {
    const char* home = std::getenv("HOME");
    if (home && *home) {
        return std::filesystem::path(home);
    }
    return normalizeAbsolutePath(std::filesystem::current_path());
}

std::filesystem::path xdgOrFallback(const char* envName, const std::filesystem::path& fallback) {
    const char* raw = std::getenv(envName);
    if (raw && *raw) {
        return std::filesystem::path(raw);
    }
    return fallback;
}

std::filesystem::path resolveInstalledTemplateDir(const std::filesystem::path& exePath, const std::filesystem::path& fallbackRoot) {
    const auto exeDir = exePath.parent_path();
    const std::vector<std::filesystem::path> candidates = {
        exeDir / "../share/subcli/templates",
        exeDir / "../templates",
        fallbackRoot / "templates",
    };

    std::error_code ec;
    for (const auto& candidateRaw : candidates) {
        const auto candidate = normalizeAbsolutePath(candidateRaw);
        if (std::filesystem::exists(candidate, ec) && !ec) {
            return candidate;
        }
        ec.clear();
    }
    return normalizeAbsolutePath(candidates.front());
}

std::filesystem::path resolveInstalledProfileDir(const std::filesystem::path& exePath, const std::filesystem::path& fallbackRoot) {
    const auto exeDir = exePath.parent_path();
    const std::vector<std::filesystem::path> candidates = {
        exeDir / "../share/subcli/profiles",
        exeDir / "../profiles",
        fallbackRoot / "profiles",
    };

    std::error_code ec;
    for (const auto& candidateRaw : candidates) {
        const auto candidate = normalizeAbsolutePath(candidateRaw);
        if (std::filesystem::exists(candidate, ec) && !ec) {
            return candidate;
        }
        ec.clear();
    }
    return normalizeAbsolutePath(candidates.front());
}

RuntimePaths buildRuntimePaths(const std::string& argv0) {
    std::vector<std::filesystem::path> candidates;
    if (!argv0.empty()) {
        const auto exePath = normalizeAbsolutePath(argv0);
        candidates.push_back(exePath.parent_path());
        candidates.push_back(exePath.parent_path().parent_path());
    }
    candidates.push_back(normalizeAbsolutePath(std::filesystem::current_path()));
    candidates.push_back(normalizeAbsolutePath(std::filesystem::current_path().parent_path()));

    std::filesystem::path root = normalizeAbsolutePath(std::filesystem::current_path());
    for (const auto& candidate : candidates) {
        if (looksLikeAppRoot(candidate)) {
            root = candidate;
            break;
        }
    }

    const auto home = homeDir();
    const auto exePath = argv0.empty() ? normalizeAbsolutePath("subcli") : normalizeAbsolutePath(argv0);

    RuntimePaths paths;
    paths.root = root;
    paths.configDir = xdgOrFallback("XDG_CONFIG_HOME", home / ".config") / "subcli";
    paths.dataDir = xdgOrFallback("XDG_DATA_HOME", home / ".local/share") / "subcli";
    paths.cacheDir = xdgOrFallback("XDG_CACHE_HOME", home / ".cache") / "subcli";
    paths.stateDir = xdgOrFallback("XDG_STATE_HOME", home / ".local/state") / "subcli";
    paths.subPath = paths.dataDir / "sub.yaml";
    paths.configPath = paths.configDir / "config.yaml";
    paths.templateDir = resolveInstalledTemplateDir(exePath, root);
    paths.profileDir = resolveInstalledProfileDir(exePath, root);
    paths.outputDir = paths.dataDir / "outputs";
    return paths;
}

std::string resolveAgainst(const std::filesystem::path& baseDir, const std::string& path) {
    std::filesystem::path p(path);
    if (p.is_absolute()) {
        return normalizeAbsolutePath(p).string();
    }
    return normalizeAbsolutePath(baseDir / p).string();
}

std::string resolveFromConfigDir(const std::string& path) {
    return resolveAgainst(gPaths.configDir, path);
}

std::string resolveFromCliCwd(const std::string& path) {
    return resolveAgainst(normalizeAbsolutePath(std::filesystem::current_path()), path);
}

std::string resolveProfileFileArg(const std::string& path) {
    const std::string cwdPath = resolveFromCliCwd(path);
    std::error_code ec;
    if (std::filesystem::exists(cwdPath, ec) && !ec) {
        return cwdPath;
    }
    ec.clear();
    std::filesystem::path p(path);
    if (!p.is_absolute()) {
        const auto rootPath = normalizeAbsolutePath(gPaths.root / p);
        if (std::filesystem::exists(rootPath, ec) && !ec) {
            return rootPath.string();
        }
    }
    return cwdPath;
}

std::string resolveCachePath(const std::string& path) {
    std::filesystem::path p(path);
    if (p.is_absolute()) {
        return normalizeAbsolutePath(p).string();
    }

    const std::string generic = p.generic_string();
    constexpr const char* legacyPrefix = "cache/";
    if (generic.rfind(legacyPrefix, 0) == 0) {
        return normalizeAbsolutePath(gPaths.cacheDir / generic.substr(std::char_traits<char>::length(legacyPrefix))).string();
    }
    return normalizeAbsolutePath(gPaths.cacheDir / p).string();
}

std::string defaultTemplatePath(const std::string& dir, const std::string& filename) {
    const std::filesystem::path configured = dir.empty() ? gPaths.templateDir : std::filesystem::path(dir);
    const std::filesystem::path base = configured.is_absolute() ? configured : (gPaths.root / configured);
    return (base / filename).string();
}

bool isSupportedProfile(const std::string& value) {
    return value == "bypass-cn" || value == "global" || value == "direct" || value == "custom";
}

bool isBuiltInProfileNameForCli(const std::string& value) {
    return value == "bypass-cn" || value == "global" || value == "direct";
}

void updateTemplateDirDefaults(AppConfig& c, const std::string& oldDir) {
    const std::map<std::string, std::string> filenames = {
        {"mihomo.normal", "mihomo_base.yaml"},
        {"mihomo.tun", "mihomo_tun.yaml"},
        {"sing-box.normal", "singbox_base.json"},
        {"sing-box.tun", "singbox_tun.json"},
        {"xray.normal", "xray_base.json"},
        {"xray.tun", "xray_tun.json"},
    };

    auto rewriteIfDefault = [&](std::map<std::string, std::string>& values, const std::string& key, const std::string& fullKey) {
        auto it = values.find(key);
        if (it == values.end()) {
            return;
        }
        const auto& filename = filenames.at(fullKey);
        const std::string oldDefault = defaultTemplatePath(oldDir, filename);
        const std::string oldLegacy = defaultTemplatePath("./templates", filename);
        if (it->second == oldDefault || it->second == oldLegacy || it->second == filename) {
            it->second = defaultTemplatePath(c.templateDir, filename);
        }
    };

    rewriteIfDefault(c.templateNormal, "mihomo", "mihomo.normal");
    rewriteIfDefault(c.templateTun, "mihomo", "mihomo.tun");
    rewriteIfDefault(c.templateNormal, "sing-box", "sing-box.normal");
    rewriteIfDefault(c.templateTun, "sing-box", "sing-box.tun");
    rewriteIfDefault(c.templateNormal, "xray", "xray.normal");
    rewriteIfDefault(c.templateTun, "xray", "xray.tun");
}

void applyConfigDefaults(AppConfig& c) {
    if (c.templateDir.empty()) {
        c.templateDir = gPaths.templateDir.string();
    } else if (c.templateDir == "./templates" || c.templateDir == "templates") {
        c.templateDir = gPaths.templateDir.string();
    } else {
        c.templateDir = resolveFromConfigDir(c.templateDir);
    }
    if (c.outputDir.empty()) {
        c.outputDir = gPaths.outputDir.string();
    } else if (c.outputDir == "./outputs" || c.outputDir == "outputs") {
        c.outputDir = gPaths.outputDir.string();
    } else {
        c.outputDir = resolveFromConfigDir(c.outputDir);
    }
    if (c.profile.empty()) {
        c.profile = "bypass-cn";
    }
    if (!c.profilePath.empty()) {
        c.profilePath = resolveFromConfigDir(c.profilePath);
    }
    if (c.assetDir.empty() || c.assetDir == "./assets" || c.assetDir == "assets") {
        c.assetDir = (gPaths.dataDir / "assets").string();
    } else {
        c.assetDir = resolveFromConfigDir(c.assetDir);
    }
    const std::map<std::string, std::string> defaultAssetPaths = {
        {"mihomo.geosite", "mihomo/geosite.dat"},
        {"mihomo.geoip", "mihomo/geoip.dat"},
        {"sing-box.geosite-cn", "sing-box/geosite-cn.srs"},
        {"sing-box.geoip-cn", "sing-box/geoip-cn.srs"},
        {"xray.geosite", "xray/geosite.dat"},
        {"xray.geoip", "xray/geoip.dat"},
    };
    const std::map<std::string, std::string> defaultAssetUrls = {
        {"mihomo.geosite", "https://github.com/MetaCubeX/meta-rules-dat/releases/download/latest/geosite.dat"},
        {"mihomo.geoip", "https://github.com/MetaCubeX/meta-rules-dat/releases/download/latest/geoip.dat"},
        {"sing-box.geosite-cn", "https://github.com/SagerNet/sing-geosite/releases/latest/download/geosite-cn.srs"},
        {"sing-box.geoip-cn", "https://github.com/SagerNet/sing-geoip/releases/latest/download/geoip-cn.srs"},
        {"xray.geosite", "https://github.com/v2fly/domain-list-community/releases/latest/download/dlc.dat"},
        {"xray.geoip", "https://github.com/v2fly/geoip/releases/latest/download/geoip.dat"},
    };
    for (const auto& kv : defaultAssetPaths) {
        if (!c.assetPaths.count(kv.first) || c.assetPaths[kv.first].empty()) {
            c.assetPaths[kv.first] = (std::filesystem::path(c.assetDir) / kv.second).string();
        } else {
            std::filesystem::path assetPath(c.assetPaths[kv.first]);
            if (!assetPath.is_absolute()) {
                c.assetPaths[kv.first] = (std::filesystem::path(c.assetDir) / assetPath).string();
            }
        }
    }
    for (const auto& kv : defaultAssetUrls) {
        if (!c.assetUrls.count(kv.first) || c.assetUrls[kv.first].empty()) {
            c.assetUrls[kv.first] = kv.second;
        }
    }
    const std::string templateDir = c.templateDir;
    if (!c.templateNormal.count("mihomo")) {
        c.templateNormal["mihomo"] = defaultTemplatePath(templateDir, "mihomo_base.yaml");
    }
    if (!c.templateTun.count("mihomo")) {
        c.templateTun["mihomo"] = defaultTemplatePath(templateDir, "mihomo_tun.yaml");
    }
    if (!c.templateNormal.count("sing-box")) {
        c.templateNormal["sing-box"] = defaultTemplatePath(templateDir, "singbox_base.json");
    }
    if (!c.templateTun.count("sing-box")) {
        c.templateTun["sing-box"] = defaultTemplatePath(templateDir, "singbox_tun.json");
    }
    if (!c.templateNormal.count("xray")) {
        c.templateNormal["xray"] = defaultTemplatePath(templateDir, "xray_base.json");
    }
    if (!c.templateTun.count("xray")) {
        c.templateTun["xray"] = defaultTemplatePath(templateDir, "xray_tun.json");
    }

    auto normalizeTemplateValue = [&](std::string& value) {
        if (value.empty()) {
            return;
        }
        std::filesystem::path templatePath(value);
        if (templatePath.is_absolute()) {
            return;
        }
        if (value.find('/') == std::string::npos && value.find('\\') == std::string::npos) {
            value = defaultTemplatePath(templateDir, value);
        } else {
            value = resolveFromConfigDir(value);
        }
    };

    for (auto& kv : c.templateNormal) {
        normalizeTemplateValue(kv.second);
    }
    for (auto& kv : c.templateTun) {
        normalizeTemplateValue(kv.second);
    }

    if (!c.mihomoPath.empty()) {
        c.mihomoPath = resolveFromConfigDir(c.mihomoPath);
    }
    if (!c.singBoxPath.empty()) {
        c.singBoxPath = resolveFromConfigDir(c.singBoxPath);
    }
    if (!c.xrayPath.empty()) {
        c.xrayPath = resolveFromConfigDir(c.xrayPath);
    }

    if (c.regionRules.empty()) {
        c.regionRules = {
            {"HK", "(?i)(hong kong|hongkong|hk|香港)"},
            {"SG", "(?i)(singapore|sg|新加坡)"},
            {"JP", "(?i)(japan|jp|tokyo|osaka|日本)"},
            {"TW", "(?i)(taiwan|tw|台灣|台湾)"},
            {"US", "(?i)(united states|usa|us|america|美国)"},
        };
    }
    if (c.renameTemplate.empty()) {
        c.renameTemplate = "{name}";
    }
    if (c.sortBy.empty()) {
        c.sortBy = "region,name";
    }
}

void ensureDefaults() {
    std::filesystem::create_directories(gPaths.configDir);
    std::filesystem::create_directories(gPaths.dataDir);
    std::filesystem::create_directories(gPaths.cacheDir);
    std::filesystem::create_directories(gPaths.stateDir);
    std::filesystem::create_directories(gPaths.outputDir);

    if (!fileExists(gPaths.subPath.string())) {
        saveSubscriptions(gPaths.subPath.string(), {});
    }
    if (!fileExists(gPaths.configPath.string())) {
        AppConfig c;
        applyConfigDefaults(c);
        saveConfig(gPaths.configPath.string(), c);
    }
}

std::string argValue(const std::vector<std::string>& args, const std::string& key, const std::string& fallback = "") {
    for (size_t i = 0; i + 1 < args.size(); ++i) {
        if (args[i] == key) {
            return args[i + 1];
        }
    }
    return fallback;
}

bool hasFlag(const std::vector<std::string>& args, const std::string& key) {
    return std::find(args.begin(), args.end(), key) != args.end();
}

bool hasHelp(const std::vector<std::string>& args) {
    return hasFlag(args, "--help") || hasFlag(args, "-h") || (args.size() > 1 && args[1] == "help");
}

void printRootUsage() {
    std::cout << "Usage:\n"
              << "  subcli [--workspace DIR] <command> [args...]\n"
              << "\n"
              << "Global Options:\n"
              << "  --workspace DIR  Use a workspace for this invocation.\n"
              << "\n"
              << "Commands:\n"
              << "  init      Create config/data/cache/state/output directories.\n"
              << "  doctor    Check dirs, templates, assets, and core paths.\n"
              << "  sub       list/add/edit/remove/enable/disable/update/validate subscriptions.\n"
              << "  config    list/get/set/remove application settings.\n"
              << "  profile   list/get/validate/explain export profiles.\n"
              << "  template  list/get/set/reset/validate export templates.\n"
              << "  asset     list/status/validate/update geo and rule assets.\n"
              << "  export    all/mihomo/sing-box/xray configs.\n"
              << "\n"
              << "Workspace:\n"
              << "  workspace init/status/use/unset/migrate/doctor workspace roots.\n"
              << "\n"
              << "Runtime Helpers (optional):\n"
              << "  check     Validate exported config with installed core.\n"
              << "  run       Run one core with an exported config.\n"
              << "  daemon    once/run/start/stop/status periodic helper.\n"
              << "  status    Show helper process status.\n"
              << "  stop      Stop a helper process.\n"
              << "  restart   Restart a helper process.\n"
              << "\n"
              << "Shell:\n"
              << "  completion  Generate shell completion scripts.\n"
              << "\n"
              << "Examples:\n"
              << "  subcli init\n"
              << "  subcli doctor\n"
              << "  subcli template list\n"
              << "  subcli profile list\n"
              << "  subcli sub add --name airport-a --url https://example/sub\n"
              << "  subcli sub update\n"
              << "  subcli export all --profile bypass-cn --check\n"
              << "\n"
              << "Help:\n"
              << "  subcli <command> --help\n"
              << "  subcli template --help\n"
              << "  subcli export --help\n";
}

void printInitUsage() {
    std::cout << "Usage:\n"
              << "  subcli init\n"
              << "\n"
              << "Initialize config, data, cache, state, template, and output directories.\n"
              << "\n"
              << "Examples:\n"
              << "  subcli init\n"
              << "\n"
              << "Next:\n"
              << "  subcli doctor\n"
              << "  subcli sub add --name airport-a --url https://example/sub\n";
}

void printDoctorUsage() {
    std::cout << "Usage:\n"
              << "  subcli doctor [--json]\n"
              << "\n"
              << "Check config/data/cache/output dirs, templates, assets, and core paths.\n"
              << "\n"
              << "Examples:\n"
              << "  subcli doctor\n"
              << "  subcli doctor --json\n"
              << "\n"
              << "Next:\n"
              << "  subcli template list\n"
              << "  subcli asset status\n";
}

void printSubUsage() {
    std::cout << "Usage:\n"
              << "  subcli sub list [--json]\n"
              << "  subcli sub add --name NAME --url URL [options]\n"
              << "  subcli sub edit <id|name> [options]\n"
              << "  subcli sub remove <id|name>\n"
              << "  subcli sub enable <id|name>\n"
              << "  subcli sub disable <id|name>\n"
              << "  subcli sub update [id-or-name ...] [--tag TAG] [--strict-network]\n"
              << "  subcli sub validate [id-or-name]\n"
              << "\n"
              << "Add/Edit Options:\n"
              << "  --id ID\n"
              << "  --name NAME\n"
              << "  --url URL\n"
              << "  --group GROUP\n"
              << "  --format-hint auto|mihomo|sing-box|xray|uri\n"
              << "  --user-agent VALUE\n"
              << "  --timeout SEC\n"
              << "  --retry N\n"
              << "  --priority N\n"
              << "  --update-interval SEC\n"
              << "  --tag TAG\n"
              << "  --tags a,b\n"
              << "  --header 'Key: Value'\n"
              << "  --remove-header Key\n"
              << "  --clear-headers\n"
              << "  --force\n"
              << "  --enable\n"
              << "  --disable\n"
              << "\n"
              << "Examples:\n"
              << "  subcli sub list\n"
              << "  subcli sub add --name airport-a --url https://example/sub\n"
              << "  subcli sub edit airport-a --tag hk --priority 20\n"
              << "  subcli sub update --tag hk\n"
              << "  subcli sub validate airport-a\n";
}

void printConfigUsage() {
    std::cout << "Usage:\n"
              << "  subcli config list [--json]\n"
              << "  subcli config get <key>\n"
              << "  subcli config set <key> <value>\n"
              << "  subcli config remove <key>\n"
              << "\n"
              << "Common Keys:\n"
              << "  tun\n"
              << "  profile\n"
              << "  profile_path\n"
              << "  output_dir\n"
              << "  template_dir\n"
              << "  asset_dir\n"
              << "  parallelism\n"
              << "  timeout\n"
              << "  retry\n"
              << "  fetch_max_bytes\n"
              << "  log_level\n"
              << "  core_paths.mihomo\n"
              << "  core_paths.sing_box\n"
              << "  core_paths.xray\n"
              << "  node_management.dedupe\n"
              << "  node_management.rename_template\n"
              << "  node_management.include_regex\n"
              << "  node_management.exclude_regex\n"
              << "  node_management.sort_by\n"
              << "  grouping.region_rules.<REGION>\n"
              << "  assets.paths.<asset-key>\n"
              << "  assets.urls.<asset-key>\n"
              << "  templates.<target>.<normal|tun>\n"
              << "\n"
              << "Examples:\n"
              << "  subcli config list\n"
              << "  subcli config get core_paths.sing_box\n"
              << "  subcli config set profile bypass-cn\n"
              << "\n"
              << "Note:\n"
              << "  Template paths are easier to manage with 'subcli template --help'.\n";
}

void printTemplateUsage() {
    std::cout << "Usage:\n"
              << "  subcli template list [--json]\n"
              << "  subcli template get <target> <kind>\n"
              << "  subcli template set <target> <kind> <path>\n"
              << "  subcli template reset [target] [kind]\n"
              << "  subcli template validate [--json]\n"
              << "\n"
              << "Targets:\n"
              << "  mihomo\n"
              << "  sing-box\n"
              << "  xray\n"
              << "\n"
              << "Kinds:\n"
              << "  normal\n"
              << "  tun\n"
              << "\n"
              << "Commands:\n"
              << "  list      Show template paths and whether files exist.\n"
              << "  get       Print one configured template path.\n"
              << "  set       Set one template path from a local file path.\n"
              << "  reset     Reset all templates, one target, or one target kind.\n"
              << "  validate  Check that template files exist and parse correctly.\n"
              << "\n"
              << "Examples:\n"
              << "  subcli template list\n"
              << "  subcli template list --json\n"
              << "  subcli template get sing-box normal\n"
              << "  subcli template set sing-box normal ./templates/singbox_base.json\n"
              << "  subcli template reset xray tun\n"
              << "  subcli template validate\n"
              << "\n"
              << "Next:\n"
              << "  subcli config list\n"
              << "  subcli export all --profile bypass-cn\n";
}

void printAssetUsage() {
    std::cout << "Usage:\n"
              << "  subcli asset list\n"
              << "  subcli asset status\n"
              << "  subcli asset validate\n"
              << "  subcli asset update [asset-key]\n"
              << "\n"
              << "Commands:\n"
              << "  list      Show configured asset key/path/url entries.\n"
              << "  status    Show local status, size, update time, and source.\n"
              << "  validate  Fail if required assets are missing.\n"
              << "  update    Download all assets or one specified asset key.\n"
              << "\n"
              << "Examples:\n"
              << "  subcli asset list\n"
              << "  subcli asset status\n"
              << "  subcli asset update\n"
              << "  subcli asset update xray.geoip\n";
}

void printProfileUsage() {
    std::cout << "Usage:\n"
              << "  subcli profile list\n"
              << "  subcli profile get <bypass-cn|global|direct>\n"
              << "  subcli profile validate <path>\n"
              << "  subcli profile explain <path-or-name> [--target <all|mihomo|sing-box|xray>] [--json]\n"
              << "\n"
              << "Built-in Profiles:\n"
              << "  bypass-cn\n"
              << "  global\n"
              << "  direct\n"
              << "\n"
              << "Examples:\n"
              << "  subcli profile list\n"
              << "  subcli profile get bypass-cn\n"
              << "  subcli profile validate ./profiles/bypass-cn.json\n"
              << "  subcli profile explain bypass-cn --target all\n"
              << "  subcli profile explain ./profiles/custom.json --json\n";
}

void printExportUsage() {
    std::cout << "Usage:\n"
              << "  subcli export <all|mihomo|sing-box|xray> [options]\n"
              << "\n"
              << "Options:\n"
              << "  --tun\n"
              << "  --check\n"
              << "  --check-timeout SEC\n"
              << "  --output-dir DIR\n"
              << "  --profile PATH_OR_NAME\n"
              << "  --sub ID_OR_NAME\n"
              << "  --tag TAG\n"
              << "  --strict-network\n"
              << "  --strict-capabilities\n"
              << "  --download-assets\n"
              << "  --explain-policy\n"
              << "  --json\n"
              << "\n"
              << "Examples:\n"
              << "  subcli export all\n"
              << "  subcli export all --profile bypass-cn --check\n"
              << "  subcli export sing-box --output-dir ./outputs --check --check-timeout 30\n"
              << "  subcli export mihomo --tag hk --strict-network\n"
              << "  subcli export xray --profile /path/to/custom-profile.json --json\n";
}

std::vector<std::string> templatePolicyPathsForTarget(ExportTarget target) {
    if (target == ExportTarget::SingBox) {
        return {"outbounds", "dns", "dns.servers", "dns.rules", "route.rules", "route.rule_set"};
    }
    if (target == ExportTarget::Xray) {
        return {"outbounds", "dns", "dns.servers", "routing.rules", "routing.balancers"};
    }
    return {"proxies", "proxy-groups", "rules", "dns", "dns.nameserver", "dns.fallback"};
}

std::string templatePolicyActionName(TemplatePolicyAction action) {
    switch (action) {
        case TemplatePolicyAction::Replace:
            return "replace";
        case TemplatePolicyAction::Append:
            return "append";
        case TemplatePolicyAction::Merge:
            return "merge";
        case TemplatePolicyAction::Reject:
            return "reject";
    }
    return "replace";
}

void printExportPolicyExplainForTarget(ExportTarget target, const ResolvedProfile* profile) {
    const std::string targetName = templatePolicyTargetKey(target);
    std::cout << "policy explain: target=" << targetName << "\n";
    const auto paths = templatePolicyPathsForTarget(target);
    for (const auto& path : paths) {
        TemplatePolicyAction explicitAction;
        const bool hasExplicit = getExplicitTemplatePolicyAction(target, profile, path, explicitAction);
        if (hasExplicit) {
            std::cout << "  " << path << ": " << templatePolicyActionName(explicitAction) << " (explicit)\n";
        } else {
            std::cout << "  " << path << ": default exporter behavior\n";
        }
    }
}

void printWorkspaceUsage() {
    std::cout << "Usage:\n"
              << "  subcli workspace init [DIR]\n"
              << "  subcli workspace status [--json]\n"
              << "  subcli workspace use DIR\n"
              << "  subcli workspace unset\n"
              << "  subcli workspace migrate [--to DIR] [--from DIR] [--dry-run] [--overwrite]\n"
              << "  subcli workspace doctor\n"
              << "\n"
              << "Examples:\n"
              << "  subcli workspace init ./ws\n"
              << "  subcli workspace use ./ws\n"
              << "  subcli workspace status --json\n"
              << "  subcli workspace migrate --to ./ws --dry-run\n"
              << "  subcli workspace doctor\n";
}

struct WorkspaceDoctorFinding {
    std::string level;
    std::string code;
    std::string detail;
};

bool canWritePath(const std::filesystem::path& path) {
    std::error_code ec;
    if (std::filesystem::exists(path, ec)) {
        const auto perms = std::filesystem::status(path, ec).permissions();
        if (ec) {
            return false;
        }
        using p = std::filesystem::perms;
        return (perms & p::owner_write) != p::none || (perms & p::group_write) != p::none || (perms & p::others_write) != p::none;
    }

    std::ofstream out(path, std::ios::app);
    if (!out) {
        return false;
    }
    out.close();
    std::filesystem::remove(path, ec);
    return true;
}

std::vector<WorkspaceDoctorFinding> buildWorkspaceDoctorFindings(const std::filesystem::path& root) {
    std::vector<WorkspaceDoctorFinding> findings;
    std::error_code ec;
    if (!std::filesystem::exists(root, ec) || !std::filesystem::is_directory(root, ec)) {
        findings.push_back({"fail", "root_missing", "workspace root is missing or not a directory: " + root.string()});
        return findings;
    }

    const bool hasLegacyMarker = std::filesystem::exists(root / ".subcli-workspace", ec) && !ec;
    const auto metadata = workspaceReadMetadata(root.string());
    if (!hasLegacyMarker && !metadata.exists) {
        findings.push_back({"warn", "marker_missing", "workspace has neither .subcli-workspace nor subcli.env.yaml marker"});
    } else {
        findings.push_back({"ok", "marker_present", hasLegacyMarker ? "legacy marker present (.subcli-workspace)" : "metadata marker present (subcli.env.yaml)"});
    }

    if (metadata.exists) {
        if (metadata.valid) {
            findings.push_back({"ok", "metadata_valid", "subcli.env.yaml is valid (env_version=2, required fields present)"});
        } else {
            findings.push_back({"fail", "metadata_invalid", "subcli.env.yaml is invalid: " + metadata.error});
        }
    } else {
        findings.push_back({"warn", "metadata_missing", "subcli.env.yaml missing (legacy workspace still supported)"});
    }

    for (const char* dir : {"profiles", "templates", "assets", "cache", "outputs", "state"}) {
        const auto path = root / dir;
        if (!std::filesystem::exists(path, ec) || !std::filesystem::is_directory(path, ec)) {
            findings.push_back({"fail", "required_dir_missing", "missing required directory: " + path.string()});
        } else {
            findings.push_back({"ok", "required_dir_ok", "directory exists: " + path.string()});
        }
    }

    for (const char* file : {"config.yaml", "sub.yaml"}) {
        const auto path = root / file;
        if (!std::filesystem::exists(path, ec) || !std::filesystem::is_regular_file(path, ec)) {
            findings.push_back({"fail", "required_file_missing", "missing required file: " + path.string()});
        } else {
            findings.push_back({"ok", "required_file_ok", "file exists: " + path.string()});
        }
    }

    for (const auto& writablePath : {root, root / "config.yaml", root / "sub.yaml", root / "outputs", root / "cache", root / "state"}) {
        if (canWritePath(writablePath)) {
            findings.push_back({"ok", "writable", "writeable: " + writablePath.string()});
        } else {
            findings.push_back({"fail", "not_writable", "not writeable: " + writablePath.string()});
        }
    }
    return findings;
}

void printDaemonUsage() {
    std::cout << "Usage:\n"
              << "  subcli daemon <once|run|start|stop|status> [--interval SEC] [--target all|mihomo|sing-box|xray]\n"
              << "               [--update-assets] [--strict-network] [--check] [--no-restart]\n"
              << "               [--pid-file PATH] [--log-file PATH]\n"
              << "\n"
              << "Optional helper for local process hosting.\n"
              << "Primary product workflow is config generation with 'subcli export ...'.\n"
              << "\n"
              << "Examples:\n"
              << "  subcli daemon once --target all\n"
              << "  subcli daemon run --interval 3600 --target all --update-assets\n";
}

void printCheckUsage() {
    std::cout << "Usage:\n"
              << "  subcli check <mihomo|sing-box|xray> [--file PATH] [--output-dir DIR] [--timeout SEC]\n"
              << "\n"
              << "Examples:\n"
              << "  subcli check sing-box --file ./outputs/sing-box.json --timeout 30\n"
              << "  subcli check mihomo --output-dir ./outputs\n";
}

void printRunUsage() {
    std::cout << "Usage:\n"
              << "  subcli run <mihomo|sing-box|xray> [--file PATH] [--output-dir DIR]\n"
              << "\n"
              << "Examples:\n"
              << "  subcli run sing-box\n"
              << "  subcli run xray --file ./outputs/xray.json\n";
}

void printStopUsage() {
    std::cout << "Usage:\n"
              << "  subcli stop <mihomo|sing-box|xray>\n"
              << "\n"
              << "Example:\n"
              << "  subcli stop sing-box\n";
}

void printStatusUsage() {
    std::cout << "Usage:\n"
              << "  subcli status [mihomo|sing-box|xray]\n"
              << "\n"
              << "Examples:\n"
              << "  subcli status\n"
              << "  subcli status sing-box\n";
}

void printRestartUsage() {
    std::cout << "Usage:\n"
              << "  subcli restart <mihomo|sing-box|xray> [--file PATH] [--output-dir DIR]\n"
              << "\n"
              << "Examples:\n"
              << "  subcli restart sing-box\n"
              << "  subcli restart xray --output-dir ./outputs\n";
}

void printCompletionUsage() {
    std::cout << "Usage:\n"
              << "  subcli completion bash\n"
              << "\n"
              << "Example:\n"
              << "  subcli completion bash > ~/.local/share/bash-completion/completions/subcli\n";
}

bool isHelpToken(const std::string& token) {
    return token == "--help" || token == "-h" || token == "help";
}

bool isKnownSubcommand(const std::string& cmd, const std::vector<std::string>& values) {
    return std::find(values.begin(), values.end(), cmd) != values.end();
}

int doSubCommand(const std::vector<std::string>& args);
int doExportCommand(const std::vector<std::string>& args);
int doRestartCommand(const std::vector<std::string>& args);
bool parseExportTarget(const std::string& value, ExportTarget& target, std::string& outFile);

void printExportTargetUsage(const std::string& target) {
    std::cout << "Usage:\n  subcli export " << target
              << " [--tun] [--check] [--check-timeout SEC] [--output-dir DIR] [--profile PATH_OR_NAME] [--sub ID_OR_NAME] [--tag TAG] [--strict-network] [--strict-capabilities] [--download-assets] [--explain-policy] [--json]\n";
}

void printSubCommandUsageLine(const std::string& cmd) {
    if (cmd == "list") {
        std::cout << "Usage:\n  subcli sub list [--json]\n";
    } else if (cmd == "add") {
        std::cout << "Usage:\n  subcli sub add --name NAME --url URL [options]\n";
    } else if (cmd == "edit") {
        std::cout << "Usage:\n  subcli sub edit <id|name> [options]\n";
    } else if (cmd == "remove") {
        std::cout << "Usage:\n  subcli sub remove <id|name>\n";
    } else if (cmd == "enable" || cmd == "disable") {
        std::cout << "Usage:\n  subcli sub " << cmd << " <id|name>\n";
    } else if (cmd == "update") {
        std::cout << "Usage:\n  subcli sub update [id-or-name ...] [--tag TAG] [--strict-network]\n";
    } else if (cmd == "validate") {
        std::cout << "Usage:\n  subcli sub validate [id-or-name]\n";
    }
}

void printConfigSubcommandUsage(const std::string& cmd) {
    if (cmd == "list") {
        std::cout << "Usage:\n  subcli config list [--json]\n";
    } else if (cmd == "get") {
        std::cout << "Usage:\n  subcli config get <key>\n";
    } else if (cmd == "set") {
        std::cout << "Usage:\n  subcli config set <key> <value>\n";
    } else if (cmd == "remove") {
        std::cout << "Usage:\n  subcli config remove <key>\n";
    }
}

void printTemplateSubcommandUsage(const std::string& cmd) {
    if (cmd == "list") {
        std::cout << "Usage:\n  subcli template list [--json]\n";
    } else if (cmd == "get") {
        std::cout << "Usage:\n  subcli template get <target> <kind>\n";
    } else if (cmd == "set") {
        std::cout << "Usage:\n  subcli template set <target> <kind> <path>\n";
    } else if (cmd == "reset") {
        std::cout << "Usage:\n  subcli template reset [target] [kind]\n";
    } else if (cmd == "validate") {
        std::cout << "Usage:\n  subcli template validate [--json]\n";
    }
}

void printAssetSubcommandUsage(const std::string& cmd) {
    if (cmd == "list") {
        std::cout << "Usage:\n  subcli asset list\n";
    } else if (cmd == "status") {
        std::cout << "Usage:\n  subcli asset status\n";
    } else if (cmd == "validate") {
        std::cout << "Usage:\n  subcli asset validate\n";
    } else if (cmd == "update") {
        std::cout << "Usage:\n  subcli asset update [asset-key]\n";
    }
}

void printProfileSubcommandUsage(const std::string& cmd) {
    if (cmd == "list") {
        std::cout << "Usage:\n  subcli profile list\n";
    } else if (cmd == "get") {
        std::cout << "Usage:\n  subcli profile get <bypass-cn|global|direct>\n";
    } else if (cmd == "validate") {
        std::cout << "Usage:\n  subcli profile validate <path>\n";
    } else if (cmd == "explain") {
        std::cout << "Usage:\n  subcli profile explain <path-or-name> [--target <all|mihomo|sing-box|xray>] [--json]\n";
    }
}

void printWorkspaceSubcommandUsage(const std::string& cmd) {
    if (cmd == "init") {
        std::cout << "Usage:\n  subcli workspace init [DIR]\n";
    } else if (cmd == "status") {
        std::cout << "Usage:\n  subcli workspace status [--json]\n";
    } else if (cmd == "use") {
        std::cout << "Usage:\n  subcli workspace use DIR\n";
    } else if (cmd == "unset") {
        std::cout << "Usage:\n  subcli workspace unset\n";
    } else if (cmd == "migrate") {
        std::cout << "Usage:\n  subcli workspace migrate [--to DIR] [--from DIR] [--dry-run] [--overwrite]\n";
    } else if (cmd == "doctor") {
        std::cout << "Usage:\n  subcli workspace doctor\n";
    }
}

void printDaemonSubcommandUsage(const std::string& cmd) {
    if (cmd == "once" || cmd == "run" || cmd == "start") {
        std::cout << "Usage:\n  subcli daemon " << cmd
                  << " [--interval SEC] [--target all|mihomo|sing-box|xray] [--update-assets] [--strict-network] [--check] [--no-restart]\n";
    } else if (cmd == "stop" || cmd == "status") {
        std::cout << "Usage:\n  subcli daemon " << cmd << " [--pid-file PATH]\n";
    }
}

std::string daemonModeFromCommands(CLI::App* onceCmd, CLI::App* runCmd, CLI::App* startCmd, CLI::App* stopCmd, CLI::App* statusCmd) {
    if (*onceCmd) return "once";
    if (*runCmd) return "run";
    if (*startCmd) return "start";
    if (*stopCmd) return "stop";
    if (*statusCmd) return "status";
    return "";
}

bool validateDaemonTarget(const std::string& target) {
    if (target == "all") {
        return true;
    }
    ExportTarget parsedTarget;
    std::string defaultFile;
    if (!parseExportTarget(target, parsedTarget, defaultFile)) {
        std::cerr << "invalid daemon target: " << target << "\n";
        return false;
    }
    return true;
}

DaemonOptions buildDaemonOptionsFromCli(int intervalSec, const std::string& target, bool updateAssets, bool strictNetwork, bool check, bool noRestart, const std::string& pidFile, const std::string& logFile) {
    DaemonOptions options;
    options.intervalSec = intervalSec;
    options.exportTarget = target;
    options.updateAssets = updateAssets;
    options.strictNetwork = strictNetwork;
    options.check = check;
    options.restartRunning = !noRestart;
    options.pidFile = pidFile;
    options.logFile = logFile;
    return options;
}

DaemonCallbacks makeDaemonCallbacks() {
    DaemonCallbacks callbacks;
    callbacks.runSubCommand = [&](const std::vector<std::string>& subArgs) { return doSubCommand(subArgs); };
    callbacks.runExportCommand = [&](const std::vector<std::string>& exportArgs) { return doExportCommand(exportArgs); };
    callbacks.isCoreRunning = [&](const std::string& coreTarget, std::string& error) {
        const auto status = inspectCoreRuntime(gPaths.stateDir, coreTarget, error);
        return status.running;
    };
    callbacks.runRestartCommand = [&](const std::vector<std::string>& restartArgs) { return doRestartCommand(restartArgs); };
    return callbacks;
}

void printDaemonLastCycle(const DaemonProcessStatus& status) {
    if (status.lastCycleMessage.empty()) {
        return;
    }
    if (status.lastCycleExitCode == 0) {
        std::cout << "\tlast=ok";
    } else {
        std::cout << "\tlast=failed(" << status.lastCycleMessage << ")";
    }
}

int doDaemonStatusMode() {
    std::string error;
    const auto status = inspectDaemonProcess(gPaths.stateDir, error);
    if (!error.empty()) {
        std::cerr << "daemon status failed: " << error << "\n";
        return ExitError;
    }
    if (!status.hasState) {
        std::cout << "daemon\tstopped\n";
        return ExitOk;
    }
    if (status.running) {
        std::cout << "daemon\trunning\tpid=" << status.pid << "\tinterval=" << status.options.intervalSec << "\ttarget=" << status.options.exportTarget;
        printDaemonLastCycle(status);
        std::cout << "\n";
        return ExitOk;
    }
    std::cout << "daemon\tstale\tpid=" << status.pid;
    printDaemonLastCycle(status);
    std::cout << "\n";
    return ExitOk;
}

int doDaemonStopMode() {
    std::string error;
    if (!stopDaemonProcess(gPaths.stateDir, 5, error)) {
        std::cerr << "daemon stop failed: " << error << "\n";
        return ExitError;
    }
    std::cout << "daemon stopped\n";
    return ExitOk;
}

int doDaemonStartMode(const DaemonOptions& options) {
    if (gExecutablePath.empty()) {
        std::cerr << "cannot resolve subcli executable path\n";
        return ExitError;
    }
    std::string error;
    if (!startDaemonProcess(gPaths.stateDir, gExecutablePath, buildDaemonRunArgs(options), options, error)) {
        std::cerr << "daemon start failed: " << error << "\n";
        return ExitError;
    }
    const auto status = inspectDaemonProcess(gPaths.stateDir, error);
    if (!error.empty()) {
        std::cerr << "daemon status read failed: " << error << "\n";
        return ExitError;
    }
    std::cout << "daemon started pid=" << status.pid << "\n";
    return ExitOk;
}

int doDaemonOnceMode(const DaemonOptions& options, const DaemonCallbacks& callbacks) {
    const int rc = runDaemonCycleWithState(gPaths.stateDir, options, callbacks);
    if (rc == 0) {
        std::cout << "daemon cycle completed\n";
        return ExitOk;
    }
    std::cerr << "daemon cycle failed\n";
    return ExitError;
}

int runDaemonLoopMode(const DaemonOptions& options, const DaemonCallbacks& callbacks) {
    while (true) {
        const int rc = runDaemonCycleWithState(gPaths.stateDir, options, callbacks);
        if (rc != 0) {
            std::cerr << "daemon cycle failed with code " << rc << "\n";
        }
        std::this_thread::sleep_for(std::chrono::seconds(options.intervalSec));
    }
}

bool hasOption(const std::vector<std::string>& args, const std::string& key) {
    for (size_t i = 0; i + 1 < args.size(); ++i) {
        if (args[i] == key) {
            return true;
        }
    }
    return false;
}

std::vector<std::string> argValues(const std::vector<std::string>& args, const std::string& key) {
    std::vector<std::string> out;
    for (size_t i = 0; i + 1 < args.size(); ++i) {
        if (args[i] == key) {
            out.push_back(args[i + 1]);
        }
    }
    return out;
}

std::vector<std::string> splitComma(const std::string& s) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, ',')) {
        if (!item.empty()) {
            out.push_back(item);
        }
    }
    return out;
}

std::string trimString(std::string v) {
    while (!v.empty() && std::isspace(static_cast<unsigned char>(v.back()))) {
        v.pop_back();
    }
    size_t i = 0;
    while (i < v.size() && std::isspace(static_cast<unsigned char>(v[i]))) {
        ++i;
    }
    return v.substr(i);
}

bool parseHeaderOption(const std::string& raw, std::string& key, std::string& value) {
    size_t sep = raw.find(':');
    if (sep == std::string::npos) {
        sep = raw.find('=');
    }
    if (sep == std::string::npos) {
        std::cerr << "invalid header, expected 'Key: Value' or 'Key=Value': " << raw << "\n";
        return false;
    }
    key = trimString(raw.substr(0, sep));
    value = trimString(raw.substr(sep + 1));
    if (key.empty()) {
        std::cerr << "invalid header: empty key\n";
        return false;
    }
    return true;
}

bool parseIntValue(const std::string& raw, const std::string& key, int& out) {
    try {
        size_t consumed = 0;
        const int value = std::stoi(raw, &consumed);
        if (consumed != raw.size()) {
            std::cerr << "invalid integer for " << key << ": " << raw << "\n";
            return false;
        }
        out = value;
        return true;
    } catch (...) {
        std::cerr << "invalid integer for " << key << ": " << raw << "\n";
        return false;
    }
}

bool parseBoundedIntValue(const std::string& raw, const std::string& key, int minValue, int& out) {
    int parsed = 0;
    if (!parseIntValue(raw, key, parsed)) {
        return false;
    }
    if (parsed < minValue) {
        std::cerr << key << " must be >= " << minValue << "\n";
        return false;
    }
    out = parsed;
    return true;
}

bool parseBoolValue(const std::string& raw, const std::string& key, bool& out) {
    std::string value = raw;
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (value == "true" || value == "1" || value == "yes" || value == "on") {
        out = true;
        return true;
    }
    if (value == "false" || value == "0" || value == "no" || value == "off") {
        out = false;
        return true;
    }
    std::cerr << "invalid boolean for " << key << ": " << raw << "\n";
    return false;
}

bool parseCliArgs(CLI::App& app, const std::vector<std::string>& args) {
    std::vector<std::string> owned = args;
    std::vector<char*> argv;
    argv.reserve(owned.size());
    for (auto& s : owned) {
        argv.push_back(s.data());
    }
    try {
        app.parse(static_cast<int>(argv.size()), argv.data());
        return true;
    } catch (const CLI::ParseError&) {
        return false;
    }
}

Subscription* findSub(std::vector<Subscription>& subs, const std::string& idOrName) {
    auto it = std::find_if(subs.begin(), subs.end(), [&](const Subscription& s) {
        return s.id == idOrName || s.name == idOrName;
    });
    return it == subs.end() ? nullptr : &(*it);
}

bool subscriptionIdOrNameExists(
    const std::vector<Subscription>& subs,
    const std::string& id,
    const std::string& name,
    const std::string& ignoreId = ""
) {
    return std::any_of(subs.begin(), subs.end(), [&](const Subscription& s) {
        if (!ignoreId.empty() && s.id == ignoreId) {
            return false;
        }
        return (!id.empty() && s.id == id) || (!name.empty() && s.name == name);
    });
}

bool isTemplateTarget(const std::string& target) {
    return target == "mihomo" || target == "sing-box" || target == "xray";
}

bool parseTemplateTargetKind(const std::string& target, const std::string& kind, std::string& error) {
    if (!isTemplateTarget(target)) {
        error = "invalid template target: " + target;
        return false;
    }
    if (kind != "normal" && kind != "tun") {
        error = "invalid template kind: " + kind;
        return false;
    }
    error.clear();
    return true;
}

std::string defaultTemplateFilenameFor(const std::string& target, const std::string& kind) {
    if (target == "mihomo") {
        return kind == "tun" ? "mihomo_tun.yaml" : "mihomo_base.yaml";
    }
    if (target == "sing-box") {
        return kind == "tun" ? "singbox_tun.json" : "singbox_base.json";
    }
    return kind == "tun" ? "xray_tun.json" : "xray_base.json";
}

std::string getTemplateValue(const AppConfig& cfg, const std::string& target, const std::string& kind) {
    const auto& values = kind == "tun" ? cfg.templateTun : cfg.templateNormal;
    auto it = values.find(target);
    return it == values.end() ? "" : it->second;
}

void setTemplateValue(AppConfig& cfg, const std::string& target, const std::string& kind, const std::string& value) {
    if (kind == "tun") {
        cfg.templateTun[target] = value;
    } else {
        cfg.templateNormal[target] = value;
    }
}

void resetTemplateValue(AppConfig& cfg, const std::string& target, const std::string& kind) {
    setTemplateValue(cfg, target, kind, defaultTemplatePath(cfg.templateDir, defaultTemplateFilenameFor(target, kind)));
}

std::vector<std::pair<std::string, std::string>> templateTargetKinds() {
    return {{"mihomo", "normal"}, {"mihomo", "tun"}, {"sing-box", "normal"}, {"sing-box", "tun"}, {"xray", "normal"}, {"xray", "tun"}};
}

nlohmann::json configToJson(const AppConfig& cfg) {
    nlohmann::json templates = nlohmann::json::object();
    for (const auto& item : templateTargetKinds()) {
        templates[item.first][item.second] = getTemplateValue(cfg, item.first, item.second);
    }

    nlohmann::json regions = nlohmann::json::object();
    for (const auto& kv : cfg.regionRules) {
        regions[kv.first] = kv.second;
    }
    nlohmann::json assetPaths = nlohmann::json::object();
    for (const auto& kv : cfg.assetPaths) {
        assetPaths[kv.first] = kv.second;
    }
    nlohmann::json assetUrls = nlohmann::json::object();
    for (const auto& kv : cfg.assetUrls) {
        assetUrls[kv.first] = kv.second;
    }

    return {
        {"tun", cfg.tun},
        {"profile", cfg.profile},
        {"profile_path", cfg.profilePath},
        {"output_dir", cfg.outputDir},
        {"template_dir", cfg.templateDir},
        {"asset_dir", cfg.assetDir},
        {"parallelism", cfg.parallelism},
        {"timeout", cfg.timeout},
        {"retry", cfg.retry},
        {"fetch_max_bytes", cfg.fetchMaxBytes},
        {"log_level", cfg.logLevel},
        {"core_paths", {{"mihomo", cfg.mihomoPath}, {"sing_box", cfg.singBoxPath}, {"xray", cfg.xrayPath}}},
        {"node_management", {{"dedupe", cfg.dedupeNodes}, {"rename_template", cfg.renameTemplate}, {"include_regex", cfg.includeRegex}, {"exclude_regex", cfg.excludeRegex}, {"sort_by", cfg.sortBy}}},
        {"templates", templates},
        {"assets", {{"paths", assetPaths}, {"urls", assetUrls}}},
        {"grouping", {{"region_rules", regions}}},
    };
}

nlohmann::json subscriptionsToJson(const std::vector<Subscription>& subs) {
    nlohmann::json items = nlohmann::json::array();
    for (const auto& s : subs) {
        items.push_back({
            {"id", s.id},
            {"name", s.name},
            {"url", s.url},
            {"enabled", s.enabled},
            {"group", s.group},
            {"priority", s.priority},
            {"update_interval", s.updateInterval},
            {"timeout", s.timeout},
            {"retry", s.retry},
            {"tags", s.tags},
            {"last_success", s.lastSuccess},
            {"last_error", s.lastError},
        });
    }
    return {{"subscriptions", items}};
}

Subscription withEffectiveNetworkPolicy(const Subscription& base, const AppConfig& cfg) {
    Subscription out = base;
    out.timeout = std::max(1, base.timeoutOverride ? base.timeout : cfg.timeout);
    out.retry = std::max(0, base.retryOverride ? base.retry : cfg.retry);
    out.fetchMaxBytes = std::max<long>(1024, cfg.fetchMaxBytes);
    out.cachePath = resolveCachePath(out.cachePath);
    return out;
}

bool shouldSkipByUpdateInterval(const Subscription& s, std::time_t now) {
    if (s.updateInterval <= 0 || s.lastSuccess.empty()) {
        return false;
    }
    std::time_t last = 0;
    if (!parseIso8601(s.lastSuccess, last)) {
        return false;
    }
    return now - last < s.updateInterval;
}

template <typename Fn>
void runParallelJobs(size_t total, int parallelism, Fn&& fn) {
    const int workers = std::max(1, std::min<int>(parallelism, static_cast<int>(total)));
    std::atomic<size_t> next{0};
    std::vector<std::future<void>> futures;
    futures.reserve(static_cast<size_t>(workers));
    for (int i = 0; i < workers; ++i) {
        futures.push_back(std::async(std::launch::async, [&]() {
            while (true) {
                const size_t idx = next.fetch_add(1);
                if (idx >= total) {
                    break;
                }
                fn(idx);
            }
        }));
    }
    for (auto& f : futures) {
        f.get();
    }
}

ParseResult parseSubscriptionForSub(const Subscription& s, const std::string& content, const AppConfig& cfg) {
    return parseSubscription(content, s.id, s.formatHint, cfg);
}

bool hasUsableParsedNodes(const ParseResult& parsed, std::string& reason) {
    if (!parsed.nodes.empty()) {
        reason.clear();
        return true;
    }
    reason = "parse yielded no supported nodes";
    return false;
}

bool parseExportTarget(const std::string& value, ExportTarget& target, std::string& outFile) {
    if (value == "mihomo") {
        target = ExportTarget::Mihomo;
        outFile = "mihomo.yaml";
        return true;
    }
    if (value == "sing-box") {
        target = ExportTarget::SingBox;
        outFile = "sing-box.json";
        return true;
    }
    if (value == "xray") {
        target = ExportTarget::Xray;
        outFile = "xray.json";
        return true;
    }
    return false;
}

int runConfigCheckForTarget(const AppConfig& cfg, ExportTarget target, const std::string& filePath, int timeoutSec = 30) {
    const auto cores = discoverCorePaths(cfg);
    CoreCheckResult result;
    if (target == ExportTarget::Mihomo) {
        result = runMihomoConfigCheck(cores.mihomo, filePath, timeoutSec);
    } else if (target == ExportTarget::SingBox) {
        result = runSingBoxConfigCheck(cores.singBox, filePath, timeoutSec);
    } else {
        result = runXrayConfigCheck(cores.xray, filePath, timeoutSec);
    }

    if (!result.ok) {
        std::cerr << "check failed: " << result.message << "\n";
        return 1;
    }
    std::cout << "check passed: " << filePath << "\n";
    return 0;
}

CoreCheckResult runConfigCheckForTargetResult(const AppConfig& cfg, ExportTarget target, const std::string& filePath, int timeoutSec = 30) {
    const auto cores = discoverCorePaths(cfg);
    if (target == ExportTarget::Mihomo) {
        return runMihomoConfigCheck(cores.mihomo, filePath, timeoutSec);
    }
    if (target == ExportTarget::SingBox) {
        return runSingBoxConfigCheck(cores.singBox, filePath, timeoutSec);
    }
    return runXrayConfigCheck(cores.xray, filePath, timeoutSec);
}

std::string coreBinaryForTarget(const CorePaths& cores, ExportTarget target) {
    if (target == ExportTarget::Mihomo) {
        return cores.mihomo;
    }
    if (target == ExportTarget::SingBox) {
        return cores.singBox;
    }
    return cores.xray;
}

std::vector<std::string> runtimeArgsForTarget(ExportTarget target, const std::string& configPath) {
    if (target == ExportTarget::Mihomo) {
        return {"-f", configPath};
    }
    if (target == ExportTarget::SingBox) {
        return {"run", "-c", configPath};
    }
    return {"run", "-config", configPath};
}

std::string runtimeConfigPathFromArgs(const std::vector<std::string>& args, const AppConfig& cfg, const std::string& defaultFile) {
    if (hasOption(args, "--file")) {
        return resolveFromCliCwd(argValue(args, "--file"));
    }
    std::string outputDir;
    if (hasOption(args, "--output-dir")) {
        outputDir = resolveFromCliCwd(argValue(args, "--output-dir", cfg.outputDir));
    } else {
        outputDir = cfg.outputDir;
    }
    return (std::filesystem::path(outputDir) / defaultFile).string();
}

int startRuntimeForTarget(const std::vector<std::string>& args, bool restart) {
    CLI::App parser(restart ? "restart" : "run");
    parser.set_help_flag("");
    parser.allow_extras(false);
    std::string targetName;
    std::string fileOpt;
    std::string outputDirOpt;
    parser.add_option("target", targetName);
    parser.add_option("--file", fileOpt);
    parser.add_option("--output-dir", outputDirOpt);
    if (!parseCliArgs(parser, args)) {
        return ExitUsage;
    }
    if (targetName.empty()) {
        return ExitUsage;
    }

    ExportTarget target;
    std::string defaultFile;
    if (!parseExportTarget(targetName, target, defaultFile)) {
        std::cerr << "unknown target: " << targetName << "\n";
        return ExitUsage;
    }

    ensureDefaults();
    auto cfg = loadConfig(gPaths.configPath.string());
    applyConfigDefaults(cfg);

    std::vector<std::string> normalized = {args[0], targetName};
    if (!fileOpt.empty()) {
        normalized.push_back("--file");
        normalized.push_back(fileOpt);
    }
    if (!outputDirOpt.empty()) {
        normalized.push_back("--output-dir");
        normalized.push_back(outputDirOpt);
    }

    const std::string configPath = runtimeConfigPathFromArgs(normalized, cfg, defaultFile);
    if (!fileExists(configPath)) {
        std::cerr << "runtime config does not exist: " << configPath << "\n";
        return ExitError;
    }

    const auto cores = discoverCorePaths(cfg);
    const std::string binary = coreBinaryForTarget(cores, target);
    if (binary.empty()) {
        std::cerr << "core binary not found for target: " << targetName << "\n";
        return ExitError;
    }
    if (!isExecutableFile(binary)) {
        std::cerr << "core binary is not executable: " << binary << "\n";
        return ExitError;
    }

    std::string error;
    if (restart && !stopCoreRuntime(gPaths.stateDir, targetName, 5, error)) {
        std::cerr << "restart stop failed: " << error << "\n";
        return ExitError;
    }
    if (!startCoreRuntime(gPaths.stateDir, targetName, binary, runtimeArgsForTarget(target, configPath), configPath, error)) {
        std::cerr << "runtime start failed: " << error << "\n";
        return ExitError;
    }

    const auto status = inspectCoreRuntime(gPaths.stateDir, targetName, error);
    if (!error.empty()) {
        std::cerr << "runtime status read failed: " << error << "\n";
        return ExitError;
    }
    std::cout << "running " << targetName << " pid=" << status.pid << " config=" << configPath << "\n";
    return ExitOk;
}

int doRunCommand(const std::vector<std::string>& args) {
    if (hasHelp(args)) {
        printRunUsage();
        return ExitOk;
    }
    if (args.size() < 2) {
        printRunUsage();
        return ExitUsage;
    }
    return startRuntimeForTarget(args, false);
}

int doRestartCommand(const std::vector<std::string>& args) {
    if (hasHelp(args)) {
        printRestartUsage();
        return ExitOk;
    }
    if (args.size() < 2) {
        printRestartUsage();
        return ExitUsage;
    }
    return startRuntimeForTarget(args, true);
}

int doStopCommand(const std::vector<std::string>& args) {
    if (hasHelp(args)) {
        printStopUsage();
        return ExitOk;
    }
    CLI::App parser("stop");
    parser.set_help_flag("");
    parser.allow_extras(false);
    std::string targetName;
    parser.add_option("target", targetName);
    if (!parseCliArgs(parser, args) || targetName.empty()) {
        printStopUsage();
        return ExitUsage;
    }
    ExportTarget target;
    std::string defaultFile;
    if (!parseExportTarget(targetName, target, defaultFile)) {
        std::cerr << "unknown target: " << targetName << "\n";
        return ExitUsage;
    }
    std::string error;
    if (!stopCoreRuntime(gPaths.stateDir, targetName, 5, error)) {
        std::cerr << "runtime stop failed: " << error << "\n";
        return ExitError;
    }
    std::cout << "stopped " << targetName << "\n";
    return ExitOk;
}

int printRuntimeStatus(const std::string& target) {
    std::string error;
    const auto status = inspectCoreRuntime(gPaths.stateDir, target, error);
    if (!error.empty()) {
        std::cerr << "runtime status failed for " << target << ": " << error << "\n";
        return ExitError;
    }
    std::cout << target << "\t";
    if (status.running) {
        std::cout << "running\tpid=" << status.pid << "\tconfig=" << status.configPath << "\n";
    } else {
        std::cout << "stopped\n";
    }
    return ExitOk;
}

int doStatusCommand(const std::vector<std::string>& args) {
    if (hasHelp(args)) {
        printStatusUsage();
        return ExitOk;
    }
    CLI::App parser("status");
    parser.set_help_flag("");
    parser.allow_extras(false);
    std::string targetName;
    parser.add_option("target", targetName)->required(false);
    if (!parseCliArgs(parser, args)) {
        return ExitUsage;
    }
    if (targetName.empty()) {
        int rc = ExitOk;
        rc = std::max(rc, printRuntimeStatus("mihomo"));
        rc = std::max(rc, printRuntimeStatus("sing-box"));
        rc = std::max(rc, printRuntimeStatus("xray"));
        return rc;
    }
    ExportTarget target;
    std::string defaultFile;
    if (!parseExportTarget(targetName, target, defaultFile)) {
        std::cerr << "unknown target: " << targetName << "\n";
        return ExitUsage;
    }
    return printRuntimeStatus(targetName);
}

int doInitCommand(const std::vector<std::string>& args) {
    if (hasHelp(args)) {
        printInitUsage();
        return 0;
    }
    ensureDefaults();
    std::cout << "initialized subcli\n";
    std::cout << "config_dir=" << gPaths.configDir.string() << "\n";
    std::cout << "data_dir=" << gPaths.dataDir.string() << "\n";
    std::cout << "cache_dir=" << gPaths.cacheDir.string() << "\n";
    std::cout << "state_dir=" << gPaths.stateDir.string() << "\n";
    std::cout << "template_dir=" << gPaths.templateDir.string() << "\n";
    std::cout << "output_dir=" << gPaths.outputDir.string() << "\n";
    return 0;
}

bool checkDirWritable(const std::filesystem::path& dir, std::string& reason) {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        reason = "cannot create directory: " + ec.message();
        return false;
    }
    const auto probe = dir / ".subcli-write-test";
    {
        std::ofstream out(probe, std::ios::binary);
        if (!out) {
            reason = "cannot write probe file";
            return false;
        }
        out << "ok";
    }
    std::filesystem::remove(probe, ec);
    reason.clear();
    return true;
}

std::vector<std::pair<std::string, std::string>> requiredTemplateFiles(const AppConfig& cfg) {
    std::vector<std::pair<std::string, std::string>> files;
    auto add = [&](const std::string& label, const std::map<std::string, std::string>& values, const std::string& key) {
        auto it = values.find(key);
        files.push_back({label, it == values.end() ? "" : it->second});
    };
    add("templates.mihomo.normal", cfg.templateNormal, "mihomo");
    add("templates.mihomo.tun", cfg.templateTun, "mihomo");
    add("templates.sing-box.normal", cfg.templateNormal, "sing-box");
    add("templates.sing-box.tun", cfg.templateTun, "sing-box");
    add("templates.xray.normal", cfg.templateNormal, "xray");
    add("templates.xray.tun", cfg.templateTun, "xray");
    return files;
}

int doDoctorCommand(const std::vector<std::string>& args) {
    if (hasHelp(args)) {
        printDoctorUsage();
        return 0;
    }
    CLI::App parser("doctor");
    parser.set_help_flag("");
    parser.allow_extras(false);
    bool jsonOutput = false;
    parser.add_flag("--json", jsonOutput);
    if (!parseCliArgs(parser, args)) {
        return 1;
    }
    ensureDefaults();
    int failed = 0;
    nlohmann::json checks = nlohmann::json::array();

    auto addCheck = [&](const std::string& name, const std::string& status, const std::filesystem::path& path, const std::string& message = "") {
        checks.push_back({{"name", name}, {"status", status}, {"path", path.string()}, {"message", message}});
        if (!jsonOutput) {
            if (status == "ok") {
                std::cout << "[ OK ] " << name << (path.empty() ? "" : "=" + path.string()) << (message.empty() ? "" : " " + message) << "\n";
            } else if (status == "warn") {
                std::cout << "[WARN] " << name << (path.empty() ? "" : "=" + path.string()) << (message.empty() ? "" : " " + message) << "\n";
            } else {
                std::cout << "[FAIL] " << name << (path.empty() ? "" : "=" + path.string()) << (message.empty() ? "" : " (" + message + ")") << "\n";
            }
        }
    };

    auto checkPath = [&](const std::string& name, const std::filesystem::path& path, bool mustExist) {
        std::error_code ec;
        const bool exists = std::filesystem::exists(path, ec);
        if (ec || (mustExist && !exists)) {
            ++failed;
            addCheck(name, "fail", path, ec ? ec.message() : "missing");
            return;
        }
        addCheck(name, "ok", path);
    };

    checkPath("config_path", gPaths.configPath, true);
    checkPath("sub_path", gPaths.subPath, true);
    checkPath("template_dir", gPaths.templateDir, true);
    checkPath("output_dir", gPaths.outputDir, true);

    auto checkWritable = [&](const std::string& name, const std::filesystem::path& path) {
        std::string reason;
        if (!checkDirWritable(path, reason)) {
            ++failed;
            addCheck(name, "fail", path, reason);
            return;
        }
        addCheck(name, "ok", path, "writable");
    };

    checkWritable("config_dir", gPaths.configDir);
    checkWritable("data_dir", gPaths.dataDir);
    checkWritable("cache_dir", gPaths.cacheDir);
    checkWritable("output_dir", gPaths.outputDir);

    AppConfig cfg = loadConfig(gPaths.configPath.string());
    applyConfigDefaults(cfg);
    for (const auto& item : requiredTemplateFiles(cfg)) {
        std::error_code ec;
        if (item.second.empty() || !std::filesystem::is_regular_file(item.second, ec) || ec) {
            ++failed;
            addCheck(item.first, "fail", item.second, ec ? ec.message() : "missing");
        } else {
            addCheck(item.first, "ok", item.second);
        }
    }
    const auto cores = discoverCorePaths(cfg);

    auto checkCore = [&](const std::string& key, const std::string& value) {
        if (value.empty()) {
            addCheck(key, "warn", {}, "not found in PATH and not configured");
            return;
        }
        if (!isExecutableFile(value)) {
            ++failed;
            addCheck(key, "fail", value, "not executable");
            return;
        }
        addCheck(key, "ok", value);
    };

    checkCore("core.mihomo", cores.mihomo);
    checkCore("core.sing-box", cores.singBox);
    checkCore("core.xray", cores.xray);

    if (jsonOutput) {
        nlohmann::json envJson = {
            {"resolution_source", environmentSourceToString(gEnvResult.source)},
            {"active_workspace_root", gEnvResult.root.empty() ? gPaths.root.string() : gEnvResult.root},
            {"resolved_path_map", {
                {"config_dir", gPaths.configDir.string()},
                {"data_dir", gPaths.dataDir.string()},
                {"cache_dir", gPaths.cacheDir.string()},
                {"state_dir", gPaths.stateDir.string()},
                {"output_dir", gPaths.outputDir.string()},
                {"template_dir", gPaths.templateDir.string()},
                {"profile_dir", gPaths.profileDir.string()},
                {"sub_path", gPaths.subPath.string()},
                {"config_path", gPaths.configPath.string()},
            }},
        };
        if (!gEnvResult.trace.empty()) {
            envJson["trace"] = gEnvResult.trace;
        }
        printJsonLine({{"failed", failed}, {"checks", checks}, {"environment", envJson}});
    } else {
        std::cout << "doctor summary: failed=" << failed << "\n";
    }
    return failed == 0 ? 0 : 1;
}

int doCheckCommand(const std::vector<std::string>& args) {
    if (hasHelp(args)) {
        printCheckUsage();
        return 0;
    }
    ensureDefaults();
    if (args.size() < 2) {
        printCheckUsage();
        return 1;
    }

    CLI::App parser("check");
    parser.set_help_flag("");
    parser.allow_extras(false);
    std::string targetName;
    std::string fileOpt;
    std::string outputDirOpt;
    std::string timeoutOpt;
    parser.add_option("target", targetName);
    parser.add_option("--file", fileOpt);
    parser.add_option("--output-dir", outputDirOpt);
    parser.add_option("--timeout", timeoutOpt);
    if (!parseCliArgs(parser, args)) {
        return ExitUsage;
    }
    if (targetName.empty()) {
        return ExitUsage;
    }

    AppConfig cfg = loadConfig(gPaths.configPath.string());
    applyConfigDefaults(cfg);
    int timeoutSec = 30;
    if (!timeoutOpt.empty() && !parseBoundedIntValue(timeoutOpt, "--timeout", 1, timeoutSec)) {
        return 1;
    }

    ExportTarget target;
    std::string defaultFile;
    if (!parseExportTarget(targetName, target, defaultFile)) {
        std::cerr << "unknown check target: " << targetName << "\n";
        return ExitUsage;
    }

    std::string filePath = fileOpt;
    if (filePath.empty()) {
        std::string outputDir;
        if (!outputDirOpt.empty()) {
            outputDir = resolveFromCliCwd(outputDirOpt);
        } else {
            outputDir = cfg.outputDir;
        }
        filePath = (std::filesystem::path(outputDir) / defaultFile).string();
    } else {
        filePath = resolveFromCliCwd(filePath);
    }
    if (!fileExists(filePath)) {
        std::cerr << "check file does not exist: " << filePath << "\n";
        return 1;
    }
    return runConfigCheckForTarget(cfg, target, filePath, timeoutSec);
}

int doCompletionCommand(const std::vector<std::string>& args) {
    if (hasHelp(args)) {
        printCompletionUsage();
        return 0;
    }
    CLI::App parser("completion");
    parser.set_help_flag("");
    parser.allow_extras(false);
    std::string shell;
    parser.add_option("shell", shell);
    if (!parseCliArgs(parser, args) || shell.empty()) {
        printCompletionUsage();
        return 1;
    }
    if (shell != "bash") {
        std::cerr << "unsupported completion shell: " << shell << "\n";
        return 1;
    }
    std::cout << generateBashCompletion();
    return 0;
}

int doSubCommand(const std::vector<std::string>& args) {
    if (hasHelp(args)) {
        if (args.size() >= 3 && args[1] == "help" && isKnownSubcommand(args[2], {"list", "add", "edit", "remove", "enable", "disable", "update", "validate"})) {
            printSubCommandUsageLine(args[2]);
        } else if (args.size() == 3 && isHelpToken(args[2]) && isKnownSubcommand(args[1], {"list", "add", "edit", "remove", "enable", "disable", "update", "validate"})) {
            printSubCommandUsageLine(args[1]);
        } else {
            printSubUsage();
        }
        return 0;
    }
    ensureDefaults();
    auto subs = loadSubscriptions(gPaths.subPath.string());

    if (args.size() < 2) {
        printSubUsage();
        return 1;
    }

    CLI::App parser("sub");
    parser.set_help_flag("");
    parser.allow_extras(false);
    parser.require_subcommand(1);

    bool listJson = false;
    std::string idOrName;
    std::vector<std::string> updateIds;
    std::string addId;
    std::string addName;
    std::string addUrl;
    std::string addGroup;
    std::string addFormatHint;
    std::string addUserAgent;
    std::string addTimeout;
    std::string addRetry;
    std::string addPriority;
    std::string addUpdateInterval;
    std::vector<std::string> addTags;
    std::string addTagsCsv;
    std::vector<std::string> addHeaders;
    bool addForce = false;

    std::string editName;
    std::string editUrl;
    std::string editGroup;
    std::string editFormatHint;
    std::string editUserAgent;
    std::string editTimeout;
    std::string editRetry;
    std::string editPriority;
    std::string editUpdateInterval;
    std::string editTagsCsv;
    std::vector<std::string> editHeaders;
    std::vector<std::string> editRemoveHeaders;
    bool editClearHeaders = false;
    bool editEnable = false;
    bool editDisable = false;

    std::vector<std::string> updateTags;
    bool updateStrictNetwork = false;

    auto* listCmd = parser.add_subcommand("list");
    listCmd->add_flag("--json", listJson);

    auto* addCmd = parser.add_subcommand("add");
    addCmd->add_option("--id", addId);
    addCmd->add_option("--name", addName);
    addCmd->add_option("--url", addUrl);
    addCmd->add_option("--group", addGroup);
    addCmd->add_option("--format-hint", addFormatHint);
    addCmd->add_option("--user-agent", addUserAgent);
    addCmd->add_option("--timeout", addTimeout);
    addCmd->add_option("--retry", addRetry);
    addCmd->add_option("--priority", addPriority);
    addCmd->add_option("--update-interval", addUpdateInterval);
    addCmd->add_option("--tag", addTags);
    addCmd->add_option("--tags", addTagsCsv);
    addCmd->add_option("--header", addHeaders);
    addCmd->add_flag("--force", addForce);

    auto* removeCmd = parser.add_subcommand("remove");
    removeCmd->add_option("id_or_name", idOrName);

    auto* enableCmd = parser.add_subcommand("enable");
    enableCmd->add_option("id_or_name", idOrName);
    auto* disableCmd = parser.add_subcommand("disable");
    disableCmd->add_option("id_or_name", idOrName);

    auto* editCmd = parser.add_subcommand("edit");
    editCmd->add_option("id_or_name", idOrName);
    editCmd->add_option("--name", editName);
    editCmd->add_option("--url", editUrl);
    editCmd->add_option("--group", editGroup);
    editCmd->add_option("--format-hint", editFormatHint);
    editCmd->add_option("--user-agent", editUserAgent);
    editCmd->add_option("--timeout", editTimeout);
    editCmd->add_option("--retry", editRetry);
    editCmd->add_option("--priority", editPriority);
    editCmd->add_option("--update-interval", editUpdateInterval);
    editCmd->add_option("--tags", editTagsCsv);
    editCmd->add_option("--header", editHeaders);
    editCmd->add_option("--remove-header", editRemoveHeaders);
    editCmd->add_flag("--clear-headers", editClearHeaders);
    editCmd->add_flag("--enable", editEnable);
    editCmd->add_flag("--disable", editDisable);

    auto* validateCmd = parser.add_subcommand("validate");
    validateCmd->add_option("id_or_name", idOrName)->required(false);

    auto* updateCmd = parser.add_subcommand("update");
    updateCmd->add_option("id_or_name", updateIds);
    updateCmd->add_option("--tag", updateTags);
    updateCmd->add_flag("--strict-network", updateStrictNetwork);

    if (!parseCliArgs(parser, args)) {
        printSubUsage();
        return ExitUsage;
    }

    std::string cmd;
    if (*listCmd) cmd = "list";
    else if (*addCmd) cmd = "add";
    else if (*removeCmd) cmd = "remove";
    else if (*enableCmd) cmd = "enable";
    else if (*disableCmd) cmd = "disable";
    else if (*editCmd) cmd = "edit";
    else if (*validateCmd) cmd = "validate";
    else if (*updateCmd) cmd = "update";
    if (cmd == "list") {
        const bool jsonOutput = listJson;
        if (jsonOutput) {
            printJsonLine(subscriptionsToJson(subs));
            return 0;
        }
        for (const auto& s : subs) {
            std::cout << s.id << "\t" << s.name << "\t" << (s.enabled ? "enabled" : "disabled") << "\tgroup="
                      << s.group << "\tpriority=" << s.priority << "\tupdate_interval=" << s.updateInterval << "\ttimeout="
                      << s.timeout << "\tretry=" << s.retry << "\ttags=";
            for (size_t i = 0; i < s.tags.size(); ++i) {
                if (i) {
                    std::cout << ",";
                }
                std::cout << s.tags[i];
            }
            std::cout << "\tlast_success=" << s.lastSuccess << "\tlast_error=" << s.lastError << "\n";
        }
        return 0;
    }

    if (cmd == "add") {
        const std::string name = addName;
        const std::string url = addUrl;
        const std::string maybeId = addId;
        const std::string group = addGroup.empty() ? "default" : addGroup;
        const std::string formatHint = addFormatHint.empty() ? "auto" : addFormatHint;
        const std::string userAgent = addUserAgent;
        int timeout = 15;
        if (!addTimeout.empty() && !parseBoundedIntValue(addTimeout, "--timeout", 1, timeout)) {
            return 1;
        }
        int retry = 2;
        if (!addRetry.empty() && !parseBoundedIntValue(addRetry, "--retry", 0, retry)) {
            return 1;
        }
        const bool timeoutOverride = !addTimeout.empty();
        const bool retryOverride = !addRetry.empty();
        const bool force = addForce;
        std::vector<std::string> tags = addTags;
        const auto tagsCsv = addTagsCsv;
        if (!tagsCsv.empty()) {
            auto more = splitComma(tagsCsv);
            tags.insert(tags.end(), more.begin(), more.end());
        }
        tags = normalizeTags(tags);
        if (name.empty() || url.empty()) {
            std::cerr << "sub add requires --name and --url\n";
            return 1;
        }
        Subscription s;
        s.id = maybeId.empty() ? makeIdFromName(name) : maybeId;
        s.name = name;
        if (subscriptionIdOrNameExists(subs, s.id, "")) {
            std::cerr << "subscription id already exists: " << s.id << "\n";
            return 1;
        }
        if (subscriptionIdOrNameExists(subs, "", s.name)) {
            std::cerr << "subscription name already exists: " << s.name << "\n";
            return 1;
        }
        s.url = url;
        s.group = group;
        s.formatHint = formatHint;
        s.userAgent = userAgent;
        s.timeout = timeout;
        s.timeoutOverride = timeoutOverride;
        s.retry = retry;
        s.retryOverride = retryOverride;
        s.tags = tags;
        for (const auto& rawHeader : addHeaders) {
            std::string key;
            std::string value;
            if (!parseHeaderOption(rawHeader, key, value)) {
                return 1;
            }
            s.headers[key] = value;
        }
        int priority = 100;
        if (!addPriority.empty() && !parseBoundedIntValue(addPriority, "--priority", 0, priority)) {
            return 1;
        }
        s.priority = priority;
        int updateInterval = 3600;
        if (!addUpdateInterval.empty() && !parseBoundedIntValue(addUpdateInterval, "--update-interval", 0, updateInterval)) {
            return 1;
        }
        s.updateInterval = updateInterval;
        s.cachePath = "subscriptions/" + s.id + ".cache";
        s.lastUpdated = nowIso8601();

        if (!force) {
            auto cfg = loadConfig(gPaths.configPath.string());
            applyConfigDefaults(cfg);
            auto fr = fetchSubscriptionWithRetry(withEffectiveNetworkPolicy(s, cfg), false);
            if (!fr.ok) {
                std::cerr << "validation failed: " << fr.error << " (use --force to add anyway)\n";
                return 1;
            }
            if (fr.content.empty()) {
                std::cerr << "validation failed: empty subscription content\n";
                return 1;
            }

            auto parsed = parseSubscriptionForSub(s, fr.content, cfg);
            for (const auto& warning : parsed.warnings) {
                std::cerr << "warning: " << s.id << " - " << warning.message << "\n";
            }
            std::string reason;
            if (!hasUsableParsedNodes(parsed, reason)) {
                std::cerr << "validation failed: " << reason << " (use --force to add anyway)\n";
                return 1;
            }
        }

        subs.push_back(s);
        saveSubscriptions(gPaths.subPath.string(), subs);
        std::cout << "added subscription: " << s.id << "\n";
        return 0;
    }

    if (cmd == "remove") {
        if (idOrName.empty()) {
            std::cerr << "sub remove requires exactly <id|name>\n";
            return 1;
        }
        const std::string id = idOrName;
        auto oldSize = subs.size();
        subs.erase(
            std::remove_if(subs.begin(), subs.end(), [&](const Subscription& s) { return s.id == id || s.name == id; }),
            subs.end()
        );
        if (subs.size() == oldSize) {
            std::cerr << "not found: " << id << "\n";
            return 1;
        }
        saveSubscriptions(gPaths.subPath.string(), subs);
        std::cout << "removed subscription: " << id << "\n";
        return 0;
    }

    if (cmd == "enable" || cmd == "disable") {
        if (idOrName.empty()) {
            std::cerr << "sub " << cmd << " requires exactly <id|name>\n";
            return 1;
        }
        auto* s = findSub(subs, idOrName);
        if (!s) {
            std::cerr << "not found: " << idOrName << "\n";
            return 1;
        }
        s->enabled = (cmd == "enable");
        saveSubscriptions(gPaths.subPath.string(), subs);
        std::cout << cmd << "d subscription: " << s->id << "\n";
        return 0;
    }

    if (cmd == "edit") {
        if (idOrName.empty()) {
            std::cerr << "sub edit requires <id|name>\n";
            return 1;
        }
        auto* s = findSub(subs, idOrName);
        if (!s) {
            std::cerr << "not found: " << idOrName << "\n";
            return 1;
        }
        const auto& name = editName;
        const auto& url = editUrl;
        const auto& group = editGroup;
        const auto& formatHint = editFormatHint;
        const auto& userAgent = editUserAgent;
        const auto& timeout = editTimeout;
        const auto& retry = editRetry;
        const auto& priority = editPriority;
        const auto& updateInterval = editUpdateInterval;
        const auto& tagsCsv = editTagsCsv;

        if (!name.empty()) {
            if (subscriptionIdOrNameExists(subs, "", name, s->id)) {
                std::cerr << "subscription name already exists: " << name << "\n";
                return 1;
            }
            s->name = name;
        }
        if (!url.empty()) {
            s->url = url;
        }
        if (!group.empty()) {
            s->group = group;
        }
        if (!formatHint.empty()) {
            s->formatHint = formatHint;
        }
        if (!userAgent.empty()) {
            s->userAgent = userAgent;
        }
        if (!timeout.empty()) {
            int parsed = 0;
            if (!parseBoundedIntValue(timeout, "--timeout", 1, parsed)) {
                return 1;
            }
            s->timeout = parsed;
            s->timeoutOverride = true;
        }
        if (!retry.empty()) {
            int parsed = 0;
            if (!parseBoundedIntValue(retry, "--retry", 0, parsed)) {
                return 1;
            }
            s->retry = parsed;
            s->retryOverride = true;
        }
        if (!priority.empty()) {
            int parsed = 0;
            if (!parseBoundedIntValue(priority, "--priority", 0, parsed)) {
                return 1;
            }
            s->priority = parsed;
        }
        if (!updateInterval.empty()) {
            int parsed = 0;
            if (!parseBoundedIntValue(updateInterval, "--update-interval", 0, parsed)) {
                return 1;
            }
            s->updateInterval = parsed;
        }
        if (!tagsCsv.empty()) {
            s->tags = normalizeTags(splitComma(tagsCsv));
        }
        if (editClearHeaders) {
            s->headers.clear();
        }
        for (const auto& key : editRemoveHeaders) {
            s->headers.erase(key);
        }
        for (const auto& rawHeader : editHeaders) {
            std::string key;
            std::string value;
            if (!parseHeaderOption(rawHeader, key, value)) {
                return 1;
            }
            s->headers[key] = value;
        }
        if (editEnable) {
            s->enabled = true;
        }
        if (editDisable) {
            s->enabled = false;
        }
        saveSubscriptions(gPaths.subPath.string(), subs);
        std::cout << "edited subscription: " << s->id << "\n";
        return 0;
    }

    if (cmd == "validate") {
        auto cfg = loadConfig(gPaths.configPath.string());
        applyConfigDefaults(cfg);
        std::string only = idOrName;
        int okCount = 0;
        int failCount = 0;
        int skippedNodes = 0;
        int matchedCount = 0;
        for (auto& s : subs) {
            if (!only.empty() && s.id != only && s.name != only) {
                continue;
            }
            ++matchedCount;
            auto fr = fetchSubscriptionWithRetry(withEffectiveNetworkPolicy(s, cfg), false);
            if (!fr.ok || fr.content.empty()) {
                ++failCount;
                std::cerr << "invalid: " << s.id << " - " << (fr.error.empty() ? "empty content" : fr.error) << "\n";
                continue;
            }
            auto parsed = parseSubscriptionForSub(s, fr.content, cfg);
            skippedNodes += parsed.skipped;
            for (const auto& warning : parsed.warnings) {
                std::cerr << "warning: " << s.id << " - " << warning.message << "\n";
            }
            std::string reason;
            if (!hasUsableParsedNodes(parsed, reason)) {
                ++failCount;
                std::cerr << "invalid: " << s.id << " - " << reason << "\n";
                continue;
            }
            ++okCount;
            std::cout << "valid: " << s.id << " nodes=" << parsed.nodes.size() << " skipped=" << parsed.skipped << "\n";
        }
        if (!only.empty() && matchedCount == 0) {
            std::cerr << "not found: " << only << "\n";
            return 1;
        }
        std::cout << "validate summary: success=" << okCount << " failed=" << failCount << " skipped_nodes=" << skippedNodes << "\n";
        return failCount > 0 ? 1 : 0;
    }

    if (cmd == "update") {
        auto cfg = loadConfig(gPaths.configPath.string());
        applyConfigDefaults(cfg);
        const bool strictNetwork = updateStrictNetwork;
        std::set<std::string> ids;
        for (const auto& id : updateIds) {
            ids.insert(id);
        }
        auto tagsFilter = updateTags;

        std::vector<size_t> selected;
        selected.reserve(subs.size());
        for (size_t i = 0; i < subs.size(); ++i) {
            const auto& s = subs[i];
            if (!s.enabled) {
                continue;
            }
            if (!ids.empty() && !ids.count(s.id) && !ids.count(s.name)) {
                continue;
            }
            if (!tagsFilter.empty()) {
                bool tagMatched = false;
                for (const auto& t : tagsFilter) {
                    if (std::find(s.tags.begin(), s.tags.end(), t) != s.tags.end()) {
                        tagMatched = true;
                        break;
                    }
                }
                if (!tagMatched) {
                    continue;
                }
            }
            selected.push_back(i);
        }

        if (selected.empty()) {
            std::cerr << "no subscriptions selected for update (check enabled state and filters)\n";
            return 1;
        }

        std::stable_sort(selected.begin(), selected.end(), [&](size_t a, size_t b) {
            if (subs[a].priority != subs[b].priority) {
                return subs[a].priority < subs[b].priority;
            }
            return subs[a].id < subs[b].id;
        });

        const std::time_t now = std::time(nullptr);
        std::vector<size_t> due;
        due.reserve(selected.size());
        int skippedByInterval = 0;
        for (auto idx : selected) {
            if (shouldSkipByUpdateInterval(subs[idx], now)) {
                ++skippedByInterval;
                std::cout << "skipped(interval): " << subs[idx].id << "\n";
                continue;
            }
            due.push_back(idx);
        }

        if (due.empty()) {
            std::cout << "update summary: success=0 failed=0 parsed_nodes=0 skipped_nodes=0 skipped_subscriptions=" << skippedByInterval
                      << "\n";
            return 0;
        }

        std::vector<FetchResult> fetchResults(subs.size());
        runParallelJobs(due.size(), cfg.parallelism, [&](size_t jobIdx) {
            const size_t idx = due[jobIdx];
            const auto effective = withEffectiveNetworkPolicy(subs[idx], cfg);
            fetchResults[idx] = fetchSubscriptionWithRetry(effective, !strictNetwork);
        });

        int okCount = 0;
        int failCount = 0;
        int parsedNodeCount = 0;
        int skippedNodeCount = 0;
        for (auto idx : due) {
            auto& s = subs[idx];
            const auto& fr = fetchResults[idx];
            s.lastUpdated = nowIso8601();
            if (!fr.ok) {
                s.lastError = fr.error;
                ++failCount;
                std::cerr << "update failed for " << s.id << ": " << fr.error << "\n";
                continue;
            }
            if (fr.usedCache) {
                std::cerr << "warning: " << s.id << " used cached subscription content (" << fr.cacheReason << ")\n";
            }

            if (!fr.notModified) {
                std::string err;
                if (!writeFile(resolveCachePath(s.cachePath), fr.content, err)) {
                    s.lastError = err;
                    ++failCount;
                    std::cerr << "cache write failed for " << s.id << ": " << err << "\n";
                    continue;
                }
            }

            auto parsed = parseSubscriptionForSub(s, fr.content, cfg);
            parsedNodeCount += static_cast<int>(parsed.nodes.size());
            skippedNodeCount += parsed.skipped;
            for (const auto& warning : parsed.warnings) {
                std::cerr << "warning: " << s.id << " - " << warning.message << "\n";
            }

            std::string reason;
            if (!hasUsableParsedNodes(parsed, reason)) {
                s.lastError = reason;
                ++failCount;
                std::cerr << "update failed for " << s.id << ": " << reason << "\n";
                continue;
            }

            if (!fr.etag.empty()) {
                s.etag = fr.etag;
            }
            if (!fr.lastModified.empty()) {
                s.lastModified = fr.lastModified;
            }
            s.lastSuccess = nowIso8601();
            s.lastError.clear();
            ++okCount;
            std::cout << (fr.notModified ? "not-modified: " : "updated: ") << s.id << " nodes=" << parsed.nodes.size()
                      << " skipped=" << parsed.skipped << "\n";
        }
        saveSubscriptions(gPaths.subPath.string(), subs);
        std::cout << "update summary: success=" << okCount << " failed=" << failCount << " parsed_nodes=" << parsedNodeCount
                  << " skipped_nodes=" << skippedNodeCount << " skipped_subscriptions=" << skippedByInterval << "\n";
        return failCount > 0 ? 1 : 0;
    }

    std::cerr << "unknown sub command: " << cmd << "\n";
    std::cerr << "Run 'subcli sub --help' to see available subscription commands.\n";
    return 1;
}

int doConfigCommand(const std::vector<std::string>& args) {
    if (hasHelp(args)) {
        if (args.size() >= 3 && args[1] == "help" && isKnownSubcommand(args[2], {"list", "get", "set", "remove"})) {
            printConfigSubcommandUsage(args[2]);
        } else if (args.size() == 3 && isHelpToken(args[2]) && isKnownSubcommand(args[1], {"list", "get", "set", "remove"})) {
            printConfigSubcommandUsage(args[1]);
        } else {
            printConfigUsage();
        }
        return 0;
    }
    ensureDefaults();
    AppConfig cfg = loadConfig(gPaths.configPath.string());
    applyConfigDefaults(cfg);

    CLI::App parser("config");
    parser.set_help_flag("");
    parser.allow_extras(false);
    parser.require_subcommand(1);

    bool jsonOutput = false;
    std::string parsedKey;
    std::string parsedValue;

    auto* listCmd = parser.add_subcommand("list");
    listCmd->add_flag("--json", jsonOutput);
    auto* getCmd = parser.add_subcommand("get");
    getCmd->add_option("key", parsedKey);
    auto* setCmd = parser.add_subcommand("set");
    setCmd->add_option("key", parsedKey);
    setCmd->add_option("value", parsedValue);
    auto* removeCmd = parser.add_subcommand("remove");
    removeCmd->add_option("key", parsedKey);

    if (!parseCliArgs(parser, args)) {
        printConfigUsage();
        return 1;
    }

    std::string cmd;
    if (*listCmd) {
        cmd = "list";
    } else if (*getCmd) {
        cmd = "get";
    } else if (*setCmd) {
        cmd = "set";
    } else if (*removeCmd) {
        cmd = "remove";
    }

    if (cmd == "list") {
        if (jsonOutput) {
            printJsonLine(configToJson(cfg));
            return 0;
        }
        std::cout << "tun=" << (cfg.tun ? "true" : "false") << "\n";
        std::cout << "profile=" << cfg.profile << "\n";
        std::cout << "profile_path=" << cfg.profilePath << "\n";
        std::cout << "output_dir=" << cfg.outputDir << "\n";
        std::cout << "template_dir=" << cfg.templateDir << "\n";
        std::cout << "asset_dir=" << cfg.assetDir << "\n";
        std::cout << "parallelism=" << cfg.parallelism << "\n";
        std::cout << "timeout=" << cfg.timeout << "\n";
        std::cout << "retry=" << cfg.retry << "\n";
        std::cout << "fetch_max_bytes=" << cfg.fetchMaxBytes << "\n";
        std::cout << "log_level=" << cfg.logLevel << "\n";
        std::cout << "core_paths.mihomo=" << cfg.mihomoPath << "\n";
        std::cout << "core_paths.sing_box=" << cfg.singBoxPath << "\n";
        std::cout << "core_paths.xray=" << cfg.xrayPath << "\n";
        std::cout << "node_management.dedupe=" << (cfg.dedupeNodes ? "true" : "false") << "\n";
        std::cout << "node_management.rename_template=" << cfg.renameTemplate << "\n";
        std::cout << "node_management.include_regex=" << cfg.includeRegex << "\n";
        std::cout << "node_management.exclude_regex=" << cfg.excludeRegex << "\n";
        std::cout << "node_management.sort_by=" << cfg.sortBy << "\n";
        for (const auto& kv : cfg.templateNormal) {
            std::cout << "templates." << kv.first << ".normal=" << kv.second << "\n";
        }
        for (const auto& kv : cfg.templateTun) {
            std::cout << "templates." << kv.first << ".tun=" << kv.second << "\n";
        }
        for (const auto& kv : cfg.regionRules) {
            std::cout << "grouping.region_rules." << kv.first << "=" << kv.second << "\n";
        }
        for (const auto& kv : cfg.assetPaths) {
            std::cout << "assets.paths." << kv.first << "=" << kv.second << "\n";
        }
        for (const auto& kv : cfg.assetUrls) {
            std::cout << "assets.urls." << kv.first << "=" << kv.second << "\n";
        }
        return 0;
    }
    if (cmd == "get") {
        if (parsedKey.empty()) {
            std::cerr << "config get requires <key>\n";
            std::cerr << "Run 'subcli config --help' to view supported keys and examples.\n";
            return 1;
        }
        const auto& key = parsedKey;
        if (key == "tun") {
            std::cout << (cfg.tun ? "true" : "false") << "\n";
            return 0;
        }
        if (key == "output_dir") {
            std::cout << cfg.outputDir << "\n";
            return 0;
        }
        if (key == "profile") {
            std::cout << cfg.profile << "\n";
            return 0;
        }
        if (key == "profile_path") {
            std::cout << cfg.profilePath << "\n";
            return 0;
        }
        if (key == "asset_dir") {
            std::cout << cfg.assetDir << "\n";
            return 0;
        }
        if (key == "template_dir") {
            std::cout << cfg.templateDir << "\n";
            return 0;
        }
        if (key == "parallelism") {
            std::cout << cfg.parallelism << "\n";
            return 0;
        }
        if (key == "timeout") {
            std::cout << cfg.timeout << "\n";
            return 0;
        }
        if (key == "retry") {
            std::cout << cfg.retry << "\n";
            return 0;
        }
        if (key == "fetch_max_bytes") {
            std::cout << cfg.fetchMaxBytes << "\n";
            return 0;
        }
        if (key == "log_level") {
            std::cout << cfg.logLevel << "\n";
            return 0;
        }
        if (key == "core_paths.mihomo") {
            std::cout << cfg.mihomoPath << "\n";
            return 0;
        }
        if (key == "core_paths.sing_box") {
            std::cout << cfg.singBoxPath << "\n";
            return 0;
        }
        if (key == "core_paths.xray") {
            std::cout << cfg.xrayPath << "\n";
            return 0;
        }
        if (key == "node_management.dedupe") {
            std::cout << (cfg.dedupeNodes ? "true" : "false") << "\n";
            return 0;
        }
        if (key == "node_management.rename_template") {
            std::cout << cfg.renameTemplate << "\n";
            return 0;
        }
        if (key == "node_management.include_regex") {
            std::cout << cfg.includeRegex << "\n";
            return 0;
        }
        if (key == "node_management.exclude_regex") {
            std::cout << cfg.excludeRegex << "\n";
            return 0;
        }
        if (key == "node_management.sort_by") {
            std::cout << cfg.sortBy << "\n";
            return 0;
        }
        const std::string tplPrefix = "templates.";
        if (key.rfind(tplPrefix, 0) == 0) {
            const auto rest = key.substr(tplPrefix.size());
            auto dot = rest.find('.');
            if (dot != std::string::npos) {
                const auto core = rest.substr(0, dot);
                const auto kind = rest.substr(dot + 1);
                if (kind == "normal" && cfg.templateNormal.count(core)) {
                    std::cout << cfg.templateNormal[core] << "\n";
                    return 0;
                }
                if (kind == "tun" && cfg.templateTun.count(core)) {
                    std::cout << cfg.templateTun[core] << "\n";
                    return 0;
                }
            }
        }
        const std::string rrPrefix = "grouping.region_rules.";
        if (key.rfind(rrPrefix, 0) == 0) {
            const auto region = key.substr(rrPrefix.size());
            if (cfg.regionRules.count(region)) {
                std::cout << cfg.regionRules[region] << "\n";
                return 0;
            }
        }
        const std::string assetPathPrefix = "assets.paths.";
        if (key.rfind(assetPathPrefix, 0) == 0) {
            const auto assetKey = key.substr(assetPathPrefix.size());
            if (cfg.assetPaths.count(assetKey)) {
                std::cout << cfg.assetPaths[assetKey] << "\n";
                return 0;
            }
        }
        const std::string assetUrlPrefix = "assets.urls.";
        if (key.rfind(assetUrlPrefix, 0) == 0) {
            const auto assetKey = key.substr(assetUrlPrefix.size());
            if (cfg.assetUrls.count(assetKey)) {
                std::cout << cfg.assetUrls[assetKey] << "\n";
                return 0;
            }
        }
        std::cerr << "unsupported key in v1\n";
        return 1;
    }
    if (cmd == "set") {
        if (parsedKey.empty() || parsedValue.empty()) {
            std::cerr << "config set requires <key> <value>\n";
            std::cerr << "Run 'subcli config --help' to view supported keys and examples.\n";
            return 1;
        }
        const auto& key = parsedKey;
        const auto& value = parsedValue;
        if (key == "tun") {
            if (!parseBoolValue(value, "tun", cfg.tun)) {
                return 1;
            }
        } else if (key == "output_dir") {
            cfg.outputDir = resolveFromConfigDir(value);
        } else if (key == "profile") {
            if (!isSupportedProfile(value)) {
                std::cerr << "unsupported profile: " << value << "\n";
                return 1;
            }
            cfg.profile = value;
        } else if (key == "profile_path") {
            cfg.profilePath = resolveFromConfigDir(value);
        } else if (key == "asset_dir") {
            cfg.assetDir = resolveFromConfigDir(value);
        } else if (key == "template_dir") {
            const std::string oldDir = cfg.templateDir;
            cfg.templateDir = resolveFromConfigDir(value);
            updateTemplateDirDefaults(cfg, oldDir);
        } else if (key == "parallelism") {
            int parsed = 0;
            if (!parseBoundedIntValue(value, "parallelism", 1, parsed)) {
                return 1;
            }
            cfg.parallelism = parsed;
        } else if (key == "timeout") {
            int parsed = 0;
            if (!parseBoundedIntValue(value, "timeout", 1, parsed)) {
                return 1;
            }
            cfg.timeout = parsed;
        } else if (key == "retry") {
            int parsed = 0;
            if (!parseBoundedIntValue(value, "retry", 0, parsed)) {
                return 1;
            }
            cfg.retry = parsed;
        } else if (key == "fetch_max_bytes") {
            int parsed = 0;
            if (!parseBoundedIntValue(value, "fetch_max_bytes", 1024, parsed)) {
                return 1;
            }
            cfg.fetchMaxBytes = parsed;
        } else if (key == "log_level") {
            cfg.logLevel = value;
        } else if (key == "core_paths.mihomo") {
            cfg.mihomoPath = resolveFromConfigDir(value);
        } else if (key == "core_paths.sing_box") {
            cfg.singBoxPath = resolveFromConfigDir(value);
        } else if (key == "core_paths.xray") {
            cfg.xrayPath = resolveFromConfigDir(value);
        } else if (key == "node_management.dedupe") {
            if (!parseBoolValue(value, "node_management.dedupe", cfg.dedupeNodes)) {
                return 1;
            }
        } else if (key == "node_management.rename_template") {
            cfg.renameTemplate = value;
        } else if (key == "node_management.include_regex") {
            cfg.includeRegex = value;
        } else if (key == "node_management.exclude_regex") {
            cfg.excludeRegex = value;
        } else if (key == "node_management.sort_by") {
            cfg.sortBy = value;
        } else if (key.rfind("templates.", 0) == 0) {
            const auto rest = key.substr(std::string("templates.").size());
            auto dot = rest.find('.');
            if (dot == std::string::npos) {
                std::cerr << "invalid template key\n";
                return 1;
            }
            const auto core = rest.substr(0, dot);
            const auto kind = rest.substr(dot + 1);
            std::string error;
            if (!parseTemplateTargetKind(core, kind, error)) {
                std::cerr << error << "\n";
                return 1;
            }
            if (kind == "normal") {
                cfg.templateNormal[core] = value;
            } else if (kind == "tun") {
                cfg.templateTun[core] = value;
            } else {
                std::cerr << "invalid template key\n";
                return 1;
            }
        } else if (key.rfind("grouping.region_rules.", 0) == 0) {
            const auto region = key.substr(std::string("grouping.region_rules.").size());
            if (region.empty()) {
                std::cerr << "invalid region rule key\n";
                return 1;
            }
            cfg.regionRules[region] = value;
        } else if (key.rfind("assets.paths.", 0) == 0) {
            const auto assetKey = key.substr(std::string("assets.paths.").size());
            if (assetKey.empty()) {
                std::cerr << "invalid asset path key\n";
                return 1;
            }
            cfg.assetPaths[assetKey] = value;
        } else if (key.rfind("assets.urls.", 0) == 0) {
            const auto assetKey = key.substr(std::string("assets.urls.").size());
            if (assetKey.empty()) {
                std::cerr << "invalid asset URL key\n";
                return 1;
            }
            cfg.assetUrls[assetKey] = value;
        } else {
            std::cerr << "unsupported key in v1\n";
            return 1;
        }
        saveConfig(gPaths.configPath.string(), cfg);
        std::cout << "updated config: " << key << "\n";
        return 0;
    }
    if (cmd == "remove") {
        if (parsedKey.empty()) {
            std::cerr << "config remove requires <key>\n";
            std::cerr << "Run 'subcli config --help' to view supported keys and examples.\n";
            return 1;
        }
        const auto& key = parsedKey;
        if (key == "output_dir") {
            cfg.outputDir = gPaths.outputDir.string();
        } else if (key == "profile") {
            cfg.profile = "bypass-cn";
        } else if (key == "profile_path") {
            cfg.profilePath.clear();
        } else if (key == "asset_dir") {
            cfg.assetDir = (gPaths.dataDir / "assets").string();
        } else if (key == "template_dir") {
            const std::string oldDir = cfg.templateDir;
            cfg.templateDir = gPaths.templateDir.string();
            updateTemplateDirDefaults(cfg, oldDir);
        } else if (key == "tun") {
            cfg.tun = false;
        } else if (key == "parallelism") {
            cfg.parallelism = 4;
        } else if (key == "timeout") {
            cfg.timeout = 15;
        } else if (key == "retry") {
            cfg.retry = 2;
        } else if (key == "fetch_max_bytes") {
            cfg.fetchMaxBytes = 10 * 1024 * 1024;
        } else if (key == "log_level") {
            cfg.logLevel = "info";
        } else if (key == "core_paths.mihomo") {
            cfg.mihomoPath.clear();
        } else if (key == "core_paths.sing_box") {
            cfg.singBoxPath.clear();
        } else if (key == "core_paths.xray") {
            cfg.xrayPath.clear();
        } else if (key == "node_management.dedupe") {
            cfg.dedupeNodes = true;
        } else if (key == "node_management.rename_template") {
            cfg.renameTemplate = "{name}";
        } else if (key == "node_management.include_regex") {
            cfg.includeRegex.clear();
        } else if (key == "node_management.exclude_regex") {
            cfg.excludeRegex.clear();
        } else if (key == "node_management.sort_by") {
            cfg.sortBy = "region,name";
        } else if (key.rfind("templates.", 0) == 0) {
            const auto rest = key.substr(std::string("templates.").size());
            auto dot = rest.find('.');
            if (dot == std::string::npos) {
                std::cerr << "invalid template key\n";
                return 1;
            }
            const auto core = rest.substr(0, dot);
            const auto kind = rest.substr(dot + 1);
            std::string error;
            if (!parseTemplateTargetKind(core, kind, error)) {
                std::cerr << error << "\n";
                return 1;
            }
            if (kind == "normal") {
                cfg.templateNormal.erase(core);
            } else if (kind == "tun") {
                cfg.templateTun.erase(core);
            } else {
                std::cerr << "invalid template key\n";
                return 1;
            }
        } else if (key.rfind("grouping.region_rules.", 0) == 0) {
            const auto region = key.substr(std::string("grouping.region_rules.").size());
            cfg.regionRules.erase(region);
        } else if (key.rfind("assets.paths.", 0) == 0) {
            cfg.assetPaths.erase(key.substr(std::string("assets.paths.").size()));
        } else if (key.rfind("assets.urls.", 0) == 0) {
            cfg.assetUrls.erase(key.substr(std::string("assets.urls.").size()));
        } else {
            std::cerr << "unsupported key in v1\n";
            return 1;
        }
        applyConfigDefaults(cfg);
        saveConfig(gPaths.configPath.string(), cfg);
        std::cout << "removed config key: " << key << "\n";
        return 0;
    }
    std::cerr << "unknown config command: " << cmd << "\n";
    std::cerr << "Run 'subcli config --help' to see available config commands.\n";
    return 1;
}

int doTemplateCommand(const std::vector<std::string>& args) {
    if (hasHelp(args)) {
        if (args.size() >= 3 && args[1] == "help" && isKnownSubcommand(args[2], {"list", "get", "set", "reset", "validate"})) {
            printTemplateSubcommandUsage(args[2]);
        } else if (args.size() == 3 && isHelpToken(args[2]) && isKnownSubcommand(args[1], {"list", "get", "set", "reset", "validate"})) {
            printTemplateSubcommandUsage(args[1]);
        } else {
            printTemplateUsage();
        }
        return 0;
    }
    ensureDefaults();
    AppConfig cfg = loadConfig(gPaths.configPath.string());
    applyConfigDefaults(cfg);

    CLI::App parser("template");
    parser.set_help_flag("");
    parser.allow_extras(false);
    parser.require_subcommand(1);

    bool jsonOutput = false;
    std::string parsedTarget;
    std::string parsedKind;
    std::string parsedPath;

    auto* listCmd = parser.add_subcommand("list");
    listCmd->add_flag("--json", jsonOutput);
    auto* getCmd = parser.add_subcommand("get");
    getCmd->add_option("target", parsedTarget);
    getCmd->add_option("kind", parsedKind);
    auto* setCmd = parser.add_subcommand("set");
    setCmd->add_option("target", parsedTarget);
    setCmd->add_option("kind", parsedKind);
    setCmd->add_option("path", parsedPath);
    auto* resetCmd = parser.add_subcommand("reset");
    resetCmd->add_option("target", parsedTarget)->required(false);
    resetCmd->add_option("kind", parsedKind)->required(false);
    auto* validateCmd = parser.add_subcommand("validate");
    validateCmd->add_flag("--json", jsonOutput);

    if (!parseCliArgs(parser, args)) {
        printTemplateUsage();
        return 1;
    }

    std::string cmd;
    if (*listCmd) {
        cmd = "list";
    } else if (*getCmd) {
        cmd = "get";
    } else if (*setCmd) {
        cmd = "set";
    } else if (*resetCmd) {
        cmd = "reset";
    } else if (*validateCmd) {
        cmd = "validate";
    }

    if (cmd == "list") {
        nlohmann::json templates = nlohmann::json::array();
        for (const auto& item : templateTargetKinds()) {
            const auto path = getTemplateValue(cfg, item.first, item.second);
            std::error_code ec;
            const bool exists = std::filesystem::is_regular_file(path, ec) && !ec;
            if (jsonOutput) {
                templates.push_back({{"target", item.first}, {"kind", item.second}, {"path", path}, {"exists", exists}});
                continue;
            }
            std::cout << item.first << "." << item.second << "=" << path << "\t" << (exists ? "ok" : "missing") << "\n";
        }
        if (jsonOutput) {
            printJsonLine({{"templates", templates}});
        }
        return 0;
    }

    if (cmd == "get") {
        if (parsedTarget.empty() || parsedKind.empty()) {
            std::cerr << "template get requires <target> <normal|tun>\n";
            std::cerr << "Run 'subcli template --help' for valid template usage.\n";
            return 1;
        }
        std::string error;
        if (!parseTemplateTargetKind(parsedTarget, parsedKind, error)) {
            std::cerr << error << "\n";
            return 1;
        }
        std::cout << getTemplateValue(cfg, parsedTarget, parsedKind) << "\n";
        return 0;
    }

    if (cmd == "set") {
        if (parsedTarget.empty() || parsedKind.empty() || parsedPath.empty()) {
            std::cerr << "template set requires <target> <normal|tun> <path>\n";
            std::cerr << "Run 'subcli template --help' for valid template usage.\n";
            return 1;
        }
        std::string error;
        if (!parseTemplateTargetKind(parsedTarget, parsedKind, error)) {
            std::cerr << error << "\n";
            return 1;
        }
        const std::string path = resolveFromCliCwd(parsedPath);
        std::error_code ec;
        if (!std::filesystem::is_regular_file(path, ec) || ec) {
            std::cerr << "template file does not exist: " << path << "\n";
            return 1;
        }
        setTemplateValue(cfg, parsedTarget, parsedKind, path);
        saveConfig(gPaths.configPath.string(), cfg);
        std::cout << "updated template: " << parsedTarget << "." << parsedKind << "\n";
        return 0;
    }

    if (cmd == "reset") {
        if (!parsedKind.empty() && parsedTarget.empty()) {
            std::cerr << "template reset accepts [target] [normal|tun]\n";
            return 1;
        }
        if (parsedTarget.empty()) {
            for (const auto& item : templateTargetKinds()) {
                resetTemplateValue(cfg, item.first, item.second);
            }
        } else if (parsedKind.empty()) {
            if (!isTemplateTarget(parsedTarget)) {
                std::cerr << "invalid template target: " << parsedTarget << "\n";
                return 1;
            }
            resetTemplateValue(cfg, parsedTarget, "normal");
            resetTemplateValue(cfg, parsedTarget, "tun");
        } else {
            std::string error;
            if (!parseTemplateTargetKind(parsedTarget, parsedKind, error)) {
                std::cerr << error << "\n";
                return 1;
            }
            resetTemplateValue(cfg, parsedTarget, parsedKind);
        }
        saveConfig(gPaths.configPath.string(), cfg);
        std::cout << "reset template configuration\n";
        return 0;
    }

    if (cmd == "validate") {
        int failed = 0;
        nlohmann::json templates = nlohmann::json::array();
        for (const auto& item : templateTargetKinds()) {
            const auto path = getTemplateValue(cfg, item.first, item.second);
            std::error_code ec;
            const bool exists = std::filesystem::is_regular_file(path, ec) && !ec;
            bool parseOk = false;
            std::string parseError;
            if (exists) {
                try {
                    if (item.first == "mihomo") {
                        YAML::Node root = YAML::LoadFile(path);
                        if (!root || !root.IsMap()) {
                            parseError = "template root must be YAML map";
                        } else {
                            parseOk = true;
                        }
                    } else {
                        auto root = nlohmann::json::parse(readFile(path), nullptr, false);
                        if (root.is_discarded()) {
                            parseError = "template JSON parse failed";
                        } else if (!root.is_object()) {
                            parseError = "template root must be JSON object";
                        } else {
                            parseOk = true;
                        }
                    }
                } catch (const std::exception& ex) {
                    parseError = ex.what();
                }
            }

            templates.push_back(
                {{"target", item.first}, {"kind", item.second}, {"path", path}, {"exists", exists}, {"parse_ok", parseOk}, {"error", parseError}}
            );

            if (!exists || !parseOk) {
                ++failed;
                if (!jsonOutput) {
                    std::cout << "[FAIL] " << item.first << "." << item.second << "=" << path;
                    if (!exists) {
                        std::cout << " (missing file)";
                    } else if (!parseError.empty()) {
                        std::cout << " (" << parseError << ")";
                    }
                    std::cout << "\n";
                }
            } else {
                if (!jsonOutput) {
                    std::cout << "[ OK ] " << item.first << "." << item.second << "=" << path << "\n";
                }
            }
        }
        if (jsonOutput) {
            printJsonLine({{"failed", failed}, {"templates", templates}});
        }
        return failed == 0 ? 0 : 1;
    }

    std::cerr << "unknown template command: " << cmd << "\n";
    std::cerr << "Run 'subcli template --help' to see available template commands.\n";
    return 1;
}

int doAssetCommand(const std::vector<std::string>& args) {
    if (args.size() < 2 || hasHelp(args)) {
        if (args.size() >= 3 && args[1] == "help" && isKnownSubcommand(args[2], {"list", "status", "validate", "update"})) {
            printAssetSubcommandUsage(args[2]);
        } else if (args.size() == 3 && isHelpToken(args[2]) && isKnownSubcommand(args[1], {"list", "status", "validate", "update"})) {
            printAssetSubcommandUsage(args[1]);
        } else {
            printAssetUsage();
        }
        return 0;
    }
    ensureDefaults();
    AppConfig cfg = loadConfig(gPaths.configPath.string());
    applyConfigDefaults(cfg);

    CLI::App parser("asset");
    parser.set_help_flag("");
    parser.allow_extras(false);
    parser.require_subcommand(1);
    std::string assetKey;
    auto* listCmd = parser.add_subcommand("list");
    auto* statusCmd = parser.add_subcommand("status");
    auto* validateCmd = parser.add_subcommand("validate");
    auto* updateCmd = parser.add_subcommand("update");
    updateCmd->add_option("asset_key", assetKey)->required(false);

    if (!parseCliArgs(parser, args)) {
        printAssetUsage();
        return ExitUsage;
    }

    const auto records = configuredAssets(cfg);
    std::string cmd;
    if (*listCmd) {
        cmd = "list";
    } else if (*statusCmd) {
        cmd = "status";
    } else if (*validateCmd) {
        cmd = "validate";
    } else if (*updateCmd) {
        cmd = "update";
    }

    if (cmd == "list") {
        for (const auto& asset : records) {
            std::cout << asset.key << " path=" << asset.path << " url=" << asset.url
                      << " status=" << (asset.exists ? "present" : "missing") << "\n";
        }
        return ExitOk;
    }

    if (cmd == "status") {
        for (const auto& asset : records) {
            std::cout << asset.key << " status=" << (asset.exists ? "present" : "missing")
                      << " size=" << (asset.sizeBytes >= 0 ? std::to_string(asset.sizeBytes) : "-")
                      << " updated=" << (asset.updatedAt.empty() ? "-" : asset.updatedAt)
                      << " source=" << (asset.sourceUrl.empty() ? "-" : asset.sourceUrl)
                      << " path=" << asset.path << "\n";
        }
        return ExitOk;
    }

    if (cmd == "validate") {
        int missing = 0;
        for (const auto& asset : records) {
            if (!asset.exists) {
                ++missing;
                std::cerr << "missing asset: " << asset.key << " at " << asset.path << "\n";
            }
        }
        if (missing > 0) {
            return ExitError;
        }
        std::cout << "all assets present\n";
        return ExitOk;
    }

    if (cmd == "update") {
        std::vector<AssetRecord> selected;
        if (!assetKey.empty()) {
            for (const auto& asset : records) {
                if (asset.key == assetKey) {
                    selected.push_back(asset);
                }
            }
            if (selected.empty()) {
                std::cerr << "asset key not found: " << assetKey << "\n";
                return ExitUsage;
            }
        } else {
            selected = records;
        }

        int failed = 0;
        for (const auto& asset : selected) {
            std::string error;
            if (!updateAsset(asset, cfg.timeout, cfg.fetchMaxBytes, error)) {
                ++failed;
                std::cerr << "asset update failed: " << asset.key << ": " << error << "\n";
                continue;
            }
            std::cout << "updated asset: " << asset.key << " -> " << asset.path << "\n";
        }
        return failed == 0 ? ExitOk : ExitError;
    }

    std::cerr << "unknown asset command: " << cmd << "\n";
    std::cerr << "Run 'subcli asset --help' to see available asset commands.\n";
    return ExitUsage;
}

int doProfileCommand(const std::vector<std::string>& args) {
    if (args.size() < 2 || hasHelp(args)) {
        if (args.size() >= 3 && args[1] == "help" && isKnownSubcommand(args[2], {"list", "get", "validate", "explain"})) {
            printProfileSubcommandUsage(args[2]);
        } else if (args.size() == 3 && isHelpToken(args[2]) && isKnownSubcommand(args[1], {"list", "get", "validate", "explain"})) {
            printProfileSubcommandUsage(args[1]);
        } else {
            printProfileUsage();
        }
        return ExitOk;
    }
    ensureDefaults();
    AppConfig cfg = loadConfig(gPaths.configPath.string());
    applyConfigDefaults(cfg);

    CLI::App parser("profile");
    parser.set_help_flag("");
    parser.allow_extras(false);
    parser.require_subcommand(1);
    std::string parsedName;
    std::string targetOpt;
    bool jsonOutput = false;
    auto* listCmd = parser.add_subcommand("list");
    auto* getCmd = parser.add_subcommand("get");
    getCmd->add_option("name", parsedName);
    auto* validateCmd = parser.add_subcommand("validate");
    validateCmd->add_option("path", parsedName);
    auto* explainCmd = parser.add_subcommand("explain");
    explainCmd->add_option("path_or_name", parsedName);
    explainCmd->add_option("--target", targetOpt);
    explainCmd->add_flag("--json", jsonOutput);

    if (!parseCliArgs(parser, args)) {
        printProfileUsage();
        return ExitUsage;
    }

    std::string cmd;
    if (*listCmd) cmd = "list";
    else if (*getCmd) cmd = "get";
    else if (*validateCmd) cmd = "validate";
    else if (*explainCmd) cmd = "explain";

    const std::vector<std::string> builtIns = {"bypass-cn", "global", "direct"};

    if (cmd == "list") {
        std::cout << "built-in profiles:\n";
        for (const auto& name : builtIns) {
            std::cout << "  " << name << "\n";
        }
        if (!cfg.profile.empty()) {
            std::cout << "configured profile: " << cfg.profile << "\n";
        }
        if (!cfg.profilePath.empty()) {
            std::cout << "configured profile_path: " << cfg.profilePath << "\n";
        }
        return ExitOk;
    }

    if (cmd == "get") {
        if (parsedName.empty()) {
            printProfileUsage();
            return ExitUsage;
        }
        if (!isBuiltInProfileNameForCli(parsedName)) {
            std::cerr << "unknown built-in profile: " << parsedName << "\n";
            return ExitError;
        }
        std::string path;
        AppConfig builtInConfig;
        resolveExportProfilePath(builtInConfig, gPaths.profileDir.string(), parsedName, path);
        std::ifstream in(path);
        if (!in) {
            std::cerr << "failed to open profile: " << path << "\n";
            return ExitError;
        }
        std::cout << in.rdbuf();
        return ExitOk;
    }

    if (cmd == "validate") {
        if (parsedName.empty()) {
            printProfileUsage();
            return ExitUsage;
        }
        ResolvedProfile profile;
        std::string error;
        const std::string path = resolveProfileFileArg(parsedName);
        if (!loadProfile(path, profile, error)) {
            std::cerr << "profile validation failed: " << error << "\n";
            return ExitError;
        }
        for (const auto& targetEntry : profile.templatePolicy.targets) {
            ExportTarget parsedTarget;
            if (!parseTemplatePolicyTarget(targetEntry.first, parsedTarget)) {
                std::cerr << "profile validation failed: unsupported template_policy target: " << targetEntry.first << "\n";
                return ExitError;
            }
            for (const auto& pathEntry : targetEntry.second.pathActions) {
                TemplatePolicyAction parsedAction;
                if (!parseTemplatePolicyAction(pathEntry.second, parsedAction)) {
                    std::cerr << "profile validation failed: unsupported template_policy action: " << pathEntry.second << "\n";
                    return ExitError;
                }
                if (!isTemplatePolicyPathSupported(parsedTarget, pathEntry.first)) {
                    std::cerr << "profile validation failed: unsupported template_policy path for "
                              << targetEntry.first << ": " << pathEntry.first << "\n";
                    return ExitError;
                }
                if (!isTemplatePolicyActionSupportedForPath(parsedTarget, pathEntry.first, parsedAction)) {
                    std::cerr << "profile validation failed: unsupported template_policy action for "
                              << targetEntry.first << " path " << pathEntry.first << ": " << pathEntry.second << "\n";
                    return ExitError;
                }
            }
        }
        std::cout << "profile valid: " << profile.name << "\n";
        return ExitOk;
    }

    if (cmd == "explain") {
        if (parsedName.empty()) return ExitUsage;

        std::string explainPath;
        if (isBuiltInProfileNameForCli(parsedName)) {
            AppConfig builtInConfig;
            if (!resolveExportProfilePath(builtInConfig, gPaths.profileDir.string(), parsedName, explainPath)) {
                std::cerr << "profile explain failed: unable to resolve built-in profile: " << parsedName << "\n";
                return ExitError;
            }
        } else {
            explainPath = resolveProfileFileArg(parsedName);
        }

        ResolvedProfile profile;
        std::string error;
        if (!loadProfile(explainPath, profile, error)) {
            std::cerr << "profile explain failed: " << error << "\n";
            return ExitError;
        }

        ProfileExplainOptions options;
        AppConfig cfg;
        try {
            cfg = loadConfig(gPaths.configPath.string());
            applyConfigDefaults(cfg);
            options.config = &cfg;
        } catch (const std::exception&) {
            options.config = nullptr;
        }
        if (!targetOpt.empty()) {
            const auto value = targetOpt;
            if (value == "all") {
                options.hasTarget = true;
                options.allTargets = true;
            } else if (!parseTemplatePolicyTarget(value, options.target)) {
                std::cerr << "invalid --target value: " << value << "\n";
                return ExitUsage;
            } else {
                options.hasTarget = true;
            }
        }

        if (jsonOutput) {
            printJsonLine(explainProfileJson(profile, options));
        } else {
            std::cout << explainProfileText(profile, options);
        }
        return ExitOk;
    }

    std::cerr << "unknown profile command: " << cmd << "\n";
    std::cerr << "Run 'subcli profile --help' to see available profile commands.\n";
    return ExitUsage;
}

int doExportCommand(const std::vector<std::string>& args) {
    if (hasHelp(args)) {
        if (args.size() >= 3 && args[1] == "help" && isKnownSubcommand(args[2], {"all", "mihomo", "sing-box", "xray"})) {
            printExportTargetUsage(args[2]);
        } else if (args.size() == 3 && isHelpToken(args[2]) && isKnownSubcommand(args[1], {"all", "mihomo", "sing-box", "xray"})) {
            printExportTargetUsage(args[1]);
        } else {
            printExportUsage();
        }
        return 0;
    }
    ensureDefaults();
    if (args.size() < 2) {
        printExportUsage();
        return 1;
    }

    CLI::App parser("export");
    parser.set_help_flag("");
    parser.allow_extras(false);
    std::string target;
    bool tunFlag = false;
    bool checkFlag = false;
    std::string checkTimeoutOpt;
    std::string outputDirOpt;
    std::string profileOpt;
    std::vector<std::string> subFilters;
    std::vector<std::string> tagFilters;
    bool strictNetworkFlag = false;
    bool strictCapabilitiesFlag = false;
    bool downloadAssetsFlag = false;
    bool explainPolicyFlag = false;
    bool jsonFlag = false;

    parser.add_option("target", target);
    parser.add_flag("--tun", tunFlag);
    parser.add_flag("--check", checkFlag);
    parser.add_option("--check-timeout", checkTimeoutOpt);
    parser.add_option("--output-dir", outputDirOpt);
    parser.add_option("--profile", profileOpt);
    parser.add_option("--sub", subFilters);
    parser.add_option("--tag", tagFilters);
    parser.add_flag("--strict-network", strictNetworkFlag);
    parser.add_flag("--strict-capabilities", strictCapabilitiesFlag);
    parser.add_flag("--download-assets", downloadAssetsFlag);
    parser.add_flag("--explain-policy", explainPolicyFlag);
    parser.add_flag("--json", jsonFlag);

    if (!parseCliArgs(parser, args)) {
        printExportUsage();
        return ExitUsage;
    }

    if (target != "all" && target != "mihomo" && target != "sing-box" && target != "xray") {
        std::cerr << "unknown export target: " << target << "\n";
        return ExitUsage;
    }

    auto subs = loadSubscriptions(gPaths.subPath.string());
    auto cfg = loadConfig(gPaths.configPath.string());
    applyConfigDefaults(cfg);
    const bool tun = tunFlag ? true : cfg.tun;
    const bool strictNetwork = strictNetworkFlag;
    const bool strictCapabilities = strictCapabilitiesFlag;
    const bool jsonOutput = jsonFlag;
    const bool downloadAssets = downloadAssetsFlag;
    const auto missing = missingAssets(cfg);
    if (!missing.empty()) {
        if (!downloadAssets) {
            for (const auto& asset : missing) {
                std::cerr << "warning: missing asset: " << asset.key << " at " << asset.path << "\n";
            }
            std::cerr << "warning: run 'subcli asset update' or export with --download-assets before direct core use\n";
        } else {
            int failedAssets = 0;
            for (const auto& asset : missing) {
                std::string assetError;
                if (!updateAsset(asset, cfg.timeout, cfg.fetchMaxBytes, assetError)) {
                    ++failedAssets;
                    std::cerr << "asset update failed: " << asset.key << ": " << assetError << "\n";
                    continue;
                }
                std::cout << "updated asset: " << asset.key << " -> " << asset.path << "\n";
            }
            if (failedAssets > 0) {
                return ExitError;
            }
        }
    }
    int checkTimeoutSec = 30;
    if (!checkTimeoutOpt.empty() && !parseBoundedIntValue(checkTimeoutOpt, "--check-timeout", 1, checkTimeoutSec)) {
        return 1;
    }
    std::string outputDir;
    if (!outputDirOpt.empty()) {
        outputDir = resolveFromCliCwd(outputDirOpt);
    } else {
        outputDir = cfg.outputDir;
    }
    const auto onlySubs = subFilters;
    const auto onlyTags = tagFilters;
    std::string profileOverride;
    if (!profileOpt.empty()) {
        profileOverride = profileOpt;
        if (!isBuiltInProfileNameForCli(profileOverride)) {
            profileOverride = resolveProfileFileArg(profileOverride);
        }
    }
    std::filesystem::create_directories(outputDir);

    std::vector<size_t> selected;
    selected.reserve(subs.size());
    for (size_t i = 0; i < subs.size(); ++i) {
        const auto& s = subs[i];
        if (!s.enabled) {
            continue;
        }
        if (!onlySubs.empty()) {
            bool matched = false;
            for (const auto& idOrName : onlySubs) {
                if (s.id == idOrName || s.name == idOrName) {
                    matched = true;
                    break;
                }
            }
            if (!matched) {
                continue;
            }
        }
        if (!onlyTags.empty()) {
            bool matched = false;
            for (const auto& t : onlyTags) {
                if (std::find(s.tags.begin(), s.tags.end(), t) != s.tags.end()) {
                    matched = true;
                    break;
                }
            }
            if (!matched) {
                continue;
            }
        }
        selected.push_back(i);
    }

    if (selected.empty()) {
        std::cerr << "no subscriptions selected for export (check --sub/--tag filters)\n";
        return 1;
    }

    std::stable_sort(selected.begin(), selected.end(), [&](size_t a, size_t b) {
        if (subs[a].priority != subs[b].priority) {
            return subs[a].priority < subs[b].priority;
        }
        return subs[a].id < subs[b].id;
    });

    std::vector<FetchResult> fetchResults(subs.size());
    runParallelJobs(selected.size(), cfg.parallelism, [&](size_t jobIdx) {
        const size_t idx = selected[jobIdx];
        const auto effective = withEffectiveNetworkPolicy(subs[idx], cfg);
        fetchResults[idx] = fetchSubscriptionWithRetry(effective, !strictNetwork);
    });

    std::vector<ProxyNode> allNodes;
    int skippedNodes = 0;
    for (auto idx : selected) {
        auto& s = subs[idx];
        const auto& fr = fetchResults[idx];
        if (!fr.ok) {
            std::cerr << "fetch failed for " << s.id << ": " << fr.error << "\n";
            continue;
        }
        if (fr.usedCache) {
            std::cerr << "warning: " << s.id << " used cached subscription content (" << fr.cacheReason << ")\n";
        }
        auto parsed = parseSubscriptionForSub(s, fr.content, cfg);
        for (auto& node : parsed.nodes) {
            node.sourceTags = s.tags;
        }
        skippedNodes += parsed.skipped;
        for (const auto& warning : parsed.warnings) {
            std::cerr << "warning: " << s.id << " - " << warning.message << "\n";
        }
        std::string reason;
        if (!hasUsableParsedNodes(parsed, reason)) {
            std::cerr << reason << " for " << s.id << "\n";
            continue;
        }
        allNodes.insert(allNodes.end(), parsed.nodes.begin(), parsed.nodes.end());
    }

    if (allNodes.empty()) {
        std::cerr << "no nodes parsed from enabled subscriptions\n";
        return 1;
    }

    int success = 0;
    int failed = 0;
    std::string error;
    bool mihomoOk = false;
    bool singOk = false;
    bool xrayOk = false;
    nlohmann::json exportTargets = nlohmann::json::array();
    ResolvedProfile resolvedProfile;
    bool profileLoaded = false;
    if (!loadExportProfile(cfg, gPaths.profileDir.string(), profileOverride, resolvedProfile, profileLoaded, error)) {
        std::cerr << error << "\n";
        return 1;
    }
    const ResolvedProfile* exportProfile = profileLoaded ? &resolvedProfile : nullptr;
    auto capabilityLevelName = [](CapabilityLevel level) {
        switch (level) {
            case CapabilityLevel::Native:
                return "native";
            case CapabilityLevel::Degraded:
                return "degraded";
            case CapabilityLevel::Unsupported:
                return "unsupported";
            case CapabilityLevel::RequiresAsset:
                return "requires_asset";
        }
        return "native";
    };

    auto capabilityFindingsForTarget = [&](ExportTarget exportTarget) {
        std::vector<nlohmann::json> findingItems;
        if (exportProfile != nullptr) {
            for (const auto& item : assessProfileCapabilities(exportTarget, *exportProfile, cfg)) {
                findingItems.push_back(
                    {
                        {"level", capabilityLevelName(item.level)},
                        {"code", item.code},
                        {"subject", item.subject},
                        {"message", item.message},
                    }
                );
            }
        }
        for (const auto& item : assessNodeCapabilities(exportTarget, allNodes)) {
            findingItems.push_back(
                {
                    {"level", capabilityLevelName(item.level)},
                    {"code", item.code},
                    {"subject", item.subject},
                    {"message", item.message},
                }
            );
        }

        std::sort(findingItems.begin(), findingItems.end(), [](const nlohmann::json& a, const nlohmann::json& b) {
            return std::tie(a["level"], a["code"], a["subject"], a["message"]) < std::tie(b["level"], b["code"], b["subject"], b["message"]);
        });

        nlohmann::json findings = nlohmann::json::array();
        for (const auto& item : findingItems) {
            findings.push_back(item);
        }
        return findings;
    };

    auto capabilityCountsForTarget = [&](ExportTarget exportTarget) {
        std::map<std::string, int> counts = {
            {"native", 0},
            {"degraded", 0},
            {"unsupported", 0},
            {"requires_asset", 0},
        };
        if (exportProfile != nullptr) {
            for (const auto& item : assessProfileCapabilities(exportTarget, *exportProfile, cfg)) {
                if (item.level == CapabilityLevel::Native) {
                    ++counts["native"];
                } else if (item.level == CapabilityLevel::Degraded) {
                    ++counts["degraded"];
                } else if (item.level == CapabilityLevel::Unsupported) {
                    ++counts["unsupported"];
                } else if (item.level == CapabilityLevel::RequiresAsset) {
                    ++counts["requires_asset"];
                }
            }
        }
        for (const auto& item : assessNodeCapabilities(exportTarget, allNodes)) {
            if (item.level == CapabilityLevel::Unsupported) {
                ++counts["unsupported"];
            }
        }
        return counts;
    };

    auto printCapabilitySummaryForTarget = [&](ExportTarget exportTarget, const std::string& displayName) {
        const auto counts = capabilityCountsForTarget(exportTarget);
        std::cout << displayName << " capability summary:"
                  << " native=" << counts.at("native")
                  << " degraded=" << counts.at("degraded")
                  << " unsupported=" << counts.at("unsupported")
                  << " requires_asset=" << counts.at("requires_asset") << "\n";
    };
    if (strictCapabilities && exportProfile != nullptr) {
        nlohmann::json strictViolations = nlohmann::json::array();
        auto hasStrictViolation = [&](ExportTarget exportTarget) {
            for (const auto& item : assessProfileCapabilities(exportTarget, *exportProfile, cfg)) {
                if (item.level == CapabilityLevel::Degraded || item.level == CapabilityLevel::Unsupported) {
                    strictViolations.push_back(
                        {
                            {"target", templatePolicyTargetKey(exportTarget)},
                            {"level", capabilityLevelName(item.level)},
                            {"code", item.code},
                            {"subject", item.subject},
                            {"message", item.message},
                        }
                    );
                    if (!jsonOutput) {
                        std::cerr << "strict-capabilities blocked " << templatePolicyTargetKey(exportTarget) << ": "
                                  << item.subject << " " << item.message << "\n";
                    }
                    return true;
                }
            }
            for (const auto& item : assessNodeCapabilities(exportTarget, allNodes)) {
                if (item.level == CapabilityLevel::Unsupported) {
                    strictViolations.push_back(
                        {
                            {"target", templatePolicyTargetKey(exportTarget)},
                            {"level", capabilityLevelName(item.level)},
                            {"code", item.code},
                            {"subject", item.subject.empty() ? std::string("node") : item.subject},
                            {"message", item.message},
                        }
                    );
                    if (!jsonOutput) {
                        std::cerr << "strict-capabilities blocked " << templatePolicyTargetKey(exportTarget) << ": "
                                  << (item.subject.empty() ? std::string("node") : item.subject) << " " << item.message << "\n";
                    }
                    return true;
                }
            }
            return false;
        };

        bool blocked = false;
        if (target == "all") {
            blocked = hasStrictViolation(ExportTarget::Mihomo) || hasStrictViolation(ExportTarget::SingBox) || hasStrictViolation(ExportTarget::Xray);
        } else if (target == "mihomo") {
            blocked = hasStrictViolation(ExportTarget::Mihomo);
        } else if (target == "sing-box") {
            blocked = hasStrictViolation(ExportTarget::SingBox);
        } else if (target == "xray") {
            blocked = hasStrictViolation(ExportTarget::Xray);
        }
        if (blocked) {
            if (jsonOutput) {
                printJsonLine({{"summary", {{"success", 0}, {"failed", 1}, {"skipped_nodes", skippedNodes}}}, {"strict_capabilities_blocked", true}, {"violations", strictViolations}});
            }
            return ExitError;
        }
    }
    if (explainPolicyFlag) {
        if (exportProfile == nullptr) {
            std::cout << "policy explain: no export profile loaded; using legacy/default exporter behavior\n";
        } else {
            std::cout << "policy explain: profile=" << exportProfile->name << "\n";
            if (target == "all") {
                printExportPolicyExplainForTarget(ExportTarget::Mihomo, exportProfile);
                printExportPolicyExplainForTarget(ExportTarget::SingBox, exportProfile);
                printExportPolicyExplainForTarget(ExportTarget::Xray, exportProfile);
            } else if (target == "mihomo") {
                printExportPolicyExplainForTarget(ExportTarget::Mihomo, exportProfile);
            } else if (target == "sing-box") {
                printExportPolicyExplainForTarget(ExportTarget::SingBox, exportProfile);
            } else if (target == "xray") {
                printExportPolicyExplainForTarget(ExportTarget::Xray, exportProfile);
            }
        }
    }

    auto runMihomo = [&]() {
        auto result = exportForTarget(ExportTarget::Mihomo, allNodes, cfg, tun, exportProfile, outputDir + "/mihomo.yaml", error);
        if (result.ok) {
            const auto counts = capabilityCountsForTarget(ExportTarget::Mihomo);
            if (!jsonOutput) {
                std::cout << "exported mihomo: " << outputDir << "/mihomo.yaml\n";
                printCapabilitySummaryForTarget(ExportTarget::Mihomo, "mihomo");
                if (result.skipped > 0) {
                    std::cout << "mihomo skipped nodes: " << result.skipped << "\n";
                }
            }
            exportTargets.push_back({{"target", "mihomo"}, {"ok", true}, {"output", outputDir + "/mihomo.yaml"}, {"skipped", result.skipped}, {"capabilities", counts}, {"findings", capabilityFindingsForTarget(ExportTarget::Mihomo)}});
            ++success;
            mihomoOk = true;
        } else {
            if (!jsonOutput) {
                std::cerr << "mihomo export failed: " << error << "\n";
            }
            exportTargets.push_back({{"target", "mihomo"}, {"ok", false}, {"error", error}});
            ++failed;
        }
    };

    auto runSing = [&]() {
        auto result = exportForTarget(ExportTarget::SingBox, allNodes, cfg, tun, exportProfile, outputDir + "/sing-box.json", error);
        if (result.ok) {
            const auto counts = capabilityCountsForTarget(ExportTarget::SingBox);
            if (!jsonOutput) {
                std::cout << "exported sing-box: " << outputDir << "/sing-box.json\n";
                printCapabilitySummaryForTarget(ExportTarget::SingBox, "sing-box");
                if (result.skipped > 0) {
                    std::cout << "sing-box skipped nodes: " << result.skipped << "\n";
                }
            }
            exportTargets.push_back({{"target", "sing-box"}, {"ok", true}, {"output", outputDir + "/sing-box.json"}, {"skipped", result.skipped}, {"capabilities", counts}, {"findings", capabilityFindingsForTarget(ExportTarget::SingBox)}});
            ++success;
            singOk = true;
        } else {
            if (!jsonOutput) {
                std::cerr << "sing-box export failed: " << error << "\n";
            }
            exportTargets.push_back({{"target", "sing-box"}, {"ok", false}, {"error", error}});
            ++failed;
        }
    };

    auto runXray = [&]() {
        auto result = exportForTarget(ExportTarget::Xray, allNodes, cfg, tun, exportProfile, outputDir + "/xray.json", error);
        if (result.ok) {
            const auto counts = capabilityCountsForTarget(ExportTarget::Xray);
            if (!jsonOutput) {
                std::cout << "exported xray: " << outputDir << "/xray.json\n";
                printCapabilitySummaryForTarget(ExportTarget::Xray, "xray");
                if (result.skipped > 0) {
                    std::cout << "xray skipped nodes: " << result.skipped << "\n";
                }
            }
            exportTargets.push_back({{"target", "xray"}, {"ok", true}, {"output", outputDir + "/xray.json"}, {"skipped", result.skipped}, {"capabilities", counts}, {"findings", capabilityFindingsForTarget(ExportTarget::Xray)}});
            ++success;
            xrayOk = true;
        } else {
            if (!jsonOutput) {
                std::cerr << "xray export failed: " << error << "\n";
            }
            exportTargets.push_back({{"target", "xray"}, {"ok", false}, {"error", error}});
            ++failed;
        }
    };

    if (target == "all") {
        runMihomo();
        runSing();
        runXray();
    } else if (target == "mihomo") {
        runMihomo();
    } else if (target == "sing-box") {
        runSing();
    } else if (target == "xray") {
        runXray();
    }

    auto setJsonTargetCheck = [&](const std::string& targetName, const nlohmann::json& checkObj) {
        for (auto& item : exportTargets) {
            if (item.value("target", std::string()) == targetName) {
                item["check"] = checkObj;
                break;
            }
        }
    };

    if (jsonOutput) {
        const bool checkRequested = checkFlag;
        for (auto& item : exportTargets) {
            if (checkRequested) {
                item["check"] = {{"requested", true}, {"ok", nullptr}, {"message", "check not run"}};
            } else {
                item["check"] = {{"requested", false}, {"ok", nullptr}, {"message", "check not requested"}};
            }
        }
    }

    if (checkFlag) {
        int checkFailed = 0;
        if ((target == "all" || target == "mihomo") && mihomoOk) {
            if (jsonOutput) {
                const auto check = runConfigCheckForTargetResult(cfg, ExportTarget::Mihomo, outputDir + "/mihomo.yaml", checkTimeoutSec);
                setJsonTargetCheck("mihomo", {{"requested", true}, {"ok", check.ok}, {"message", check.ok ? std::string("check passed") : check.message}});
                if (!check.ok) {
                    ++checkFailed;
                }
            } else {
                checkFailed += runConfigCheckForTarget(cfg, ExportTarget::Mihomo, outputDir + "/mihomo.yaml", checkTimeoutSec);
            }
        }
        if ((target == "all" || target == "sing-box") && singOk) {
            if (jsonOutput) {
                const auto check = runConfigCheckForTargetResult(cfg, ExportTarget::SingBox, outputDir + "/sing-box.json", checkTimeoutSec);
                setJsonTargetCheck("sing-box", {{"requested", true}, {"ok", check.ok}, {"message", check.ok ? std::string("check passed") : check.message}});
                if (!check.ok) {
                    ++checkFailed;
                }
            } else {
                checkFailed += runConfigCheckForTarget(cfg, ExportTarget::SingBox, outputDir + "/sing-box.json", checkTimeoutSec);
            }
        }
        if ((target == "all" || target == "xray") && xrayOk) {
            if (jsonOutput) {
                const auto check = runConfigCheckForTargetResult(cfg, ExportTarget::Xray, outputDir + "/xray.json", checkTimeoutSec);
                setJsonTargetCheck("xray", {{"requested", true}, {"ok", check.ok}, {"message", check.ok ? std::string("check passed") : check.message}});
                if (!check.ok) {
                    ++checkFailed;
                }
            } else {
                checkFailed += runConfigCheckForTarget(cfg, ExportTarget::Xray, outputDir + "/xray.json", checkTimeoutSec);
            }
        }
        if (checkFailed > 0) {
            ++failed;
        }
    }

    if (jsonOutput) {
        printJsonLine({{"summary", {{"success", success}, {"failed", failed}, {"skipped_nodes", skippedNodes}}}, {"targets", exportTargets}});
    } else {
        std::cout << "export summary: success=" << success << " failed=" << failed << " skipped_nodes=" << skippedNodes << "\n";
    }
    return failed > 0 ? 1 : (success > 0 ? 0 : 1);
}

int doWorkspaceCommand(const std::vector<std::string>& args) {
    if (hasHelp(args)) {
        if (args.size() >= 3 && args[1] == "help" && isKnownSubcommand(args[2], {"init", "status", "use", "unset", "migrate", "doctor"})) {
            printWorkspaceSubcommandUsage(args[2]);
        } else if (args.size() == 3 && isHelpToken(args[2]) && isKnownSubcommand(args[1], {"init", "status", "use", "unset", "migrate", "doctor"})) {
            printWorkspaceSubcommandUsage(args[1]);
        } else {
            printWorkspaceUsage();
        }
        return ExitOk;
    }
    if (args.size() < 2) {
        printWorkspaceUsage();
        return ExitUsage;
    }

    CLI::App parser("workspace");
    parser.set_help_flag("");
    parser.allow_extras(false);
    parser.require_subcommand(1);
    std::string dirArg;
    bool jsonOutput = false;
    std::string toOpt;
    std::string fromOpt;
    bool dryRun = false;
    bool overwrite = false;
    auto* initCmd = parser.add_subcommand("init");
    initCmd->add_option("dir", dirArg)->required(false);
    auto* statusCmd = parser.add_subcommand("status");
    statusCmd->add_flag("--json", jsonOutput);
    auto* useCmd = parser.add_subcommand("use");
    useCmd->add_option("dir", dirArg);
    parser.add_subcommand("unset");
    auto* migrateCmd = parser.add_subcommand("migrate");
    migrateCmd->add_option("--to", toOpt);
    migrateCmd->add_option("--from", fromOpt);
    migrateCmd->add_flag("--dry-run", dryRun);
    migrateCmd->add_flag("--overwrite", overwrite);
    parser.add_subcommand("doctor");

    if (!parseCliArgs(parser, args)) {
        printWorkspaceUsage();
        return ExitUsage;
    }

    std::string mode;
    if (*initCmd) mode = "init";
    else if (*statusCmd) mode = "status";
    else if (*useCmd) mode = "use";
    else if (*migrateCmd) mode = "migrate";
    else if (*parser.get_subcommand("unset")) mode = "unset";
    else if (*parser.get_subcommand("doctor")) mode = "doctor";

    if (mode == "init") {
        std::string initRoot;
        if (!dirArg.empty()) {
            initRoot = dirArg;
        } else {
            initRoot = normalizeAbsolutePath(std::filesystem::current_path()).string() + "/subcli-workspace";
        }
        const auto r = workspaceInit(initRoot);
        if (!r.ok) {
            std::cerr << "workspace init failed: " << r.error << "\n";
            return ExitError;
        }
        std::cout << "workspace initialized: " << r.root << "\n";
        return ExitOk;
    }

    if (mode == "status") {
        const auto status = workspaceStatus();
        if (!status.ok) {
            std::cerr << "workspace status failed: " << status.error << "\n";
            return ExitError;
        }
        const auto metadata = workspaceReadMetadata(gPaths.root.string());
        if (jsonOutput) {
            nlohmann::json metadataJson = {
                {"exists", metadata.exists},
                {"valid", metadata.valid},
                {"env_version", metadata.envVersion},
                {"name", metadata.name},
                {"created_at", metadata.createdAt},
                {"description", metadata.description},
                {"error", metadata.error},
            };
            printJsonLine({
                {"active_root", gPaths.root.string()},
                {"active_root_has_legacy_marker", std::filesystem::exists(gPaths.root / ".subcli-workspace")},
                {"metadata", metadataJson},
                {"has_default", status.hasDefault},
                {"default_root", status.defaultRoot},
                {"default_root_exists", status.defaultRootExists},
                {"default_root_has_marker", status.defaultRootHasMarker},
                {"persisted_path", status.persistedPath},
            });
            return ExitOk;
        }
        std::cout << "active workspace: " << gPaths.root.string() << "\n";
        if (!metadata.exists) {
            std::cout << "metadata: missing (legacy workspace is still supported)\n";
        } else if (!metadata.valid) {
            std::cout << "metadata: invalid (" << metadata.error << ")\n";
        } else {
            std::cout << "metadata: env_version=" << metadata.envVersion << " name=" << metadata.name << " created_at=" << metadata.createdAt
                      << " description=" << metadata.description << "\n";
        }
        if (!status.hasDefault) {
            std::cout << "no persisted default workspace\n";
        } else {
            std::cout << "persisted default workspace: " << status.defaultRoot
                      << " (exists=" << (status.defaultRootExists ? "true" : "false")
                      << " marker=" << (status.defaultRootHasMarker ? "true" : "false") << ")\n";
        }
        return ExitOk;
    }

    if (mode == "use") {
        if (dirArg.empty()) {
            std::cerr << "workspace use requires DIR\n";
            return ExitUsage;
        }
        std::string error;
        if (!workspaceUse(dirArg, error)) {
            std::cerr << "workspace use failed: " << error << "\n";
            return ExitError;
        }
        std::cout << "default workspace set to: " << dirArg << "\n";
        return ExitOk;
    }

    if (mode == "unset") {
        std::string error;
        if (!workspaceUnset(error)) {
            std::cerr << "workspace unset failed: " << error << "\n";
            return ExitError;
        }
        std::cout << "default workspace removed\n";
        return ExitOk;
    }

    if (mode == "migrate") {
        WorkspaceMigrateOptions options;
        if (!fromOpt.empty()) {
            options.fromRoot = fromOpt;
        } else {
            options.fromRoot = gPaths.root.string();
        }
        options.toRoot = toOpt;
        if (options.toRoot.empty()) {
            std::cerr << "workspace migrate requires --to DIR\n";
            return ExitUsage;
        }
        options.dryRun = dryRun;
        options.overwrite = overwrite;

        const auto result = workspaceMigrate(options);
        if (!result.ok) {
            std::cerr << "workspace migrate failed: " << result.error << "\n";
            return ExitError;
        }
        if (options.dryRun) {
            std::cout << "migrate dry-run\n";
        }
        std::cout << "workspace migrated: " << result.copied.size() << " items copied, " << result.skipped.size() << " items skipped\n";
        for (const auto& item : result.copied) {
            std::cout << "  copied: " << item << "\n";
        }
        for (const auto& item : result.skipped) {
            std::cout << "  skipped: " << item << "\n";
        }
        return ExitOk;
    }

    if (mode == "doctor") {
        const auto findings = buildWorkspaceDoctorFindings(gPaths.root);
        int okCount = 0;
        int warnCount = 0;
        int failCount = 0;
        for (const auto& finding : findings) {
            if (finding.level == "ok") {
                ++okCount;
                std::cout << "[ OK ] " << finding.detail << "\n";
            } else if (finding.level == "warn") {
                ++warnCount;
                std::cout << "[WARN] " << finding.detail << "\n";
            } else {
                ++failCount;
                std::cout << "[FAIL] " << finding.detail << "\n";
            }
        }
        std::cout << "workspace doctor summary: root=" << gPaths.root.string() << " ok=" << okCount << " warn=" << warnCount << " fail=" << failCount << "\n";
        return failCount > 0 ? ExitError : ExitOk;
    }

    std::cerr << "unknown workspace mode: " << mode << "\n";
    return ExitUsage;
}

int doDaemonCommand(const std::vector<std::string>& args) {
    if (hasHelp(args)) {
        if (args.size() >= 3 && args[1] == "help" && isKnownSubcommand(args[2], {"once", "run", "start", "stop", "status"})) {
            printDaemonSubcommandUsage(args[2]);
        } else if (args.size() == 3 && isHelpToken(args[2]) && isKnownSubcommand(args[1], {"once", "run", "start", "stop", "status"})) {
            printDaemonSubcommandUsage(args[1]);
        } else {
            printDaemonUsage();
        }
        return ExitOk;
    }
    if (args.size() < 2) {
        printDaemonUsage();
        return ExitUsage;
    }

    CLI::App parser("daemon");
    parser.set_help_flag("");
    parser.allow_extras(false);
    parser.require_subcommand(1);
    int intervalSec = 3600;
    std::string target = "all";
    bool updateAssets = false;
    bool strictNetwork = false;
    bool check = false;
    bool noRestart = false;
    std::string pidFile;
    std::string logFile;

    auto attachRunLike = [&](CLI::App* sc) {
        sc->add_option("--interval", intervalSec);
        sc->add_option("--target", target);
        sc->add_flag("--update-assets", updateAssets);
        sc->add_flag("--strict-network", strictNetwork);
        sc->add_flag("--check", check);
        sc->add_flag("--no-restart", noRestart);
        sc->add_option("--pid-file", pidFile);
        sc->add_option("--log-file", logFile);
    };

    auto* onceCmd = parser.add_subcommand("once");
    auto* runCmd = parser.add_subcommand("run");
    auto* startCmd = parser.add_subcommand("start");
    auto* stopCmd = parser.add_subcommand("stop");
    auto* statusCmd = parser.add_subcommand("status");
    attachRunLike(onceCmd);
    attachRunLike(runCmd);
    attachRunLike(startCmd);
    stopCmd->add_option("--pid-file", pidFile);
    statusCmd->add_option("--pid-file", pidFile);

    if (!parseCliArgs(parser, args)) {
        printDaemonUsage();
        return ExitUsage;
    }

    const std::string mode = daemonModeFromCommands(onceCmd, runCmd, startCmd, stopCmd, statusCmd);

    if (mode != "once" && mode != "run" && mode != "start" && mode != "stop" && mode != "status") {
        std::cerr << "unknown daemon mode: " << mode << "\n";
        return ExitUsage;
    }

    if (mode == "status") return doDaemonStatusMode();

    if (mode == "stop") return doDaemonStopMode();

    if (!parseBoundedIntValue(std::to_string(intervalSec), "--interval", 1, intervalSec)) {
        return ExitUsage;
    }
    if (!validateDaemonTarget(target)) {
        return ExitUsage;
    }

    const DaemonOptions options = buildDaemonOptionsFromCli(intervalSec, target, updateAssets, strictNetwork, check, noRestart, pidFile, logFile);
    const DaemonCallbacks callbacks = makeDaemonCallbacks();

    if (mode == "start") {
        return doDaemonStartMode(options);
    }

    if (mode == "once") {
        return doDaemonOnceMode(options, callbacks);
    }

    return runDaemonLoopMode(options, callbacks);
}

} // namespace

int main(int argc, char** argv) {
    struct CurlGlobal {
        CurlGlobal() { curl_global_init(CURL_GLOBAL_DEFAULT); }
        ~CurlGlobal() { curl_global_cleanup(); }
    } curlGlobal;

    try {
        const std::string argv0 = (argc > 0 && argv[0]) ? argv[0] : "";
        gPaths = buildRuntimePaths(argv0);
        gExecutablePath = detectExecutablePath(argv0);

        std::string cliWorkspace;

        CLI::App cli("subcli");
        cli.set_help_flag("");
        cli.allow_extras();
        cli.prefix_command();
        cli.add_option("--workspace", cliWorkspace, "Use a workspace for this invocation");

        std::string cmd;
        auto* commandOption = cli.add_option("command", cmd, "Command to run");
        commandOption->required(false);
        commandOption->check(CLI::IsMember(
            {
                "init", "doctor", "sub", "config", "template", "asset", "profile", "export", "daemon", "run", "stop", "status",
                "restart", "check", "completion", "workspace"
            }
        ));

        auto buildTail = [&](const std::string& name, const std::vector<std::string>& extra) {
            std::vector<std::string> tail;
            tail.push_back(name);
            tail.insert(tail.end(), extra.begin(), extra.end());
            return tail;
        };

        if (argc < 2) {
            printRootUsage();
            return ExitOk;
        }

        const std::string cmdToken = argv[1] ? std::string(argv[1]) : std::string();
        if (cmdToken == "--help" || cmdToken == "-h" || cmdToken == "help") {
            printRootUsage();
            return ExitOk;
        }

        std::vector<std::string> extra;
        try {
            cli.parse(argc, argv);
            extra = cli.remaining();
        } catch (const CLI::ParseError& ex) {
            std::cerr << "unknown command: " << cmdToken << "\n";
            std::cerr << "Run 'subcli --help' to see available commands.\n";
            return ExitUsage;
        }

        const std::string envWorkspace = [&]() {
            const char* raw = std::getenv("SUBCLI_WORKSPACE");
            return raw && *raw ? std::string(raw) : "";
        }();

        EnvironmentResolveInput input;
        input.cliWorkspace = cliWorkspace;
        input.envWorkspace = envWorkspace;
        input.cwd = std::filesystem::current_path().string();
        input.persistedWorkspace = readPersistedDefaultWorkspace();
        input.platform = detectPlatformKind();

        gEnvResult = resolveEnvironment(input);
        if (!gEnvResult.ok) {
            std::cerr << "environment resolution failed: " << gEnvResult.error << "\n";
            return ExitError;
        }

        if (gEnvResult.source != EnvironmentSource::PlatformDefault) {
            gPaths.root = gEnvResult.paths.root;
            gPaths.configDir = gEnvResult.paths.configDir;
            gPaths.dataDir = gEnvResult.paths.dataDir;
            gPaths.cacheDir = gEnvResult.paths.cacheDir;
            gPaths.stateDir = gEnvResult.paths.stateDir;
            gPaths.outputDir = gEnvResult.paths.outputDir;
            gPaths.templateDir = gEnvResult.paths.templateDir;
            gPaths.profileDir = gEnvResult.paths.profileDir;
            gPaths.subPath = gEnvResult.paths.subPath;
            gPaths.configPath = gEnvResult.paths.configPath;
        }

        if (cmd.empty()) {
            printRootUsage();
            return ExitOk;
        }

        if (cmd == "sub") {
            return doSubCommand(buildTail("sub", extra));
        }
        if (cmd == "init") {
            return doInitCommand(buildTail("init", extra));
        }
        if (cmd == "doctor") {
            return doDoctorCommand(buildTail("doctor", extra));
        }
        if (cmd == "config") {
            return doConfigCommand(buildTail("config", extra));
        }
        if (cmd == "template") {
            return doTemplateCommand(buildTail("template", extra));
        }
        if (cmd == "asset") {
            return doAssetCommand(buildTail("asset", extra));
        }
        if (cmd == "profile") {
            return doProfileCommand(buildTail("profile", extra));
        }
        if (cmd == "export") {
            return doExportCommand(buildTail("export", extra));
        }
        if (cmd == "daemon") {
            return doDaemonCommand(buildTail("daemon", extra));
        }
        if (cmd == "run") {
            return doRunCommand(buildTail("run", extra));
        }
        if (cmd == "stop") {
            return doStopCommand(buildTail("stop", extra));
        }
        if (cmd == "status") {
            return doStatusCommand(buildTail("status", extra));
        }
        if (cmd == "restart") {
            return doRestartCommand(buildTail("restart", extra));
        }
        if (cmd == "check") {
            return doCheckCommand(buildTail("check", extra));
        }
        if (cmd == "completion") {
            return doCompletionCommand(buildTail("completion", extra));
        }
        if (cmd == "workspace") {
            return doWorkspaceCommand(buildTail("workspace", extra));
        }

        printRootUsage();
        return ExitOk;
    } catch (const std::exception& ex) {
        std::cerr << "error: " << ex.what() << "\n";
        return 1;
    } catch (...) {
        std::cerr << "error: unexpected failure\n";
        return 1;
    }
}
