#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <curl/curl.h>
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
#include "subcli/exporter.hpp"
#include "subcli/fetch.hpp"
#include "subcli/parser.hpp"
#include "subcli/profile.hpp"
#include "subcli/profile_explain.hpp"
#include "subcli/store.hpp"
#include "subcli/util.hpp"
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
    std::cout << "subcli <init|doctor|sub|config|template|asset|profile|export|daemon|run|stop|status|restart|check|completion> ...\n"
              << "\n"
              << "Primary flow (cross-platform guarantee):\n"
              << "  subcli init\n"
              << "  subcli doctor\n"
              << "  subcli sub add --name NAME --url URL\n"
              << "  subcli sub update\n"
              << "  subcli export all --check\n"
              << "\n"
              << "Optional runtime helpers:\n"
              << "  subcli daemon once --target all\n"
              << "  subcli run sing-box\n"
              << "\n"
              << "Use 'subcli <command> --help' for command details.\n";
}

void printInitUsage() {
    std::cout << "usage: subcli init\n"
              << "Initializes config, data, cache, state, and output directories.\n";
}

void printDoctorUsage() {
    std::cout << "usage: subcli doctor\n"
              << "Checks config/data/cache/output dirs, templates, and configured external cores.\n";
}

void printSubUsage() {
    std::cout << "usage: subcli sub <add|remove|list|update|enable|disable|edit|validate> ...\n"
              << "  subcli sub add --name NAME --url URL [--force] [--tag TAG] [--header 'Key: Value']\n"
              << "  subcli sub remove <id|name>\n"
              << "  subcli sub list\n"
              << "  subcli sub update [id-or-name ...] [--tag TAG] [--strict-network]\n"
              << "  subcli sub validate [id-or-name]\n"
              << "  subcli sub enable <id|name> | disable <id|name>\n"
              << "  subcli sub edit <id|name> [--name NAME] [--url URL] [--header 'Key: Value'] [--remove-header Key]\n";
}

void printConfigUsage() {
    std::cout << "usage: subcli config <list|get|set|remove> ...\n"
              << "Core path keys: core_paths.mihomo, core_paths.sing_box, core_paths.xray\n"
              << "Template paths are easier to manage with 'subcli template --help'.\n";
}

void printTemplateUsage() {
    std::cout << "usage: subcli template <list|get|set|reset|validate> ...\n"
              << "  subcli template list\n"
              << "  subcli template get <mihomo|sing-box|xray> <normal|tun>\n"
              << "  subcli template set <mihomo|sing-box|xray> <normal|tun> <path>\n"
              << "  subcli template reset [mihomo|sing-box|xray] [normal|tun]\n"
              << "  subcli template validate\n";
}

void printAssetUsage() {
    std::cout << "usage: subcli asset <list|status|validate|update>\n"
              << "  subcli asset list\n"
              << "  subcli asset status\n"
              << "  subcli asset validate\n"
              << "  subcli asset update [asset-key]\n";
}

void printProfileUsage() {
    std::cout << "usage: subcli profile <list|get|validate|explain> ...\n"
              << "  subcli profile list\n"
              << "  subcli profile get <bypass-cn|global|direct>\n"
              << "  subcli profile validate <path>\n"
              << "  subcli profile explain <path-or-name> [--target <all|mihomo|sing-box|xray>] [--json]\n";
}

