#include "subcli/config_defaults.hpp"

#include <filesystem>
#include <fstream>
#include <map>
#include <system_error>

#include "subcli/environment.hpp"
#include "subcli/store.hpp"

namespace subcli {
namespace fs = std::filesystem;

namespace {

void fillCommonDefaults(AppConfig& config) {
    config.profile = "bypass-cn";
    config.tun = false;
    config.logLevel = "info";
    config.parallelism = 4;
    config.timeout = 15;
    config.retry = 2;
    config.fetchMaxBytes = 10 * 1024 * 1024;

    config.templateNormal["mihomo"] = "./templates/mihomo_base.yaml";
    config.templateTun["mihomo"] = "./templates/mihomo_tun.yaml";
    config.templateNormal["sing-box"] = "./templates/singbox_base.json";
    config.templateTun["sing-box"] = "./templates/singbox_tun.json";
    config.templateNormal["xray"] = "./templates/xray_base.json";
    config.templateTun["xray"] = "./templates/xray_tun.json";

    config.assetPaths["mihomo.geosite"] = "./data/assets/mihomo/geosite.dat";
    config.assetPaths["mihomo.geoip"] = "./data/assets/mihomo/geoip.dat";
    config.assetPaths["sing-box.geosite-cn"] = "./data/assets/sing-box/geosite-cn.srs";
    config.assetPaths["sing-box.geoip-cn"] = "./data/assets/sing-box/geoip-cn.srs";
    config.assetPaths["xray.geosite"] = "./data/assets/xray/geosite.dat";
    config.assetPaths["xray.geoip"] = "./data/assets/xray/geoip.dat";

    config.assetUrls["mihomo.geosite"] = "https://github.com/MetaCubeX/meta-rules-dat/releases/download/latest/geosite.dat";
    config.assetUrls["mihomo.geoip"] = "https://github.com/MetaCubeX/meta-rules-dat/releases/download/latest/geoip.dat";
    config.assetUrls["sing-box.geosite-cn"] = "https://github.com/SagerNet/sing-geosite/releases/latest/download/geosite-cn.srs";
    config.assetUrls["sing-box.geoip-cn"] = "https://github.com/SagerNet/sing-geoip/releases/latest/download/geoip-cn.srs";
    config.assetUrls["xray.geosite"] = "https://github.com/v2fly/domain-list-community/releases/latest/download/dlc.dat";
    config.assetUrls["xray.geoip"] = "https://github.com/v2fly/geoip/releases/latest/download/geoip.dat";

    config.regionRules = {
        {"HK", "(?i)(hong kong|hongkong|hk|香港)"},
        {"SG", "(?i)(singapore|sg|新加坡)"},
        {"JP", "(?i)(japan|jp|tokyo|osaka|日本)"},
        {"TW", "(?i)(taiwan|tw|台灣|台湾)"},
        {"US", "(?i)(united states|usa|us|america|美国)"},
    };
}

void fillPortablePaths(AppConfig& config) {
    config.dataDir = "./data";
    config.cacheDir = "./cache";
    config.assetDir = "./data/assets";
    config.templateDir = "./templates";
    config.profileDir = "./profiles";
    config.outputDir = "./outputs";
    config.stateDir = "./data/state";
    config.logDir = "./logs";
    config.subFile = "./data/sub.yaml";
}

void fillFhsPaths(AppConfig& config, PlatformKind platform) {
    if (platform == PlatformKind::MacOS) {
        config.dataDir = "/usr/local/var/lib/subcli";
        config.cacheDir = "/usr/local/var/cache/subcli";
        config.assetDir = "/usr/local/var/lib/subcli/assets";
        config.templateDir = "/usr/local/share/subcli/templates";
        config.profileDir = "/usr/local/share/subcli/profiles";
        config.outputDir = "/usr/local/var/lib/subcli/outputs";
        config.stateDir = "/usr/local/var/lib/subcli/state";
        config.logDir = "/usr/local/var/log/subcli";
        config.subFile = "/usr/local/var/lib/subcli/sub.yaml";
    } else {
        config.dataDir = "/var/lib/subcli";
        config.cacheDir = "/var/cache/subcli";
        config.assetDir = "/var/lib/subcli/assets";
        config.templateDir = "/usr/share/subcli/templates";
        config.profileDir = "/usr/share/subcli/profiles";
        config.outputDir = "/var/lib/subcli/outputs";
        config.stateDir = "/var/lib/subcli/state";
        config.logDir = "/var/log/subcli";
        config.subFile = "/var/lib/subcli/sub.yaml";
    }

    config.assetPaths["mihomo.geosite"] = config.assetDir + "/mihomo/geosite.dat";
    config.assetPaths["mihomo.geoip"] = config.assetDir + "/mihomo/geoip.dat";
    config.assetPaths["sing-box.geosite-cn"] = config.assetDir + "/sing-box/geosite-cn.srs";
    config.assetPaths["sing-box.geoip-cn"] = config.assetDir + "/sing-box/geoip-cn.srs";
    config.assetPaths["xray.geosite"] = config.assetDir + "/xray/geosite.dat";
    config.assetPaths["xray.geoip"] = config.assetDir + "/xray/geoip.dat";

    config.templateNormal["mihomo"] = config.templateDir + "/mihomo_base.yaml";
    config.templateTun["mihomo"] = config.templateDir + "/mihomo_tun.yaml";
    config.templateNormal["sing-box"] = config.templateDir + "/singbox_base.json";
    config.templateTun["sing-box"] = config.templateDir + "/singbox_tun.json";
    config.templateNormal["xray"] = config.templateDir + "/xray_base.json";
    config.templateTun["xray"] = config.templateDir + "/xray_tun.json";
}

} // namespace

AppConfig makeDefaultConfig(ConfigLayout layout, PlatformKind platform) {
    AppConfig config;
    fillCommonDefaults(config);
    if (layout == ConfigLayout::FHS) {
        fillFhsPaths(config, platform);
    } else {
        fillPortablePaths(config);
    }
    return config;
}

void resolveConfigPathsFromAppDir(AppConfig& config, const fs::path& appDir) {
    config.dataDir = resolvePathFromAppDir(appDir, config.dataDir).string();
    config.cacheDir = resolvePathFromAppDir(appDir, config.cacheDir).string();
    config.assetDir = resolvePathFromAppDir(appDir, config.assetDir).string();
    config.templateDir = resolvePathFromAppDir(appDir, config.templateDir).string();
    config.profileDir = resolvePathFromAppDir(appDir, config.profileDir).string();
    config.outputDir = resolvePathFromAppDir(appDir, config.outputDir).string();
    config.stateDir = resolvePathFromAppDir(appDir, config.stateDir).string();
    config.logDir = resolvePathFromAppDir(appDir, config.logDir).string();
    config.subFile = resolvePathFromAppDir(appDir, config.subFile).string();
    if (!config.profilePath.empty()) {
        config.profilePath = resolvePathFromAppDir(appDir, config.profilePath).string();
    }
    if (!config.mihomoPath.empty()) {
        config.mihomoPath = resolvePathFromAppDir(appDir, config.mihomoPath).string();
    }
    if (!config.singBoxPath.empty()) {
        config.singBoxPath = resolvePathFromAppDir(appDir, config.singBoxPath).string();
    }
    if (!config.xrayPath.empty()) {
        config.xrayPath = resolvePathFromAppDir(appDir, config.xrayPath).string();
    }
    for (auto& kv : config.assetPaths) {
        kv.second = resolvePathFromAppDir(appDir, kv.second).string();
    }
    for (auto& kv : config.templateNormal) {
        kv.second = resolvePathFromAppDir(appDir, kv.second).string();
    }
    for (auto& kv : config.templateTun) {
        kv.second = resolvePathFromAppDir(appDir, kv.second).string();
    }
}

bool ensureConfigRuntimeFiles(const AppConfig& config, std::string& error) {
    std::error_code ec;
    for (const std::string& dir : {config.dataDir, config.cacheDir, config.assetDir, config.outputDir, config.stateDir, config.logDir}) {
        if (!dir.empty()) {
            fs::create_directories(dir, ec);
            if (ec) {
                error = "failed to create directory: " + dir + ": " + ec.message();
                return false;
            }
        }
    }
    const fs::path subPath(config.subFile);
    fs::create_directories(subPath.parent_path(), ec);
    if (ec) {
        error = "failed to create subscription directory: " + subPath.parent_path().string() + ": " + ec.message();
        return false;
    }
    if (!fs::exists(subPath, ec)) {
        std::ofstream out(subPath);
        if (!out) {
            error = "failed to create subscription file: " + subPath.string();
            return false;
        }
        out << "version: 1\nsubscriptions: []\n";
    }
    return true;
}

bool writeInitialConfig(const fs::path& path, const AppConfig& config, bool force, std::string& error) {
    std::error_code ec;
    if (fs::exists(path, ec) && !force) {
        error = "config already exists: " + path.string();
        return false;
    }
    fs::create_directories(path.parent_path(), ec);
    if (ec) {
        error = "failed to create config directory: " + path.parent_path().string() + ": " + ec.message();
        return false;
    }
    try {
        saveConfig(path.string(), config);
        return true;
    } catch (const std::exception& ex) {
        error = ex.what();
        return false;
    }
}

} // namespace subcli