void printExportUsage() {
    std::cout << "usage: subcli export <all|mihomo|sing-box|xray> [--tun] [--check] [--check-timeout SEC]\n"
              << "       [--output-dir DIR] [--profile PATH_OR_NAME] [--sub ID_OR_NAME] [--tag TAG] [--strict-network] [--strict-capabilities] [--download-assets] [--explain-policy] [--json]\n";
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

void printDaemonUsage() {
    std::cout << "usage: subcli daemon <once|run|start|stop|status> [--interval SEC] [--target all|mihomo|sing-box|xray]\n"
              << "       [--update-assets] [--strict-network] [--check] [--no-restart] [--pid-file PATH] [--log-file PATH]\n"
              << "Optional helper for local process hosting; config generation remains the primary product workflow.\n";
}

void printCheckUsage() {
    std::cout << "usage: subcli check <mihomo|sing-box|xray> [--file PATH] [--output-dir DIR] [--timeout SEC]\n";
}

void printRunUsage() {
    std::cout << "usage: subcli run <mihomo|sing-box|xray> [--file PATH] [--output-dir DIR]\n";
}

void printStopUsage() {
    std::cout << "usage: subcli stop <mihomo|sing-box|xray>\n";
}

void printStatusUsage() {
    std::cout << "usage: subcli status [mihomo|sing-box|xray]\n";
}

void printRestartUsage() {
    std::cout << "usage: subcli restart <mihomo|sing-box|xray> [--file PATH] [--output-dir DIR]\n";
}

void printCompletionUsage() {
    std::cout << "usage: subcli completion bash\n";
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

bool validateOptions(
    const std::vector<std::string>& args,
    size_t start,
    const std::set<std::string>& known,
    const std::set<std::string>& valueOptions
) {
    for (size_t i = start; i < args.size(); ++i) {
        const auto& arg = args[i];
        if (arg.rfind("--", 0) != 0) {
            continue;
        }
        if (!known.count(arg)) {
            std::cerr << "unknown option: " << arg << "\n";
            return false;
        }
        if (valueOptions.count(arg)) {
            if (i + 1 >= args.size() || known.count(args[i + 1])) {
                std::cerr << "missing value for option: " << arg << "\n";
                return false;
            }
            ++i;
        }
    }
    return true;
}

std::vector<std::string> positionalArgs(
    const std::vector<std::string>& args,
    size_t start,
    const std::set<std::string>& valueOptions
) {
    std::vector<std::string> out;
    for (size_t i = start; i < args.size(); ++i) {
        const auto& arg = args[i];
        if (arg.rfind("--", 0) == 0) {
            if (valueOptions.count(arg) && i + 1 < args.size()) {
                ++i;
            }
            continue;
        }
        out.push_back(arg);
    }
    return out;
}

bool ensureNoExtraPositionals(
    const std::vector<std::string>& args,
    size_t start,
    const std::set<std::string>& valueOptions,
    const std::string& message
) {
    const auto extra = positionalArgs(args, start, valueOptions);
    if (!extra.empty()) {
        std::cerr << message << "\n";
        return false;
    }
    return true;
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
    if (!validateOptions(args, 2, {"--file", "--output-dir"}, {"--file", "--output-dir"})) {
        return ExitUsage;
    }
    if (!ensureNoExtraPositionals(args, 2, {"--file", "--output-dir"}, "run/restart accepts one target and options")) {
        return ExitUsage;
    }
    if (args.size() < 2) {
        return ExitUsage;
    }

    ExportTarget target;
    std::string defaultFile;
    if (!parseExportTarget(args[1], target, defaultFile)) {
        std::cerr << "unknown target: " << args[1] << "\n";
        return ExitUsage;
    }

    ensureDefaults();
    auto cfg = loadConfig(gPaths.configPath.string());
    applyConfigDefaults(cfg);

    const std::string configPath = runtimeConfigPathFromArgs(args, cfg, defaultFile);
    if (!fileExists(configPath)) {
        std::cerr << "runtime config does not exist: " << configPath << "\n";
        return ExitError;
    }

    const auto cores = discoverCorePaths(cfg);
    const std::string binary = coreBinaryForTarget(cores, target);
    if (binary.empty()) {
        std::cerr << "core binary not found for target: " << args[1] << "\n";
        return ExitError;
    }
    if (!isExecutableFile(binary)) {
        std::cerr << "core binary is not executable: " << binary << "\n";
        return ExitError;
    }

    std::string error;
    if (restart && !stopCoreRuntime(gPaths.stateDir, args[1], 5, error)) {
        std::cerr << "restart stop failed: " << error << "\n";
        return ExitError;
    }
    if (!startCoreRuntime(gPaths.stateDir, args[1], binary, runtimeArgsForTarget(target, configPath), configPath, error)) {
        std::cerr << "runtime start failed: " << error << "\n";
        return ExitError;
    }

    const auto status = inspectCoreRuntime(gPaths.stateDir, args[1], error);
    if (!error.empty()) {
        std::cerr << "runtime status read failed: " << error << "\n";
        return ExitError;
    }
    std::cout << "running " << args[1] << " pid=" << status.pid << " config=" << configPath << "\n";
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
    if (!validateOptions(args, 2, {}, {})) {
        return ExitUsage;
    }
    if (args.size() != 2) {
        printStopUsage();
        return ExitUsage;
    }
    ExportTarget target;
    std::string defaultFile;
    if (!parseExportTarget(args[1], target, defaultFile)) {
        std::cerr << "unknown target: " << args[1] << "\n";
        return ExitUsage;
    }
    std::string error;
    if (!stopCoreRuntime(gPaths.stateDir, args[1], 5, error)) {
        std::cerr << "runtime stop failed: " << error << "\n";
        return ExitError;
    }
    std::cout << "stopped " << args[1] << "\n";
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
    if (!validateOptions(args, 1, {}, {})) {
        return ExitUsage;
    }
    if (args.size() == 1) {
        int rc = ExitOk;
        rc = std::max(rc, printRuntimeStatus("mihomo"));
        rc = std::max(rc, printRuntimeStatus("sing-box"));
        rc = std::max(rc, printRuntimeStatus("xray"));
        return rc;
    }
    if (args.size() != 2) {
        printStatusUsage();
        return ExitUsage;
    }
    ExportTarget target;
    std::string defaultFile;
    if (!parseExportTarget(args[1], target, defaultFile)) {
        std::cerr << "unknown target: " << args[1] << "\n";
        return ExitUsage;
    }
    return printRuntimeStatus(args[1]);
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
    if (!validateOptions(args, 1, {"--json"}, {})) {
        return 1;
    }
    const bool jsonOutput = hasFlag(args, "--json");
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
        printJsonLine({{"failed", failed}, {"checks", checks}});
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
    if (!validateOptions(args, 2, {"--file", "--output-dir", "--timeout"}, {"--file", "--output-dir", "--timeout"})) {
        return ExitUsage;
    }
    if (!ensureNoExtraPositionals(args, 2, {"--file", "--output-dir", "--timeout"}, "check accepts only one target and options")) {
        return ExitUsage;
    }

    AppConfig cfg = loadConfig(gPaths.configPath.string());
    applyConfigDefaults(cfg);
    int timeoutSec = 30;
    if (hasOption(args, "--timeout") && !parseBoundedIntValue(argValue(args, "--timeout", "30"), "--timeout", 1, timeoutSec)) {
        return 1;
    }

    ExportTarget target;
    std::string defaultFile;
    if (!parseExportTarget(args[1], target, defaultFile)) {
        std::cerr << "unknown check target: " << args[1] << "\n";
        return ExitUsage;
    }

    std::string filePath = argValue(args, "--file");
    if (filePath.empty()) {
        std::string outputDir;
        if (hasOption(args, "--output-dir")) {
            outputDir = resolveFromCliCwd(argValue(args, "--output-dir", cfg.outputDir));
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
    if (args.size() != 2) {
        printCompletionUsage();
        return 1;
    }
    if (args[1] != "bash") {
        std::cerr << "unsupported completion shell: " << args[1] << "\n";
        return 1;
    }
    std::cout << generateBashCompletion();
    return 0;
}

int doSubCommand(const std::vector<std::string>& args) {
    if (hasHelp(args)) {
        printSubUsage();
        return 0;
    }
    ensureDefaults();
    auto subs = loadSubscriptions(gPaths.subPath.string());

    if (args.size() < 2) {
        printSubUsage();
        return 1;
    }

    const std::string cmd = args[1];
    if (cmd == "list") {
        if (!validateOptions(args, 2, {"--json"}, {})) {
            return 1;
        }
        const bool jsonOutput = hasFlag(args, "--json");
        if ((!jsonOutput && args.size() != 2) || (jsonOutput && args.size() != 3)) {
            std::cerr << "sub list does not accept arguments\n";
            return 1;
        }
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
        if (!validateOptions(
                args,
                2,
                {"--id", "--name", "--url", "--group", "--format-hint", "--user-agent", "--timeout", "--retry", "--priority", "--update-interval", "--tag", "--tags", "--header", "--force"},
                {"--id", "--name", "--url", "--group", "--format-hint", "--user-agent", "--timeout", "--retry", "--priority", "--update-interval", "--tag", "--tags", "--header"}
            )) {
            return 1;
        }
        const std::string name = argValue(args, "--name");
        const std::string url = argValue(args, "--url");
        const std::string maybeId = argValue(args, "--id");
        const std::string group = argValue(args, "--group", "default");
        const std::string formatHint = argValue(args, "--format-hint", "auto");
        const std::string userAgent = argValue(args, "--user-agent", "");
        int timeout = 15;
        if (!parseBoundedIntValue(argValue(args, "--timeout", "15"), "--timeout", 1, timeout)) {
            return 1;
        }
        int retry = 2;
        if (!parseBoundedIntValue(argValue(args, "--retry", "2"), "--retry", 0, retry)) {
            return 1;
        }
        const bool timeoutOverride = hasOption(args, "--timeout");
        const bool retryOverride = hasOption(args, "--retry");
        const bool force = hasFlag(args, "--force");
        std::vector<std::string> tags = argValues(args, "--tag");
        const auto tagsCsv = argValue(args, "--tags");
        if (!tagsCsv.empty()) {
            auto more = splitComma(tagsCsv);
            tags.insert(tags.end(), more.begin(), more.end());
        }
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
        for (const auto& rawHeader : argValues(args, "--header")) {
            std::string key;
            std::string value;
            if (!parseHeaderOption(rawHeader, key, value)) {
                return 1;
            }
            s.headers[key] = value;
        }
        int priority = 100;
        if (!parseBoundedIntValue(argValue(args, "--priority", "100"), "--priority", 0, priority)) {
            return 1;
        }
        s.priority = priority;
        int updateInterval = 3600;
        if (!parseBoundedIntValue(argValue(args, "--update-interval", "3600"), "--update-interval", 0, updateInterval)) {
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
        if (!validateOptions(args, 3, {}, {})) {
            return 1;
        }
        if (args.size() != 3) {
            std::cerr << "sub remove requires exactly <id|name>\n";
            return 1;
        }
        const std::string id = args[2];
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
        if (!validateOptions(args, 3, {}, {})) {
            return 1;
        }
        if (args.size() != 3) {
            std::cerr << "sub " << cmd << " requires exactly <id|name>\n";
            return 1;
        }
        auto* s = findSub(subs, args[2]);
        if (!s) {
            std::cerr << "not found: " << args[2] << "\n";
            return 1;
        }
        s->enabled = (cmd == "enable");
        saveSubscriptions(gPaths.subPath.string(), subs);
        std::cout << cmd << "d subscription: " << s->id << "\n";
        return 0;
    }

    if (cmd == "edit") {
        const std::set<std::string> editValueOptions = {"--name", "--url", "--group", "--format-hint", "--user-agent", "--timeout", "--retry", "--priority", "--update-interval", "--tags", "--header", "--remove-header"};
        if (!validateOptions(
                args,
                3,
                {"--name", "--url", "--group", "--format-hint", "--user-agent", "--timeout", "--retry", "--priority", "--update-interval", "--tags", "--header", "--remove-header", "--clear-headers", "--enable", "--disable"},
                editValueOptions
            )) {
            return 1;
        }
        if (args.size() < 3) {
            std::cerr << "sub edit requires <id|name>\n";
            return 1;
        }
        if (!ensureNoExtraPositionals(args, 3, editValueOptions, "sub edit accepts exactly one <id|name>")) {
            return 1;
        }
        auto* s = findSub(subs, args[2]);
        if (!s) {
            std::cerr << "not found: " << args[2] << "\n";
            return 1;
        }
        const auto name = argValue(args, "--name");
        const auto url = argValue(args, "--url");
        const auto group = argValue(args, "--group");
        const auto formatHint = argValue(args, "--format-hint");
        const auto userAgent = argValue(args, "--user-agent");
        const auto timeout = argValue(args, "--timeout");
        const auto retry = argValue(args, "--retry");
        const auto priority = argValue(args, "--priority");
        const auto updateInterval = argValue(args, "--update-interval");
        const auto tagsCsv = argValue(args, "--tags");

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
            s->tags = splitComma(tagsCsv);
        }
        if (hasFlag(args, "--clear-headers")) {
            s->headers.clear();
        }
        for (const auto& key : argValues(args, "--remove-header")) {
            s->headers.erase(key);
        }
        for (const auto& rawHeader : argValues(args, "--header")) {
            std::string key;
            std::string value;
            if (!parseHeaderOption(rawHeader, key, value)) {
                return 1;
            }
            s->headers[key] = value;
        }
        if (hasFlag(args, "--enable")) {
            s->enabled = true;
        }
        if (hasFlag(args, "--disable")) {
            s->enabled = false;
        }
        saveSubscriptions(gPaths.subPath.string(), subs);
        std::cout << "edited subscription: " << s->id << "\n";
        return 0;
    }

    if (cmd == "validate") {
        if (!validateOptions(args, 2, {}, {})) {
            return 1;
        }
        if (args.size() != 2 && args.size() != 3) {
            std::cerr << "sub validate accepts at most one <id|name>\n";
            return 1;
        }
        auto cfg = loadConfig(gPaths.configPath.string());
        applyConfigDefaults(cfg);
        std::string only;
        if (args.size() >= 3 && args[2].rfind("--", 0) != 0) {
            only = args[2];
        }
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
        if (!validateOptions(args, 2, {"--tag", "--strict-network"}, {"--tag"})) {
            return 1;
        }
        auto cfg = loadConfig(gPaths.configPath.string());
        applyConfigDefaults(cfg);
        const bool strictNetwork = hasFlag(args, "--strict-network");
        std::set<std::string> ids;
        for (const auto& id : positionalArgs(args, 2, {"--tag"})) {
            ids.insert(id);
        }
        auto tagsFilter = argValues(args, "--tag");

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
    return 1;
}

int doConfigCommand(const std::vector<std::string>& args) {
    if (hasHelp(args)) {
        printConfigUsage();
        return 0;
    }
    ensureDefaults();
    AppConfig cfg = loadConfig(gPaths.configPath.string());
    applyConfigDefaults(cfg);
    if (args.size() < 2) {
        printConfigUsage();
        return 1;
    }
    const std::string cmd = args[1];
    const bool jsonOutput = hasFlag(args, "--json");
    if (cmd == "list") {
        if (!validateOptions(args, 2, {"--json"}, {})) {
            return 1;
        }
        if ((!jsonOutput && args.size() != 2) || (jsonOutput && args.size() != 3)) {
            std::cerr << "config list does not accept arguments\n";
            return 1;
        }
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
        if (args.size() != 3) {
            std::cerr << "config get requires <key>\n";
            return 1;
        }
        const auto& key = args[2];
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
        if (args.size() != 4) {
            std::cerr << "config set requires <key> <value>\n";
            return 1;
        }
        const auto& key = args[2];
        const auto& value = args[3];
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
        if (args.size() != 3) {
            std::cerr << "config remove requires <key>\n";
            return 1;
        }
        const auto& key = args[2];
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
    return 1;
}

int doTemplateCommand(const std::vector<std::string>& args) {
    if (hasHelp(args)) {
        printTemplateUsage();
        return 0;
    }
    ensureDefaults();
    AppConfig cfg = loadConfig(gPaths.configPath.string());
    applyConfigDefaults(cfg);
    if (args.size() < 2) {
        printTemplateUsage();
        return 1;
    }

    const std::string cmd = args[1];
    const bool jsonOutput = hasFlag(args, "--json");
    if (cmd == "list") {
        if (!validateOptions(args, 2, {"--json"}, {})) {
            return 1;
        }
        if ((!jsonOutput && args.size() != 2) || (jsonOutput && args.size() != 3)) {
            std::cerr << "template list does not accept arguments\n";
            return 1;
        }
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
        if (args.size() != 4) {
            std::cerr << "template get requires <target> <normal|tun>\n";
            return 1;
        }
        std::string error;
        if (!parseTemplateTargetKind(args[2], args[3], error)) {
            std::cerr << error << "\n";
            return 1;
        }
        std::cout << getTemplateValue(cfg, args[2], args[3]) << "\n";
        return 0;
    }

    if (cmd == "set") {
        if (args.size() != 5) {
            std::cerr << "template set requires <target> <normal|tun> <path>\n";
            return 1;
        }
        std::string error;
        if (!parseTemplateTargetKind(args[2], args[3], error)) {
            std::cerr << error << "\n";
            return 1;
        }
        const std::string path = resolveFromCliCwd(args[4]);
        std::error_code ec;
        if (!std::filesystem::is_regular_file(path, ec) || ec) {
            std::cerr << "template file does not exist: " << path << "\n";
            return 1;
        }
        setTemplateValue(cfg, args[2], args[3], path);
        saveConfig(gPaths.configPath.string(), cfg);
        std::cout << "updated template: " << args[2] << "." << args[3] << "\n";
        return 0;
    }

    if (cmd == "reset") {
        if (args.size() > 4) {
            std::cerr << "template reset accepts [target] [normal|tun]\n";
            return 1;
        }
        if (args.size() == 2) {
            for (const auto& item : templateTargetKinds()) {
                resetTemplateValue(cfg, item.first, item.second);
            }
        } else if (args.size() == 3) {
            if (!isTemplateTarget(args[2])) {
                std::cerr << "invalid template target: " << args[2] << "\n";
                return 1;
            }
            resetTemplateValue(cfg, args[2], "normal");
            resetTemplateValue(cfg, args[2], "tun");
        } else {
            std::string error;
            if (!parseTemplateTargetKind(args[2], args[3], error)) {
                std::cerr << error << "\n";
                return 1;
            }
            resetTemplateValue(cfg, args[2], args[3]);
        }
        saveConfig(gPaths.configPath.string(), cfg);
        std::cout << "reset template configuration\n";
        return 0;
    }

    if (cmd == "validate") {
        if (!validateOptions(args, 2, {"--json"}, {})) {
            return 1;
        }
        if ((!jsonOutput && args.size() != 2) || (jsonOutput && args.size() != 3)) {
            std::cerr << "template validate does not accept arguments\n";
            return 1;
        }
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
    return 1;
}

int doAssetCommand(const std::vector<std::string>& args) {
    if (args.size() < 2 || hasHelp(args)) {
        printAssetUsage();
        return 0;
    }
    ensureDefaults();
    AppConfig cfg = loadConfig(gPaths.configPath.string());
    applyConfigDefaults(cfg);
    const auto records = configuredAssets(cfg);
    const std::string cmd = args[1];

    if (cmd == "list") {
        if (args.size() != 2) {
            std::cerr << "asset list does not accept arguments\n";
            return ExitUsage;
        }
        for (const auto& asset : records) {
            std::cout << asset.key << " path=" << asset.path << " url=" << asset.url
                      << " status=" << (asset.exists ? "present" : "missing") << "\n";
        }
        return ExitOk;
    }

    if (cmd == "status") {
        if (args.size() != 2) {
            std::cerr << "asset status does not accept arguments\n";
            return ExitUsage;
        }
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
        if (args.size() != 2) {
            std::cerr << "asset validate does not accept arguments\n";
            return ExitUsage;
        }
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
        if (args.size() > 3) {
            std::cerr << "asset update accepts at most one [asset-key]\n";
            return ExitUsage;
        }

        std::vector<AssetRecord> selected;
        if (args.size() == 3) {
            for (const auto& asset : records) {
                if (asset.key == args[2]) {
                    selected.push_back(asset);
                }
            }
            if (selected.empty()) {
                std::cerr << "asset key not found: " << args[2] << "\n";
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
    return ExitUsage;
}

int doProfileCommand(const std::vector<std::string>& args) {
    if (args.size() < 2 || hasHelp(args)) {
        printProfileUsage();
        return ExitOk;
    }
    ensureDefaults();
    AppConfig cfg = loadConfig(gPaths.configPath.string());
    applyConfigDefaults(cfg);
    const std::string cmd = args[1];
    const std::vector<std::string> builtIns = {"bypass-cn", "global", "direct"};

    if (cmd == "list") {
        if (args.size() != 2) {
            std::cerr << "profile list does not accept arguments\n";
            return ExitUsage;
        }
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
        if (args.size() != 3) {
            printProfileUsage();
            return ExitUsage;
        }
        if (!isBuiltInProfileNameForCli(args[2])) {
            std::cerr << "unknown built-in profile: " << args[2] << "\n";
            return ExitError;
        }
        std::string path;
        AppConfig builtInConfig;
        resolveExportProfilePath(builtInConfig, gPaths.profileDir.string(), args[2], path);
        std::ifstream in(path);
        if (!in) {
            std::cerr << "failed to open profile: " << path << "\n";
            return ExitError;
        }
        std::cout << in.rdbuf();
        return ExitOk;
    }

    if (cmd == "validate") {
        if (args.size() != 3) {
            printProfileUsage();
            return ExitUsage;
        }
        ResolvedProfile profile;
        std::string error;
        const std::string path = resolveProfileFileArg(args[2]);
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
        if (!validateOptions(args, 3, {"--target", "--json"}, {"--target"})) {
            return ExitUsage;
        }
        if (!ensureNoExtraPositionals(args, 3, {"--target"}, "profile explain requires one <path-or-name> and options")) {
            return ExitUsage;
        }

        std::string explainPath;
        if (isBuiltInProfileNameForCli(args[2])) {
            AppConfig builtInConfig;
            if (!resolveExportProfilePath(builtInConfig, gPaths.profileDir.string(), args[2], explainPath)) {
                std::cerr << "profile explain failed: unable to resolve built-in profile: " << args[2] << "\n";
                return ExitError;
            }
        } else {
            explainPath = resolveProfileFileArg(args[2]);
        }

        ResolvedProfile profile;
        std::string error;
        if (!loadProfile(explainPath, profile, error)) {
            std::cerr << "profile explain failed: " << error << "\n";
            return ExitError;
        }

        ProfileExplainOptions options;
        if (hasOption(args, "--target")) {
            const auto value = argValue(args, "--target");
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

        if (hasFlag(args, "--json")) {
            printJsonLine(explainProfileJson(profile, options));
        } else {
            std::cout << explainProfileText(profile, options);
        }
        return ExitOk;
    }

    std::cerr << "unknown profile command: " << cmd << "\n";
    return ExitUsage;
}

int doExportCommand(const std::vector<std::string>& args) {
    if (hasHelp(args)) {
        printExportUsage();
        return 0;
    }
    ensureDefaults();
    if (args.size() < 2) {
        printExportUsage();
        return 1;
    }
    const std::string target = args[1];
    if (target != "all" && target != "mihomo" && target != "sing-box" && target != "xray") {
        std::cerr << "unknown export target: " << target << "\n";
        return ExitUsage;
    }
    if (!validateOptions(
            args,
            2,
            {"--tun", "--check", "--check-timeout", "--output-dir", "--profile", "--sub", "--tag", "--strict-network", "--strict-capabilities", "--download-assets", "--explain-policy", "--json"},
            {"--check-timeout", "--output-dir", "--profile", "--sub", "--tag"}
        )) {
        return ExitUsage;
    }
    if (!ensureNoExtraPositionals(args, 2, {"--check-timeout", "--output-dir", "--profile", "--sub", "--tag"}, "export accepts only one target and options")) {
        return ExitUsage;
    }

    auto subs = loadSubscriptions(gPaths.subPath.string());
    auto cfg = loadConfig(gPaths.configPath.string());
    applyConfigDefaults(cfg);
    const bool tun = hasFlag(args, "--tun") ? true : cfg.tun;
    const bool strictNetwork = hasFlag(args, "--strict-network");
    const bool strictCapabilities = hasFlag(args, "--strict-capabilities");
    const bool jsonOutput = hasFlag(args, "--json");
    const bool downloadAssets = hasFlag(args, "--download-assets");
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
    if (hasOption(args, "--check-timeout") && !parseBoundedIntValue(argValue(args, "--check-timeout", "30"), "--check-timeout", 1, checkTimeoutSec)) {
        return 1;
    }
    std::string outputDir;
    if (hasOption(args, "--output-dir")) {
        outputDir = resolveFromCliCwd(argValue(args, "--output-dir", cfg.outputDir));
    } else {
        outputDir = cfg.outputDir;
    }
    const auto onlySubs = argValues(args, "--sub");
    const auto onlyTags = argValues(args, "--tag");
    std::string profileOverride;
    if (hasOption(args, "--profile")) {
        profileOverride = argValue(args, "--profile");
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
            for (const auto& item : assessProfileCapabilities(exportTarget, *exportProfile)) {
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
            for (const auto& item : assessProfileCapabilities(exportTarget, *exportProfile)) {
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
            for (const auto& item : assessProfileCapabilities(exportTarget, *exportProfile)) {
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
    if (hasFlag(args, "--explain-policy")) {
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

    if (hasFlag(args, "--check")) {
        int checkFailed = 0;
        if ((target == "all" || target == "mihomo") && mihomoOk) {
            checkFailed += runConfigCheckForTarget(cfg, ExportTarget::Mihomo, outputDir + "/mihomo.yaml", checkTimeoutSec);
        }
        if ((target == "all" || target == "sing-box") && singOk) {
            checkFailed += runConfigCheckForTarget(cfg, ExportTarget::SingBox, outputDir + "/sing-box.json", checkTimeoutSec);
        }
        if ((target == "all" || target == "xray") && xrayOk) {
            checkFailed += runConfigCheckForTarget(cfg, ExportTarget::Xray, outputDir + "/xray.json", checkTimeoutSec);
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

int doDaemonCommand(const std::vector<std::string>& args) {
    if (hasHelp(args)) {
        printDaemonUsage();
        return ExitOk;
    }
    if (args.size() < 2) {
        printDaemonUsage();
        return ExitUsage;
    }
    const std::string mode = args[1];
    if (mode != "once" && mode != "run" && mode != "start" && mode != "stop" && mode != "status") {
        std::cerr << "unknown daemon mode: " << mode << "\n";
        return ExitUsage;
    }

    if (mode == "status") {
        if (!validateOptions(args, 2, {}, {})) {
            return ExitUsage;
        }
        if (args.size() != 2) {
            printDaemonUsage();
            return ExitUsage;
        }
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
            std::cout << "daemon\trunning\tpid=" << status.pid << "\tinterval=" << status.options.intervalSec
                      << "\ttarget=" << status.options.exportTarget;
            if (!status.lastCycleMessage.empty()) {
                if (status.lastCycleExitCode == 0) {
                    std::cout << "\tlast=ok";
                } else {
                    std::cout << "\tlast=failed(" << status.lastCycleMessage << ")";
                }
            }
            std::cout << "\n";
        } else {
            std::cout << "daemon\tstale\tpid=" << status.pid;
            if (!status.lastCycleMessage.empty()) {
                if (status.lastCycleExitCode == 0) {
                    std::cout << "\tlast=ok";
                } else {
                    std::cout << "\tlast=failed(" << status.lastCycleMessage << ")";
                }
            }
            std::cout << "\n";
        }
        return ExitOk;
    }

    if (mode == "stop") {
        if (!validateOptions(args, 2, {}, {})) {
            return ExitUsage;
        }
        if (args.size() != 2) {
            printDaemonUsage();
            return ExitUsage;
        }
        std::string error;
        if (!stopDaemonProcess(gPaths.stateDir, 5, error)) {
            std::cerr << "daemon stop failed: " << error << "\n";
            return ExitError;
        }
        std::cout << "daemon stopped\n";
        return ExitOk;
    }

    if (!validateOptions(
            args,
            2,
            {"--interval", "--target", "--update-assets", "--strict-network", "--check", "--no-restart", "--pid-file", "--log-file"},
            {"--interval", "--target", "--pid-file", "--log-file"}
        )) {
        return ExitUsage;
    }
    if (!ensureNoExtraPositionals(args, 2, {"--interval", "--target", "--pid-file", "--log-file"}, "daemon accepts mode and options")) {
        return ExitUsage;
    }

    int intervalSec = 3600;
    if (hasOption(args, "--interval") && !parseBoundedIntValue(argValue(args, "--interval", "3600"), "--interval", 1, intervalSec)) {
        return ExitUsage;
    }
    const std::string target = argValue(args, "--target", "all");
    if (target != "all") {
        ExportTarget parsedTarget;
        std::string defaultFile;
        if (!parseExportTarget(target, parsedTarget, defaultFile)) {
            std::cerr << "invalid daemon target: " << target << "\n";
            return ExitUsage;
        }
    }

    DaemonOptions options;
    options.intervalSec = intervalSec;
    options.exportTarget = target;
    options.updateAssets = hasFlag(args, "--update-assets");
    options.strictNetwork = hasFlag(args, "--strict-network");
    options.check = hasFlag(args, "--check");
    options.restartRunning = !hasFlag(args, "--no-restart");
    options.pidFile = argValue(args, "--pid-file");
    options.logFile = argValue(args, "--log-file");

    DaemonCallbacks callbacks;
    callbacks.runSubCommand = [&](const std::vector<std::string>& subArgs) { return doSubCommand(subArgs); };
    callbacks.runExportCommand = [&](const std::vector<std::string>& exportArgs) { return doExportCommand(exportArgs); };
    callbacks.isCoreRunning = [&](const std::string& coreTarget, std::string& error) {
        const auto status = inspectCoreRuntime(gPaths.stateDir, coreTarget, error);
        return status.running;
    };
    callbacks.runRestartCommand = [&](const std::vector<std::string>& restartArgs) { return doRestartCommand(restartArgs); };

    if (mode == "start") {
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

    if (mode == "once") {
        const int rc = runDaemonCycleWithState(gPaths.stateDir, options, callbacks);
        if (rc == 0) {
            std::cout << "daemon cycle completed\n";
            return ExitOk;
        }
        std::cerr << "daemon cycle failed\n";
        return ExitError;
    }

    while (true) {
        const int rc = runDaemonCycleWithState(gPaths.stateDir, options, callbacks);
        if (rc != 0) {
            std::cerr << "daemon cycle failed with code " << rc << "\n";
        }
        std::this_thread::sleep_for(std::chrono::seconds(options.intervalSec));
    }
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

        std::vector<std::string> args;
        args.reserve(static_cast<size_t>(argc));
        for (int i = 0; i < argc; ++i) {
            args.emplace_back(argv[i]);
        }

        if (args.size() < 2) {
            printRootUsage();
            return 0;
        }

        const std::string cmd = args[1];
        if (cmd == "--help" || cmd == "-h" || cmd == "help") {
            printRootUsage();
            return 0;
        }
        std::vector<std::string> tail(args.begin() + 1, args.end());
        if (cmd == "sub") {
            return doSubCommand(tail);
        }
        if (cmd == "init") {
            return doInitCommand(tail);
        }
        if (cmd == "doctor") {
            return doDoctorCommand(tail);
        }
        if (cmd == "config") {
            return doConfigCommand(tail);
        }
        if (cmd == "template") {
            return doTemplateCommand(tail);
        }
        if (cmd == "asset") {
            return doAssetCommand(tail);
        }
        if (cmd == "profile") {
            return doProfileCommand(tail);
        }
        if (cmd == "export") {
            return doExportCommand(tail);
        }
        if (cmd == "daemon") {
            return doDaemonCommand(tail);
        }
        if (cmd == "run") {
            return doRunCommand(tail);
        }
        if (cmd == "stop") {
            return doStopCommand(tail);
        }
        if (cmd == "status") {
            return doStatusCommand(tail);
        }
        if (cmd == "restart") {
            return doRestartCommand(tail);
        }
        if (cmd == "check") {
            return doCheckCommand(tail);
        }
        if (cmd == "completion") {
            return doCompletionCommand(tail);
        }

        std::cerr << "unknown command: " << cmd << "\n";
        return ExitUsage;
    } catch (const std::exception& ex) {
        std::cerr << "error: " << ex.what() << "\n";
        return 1;
    } catch (...) {
        std::cerr << "error: unexpected failure\n";
        return 1;
    }
}
