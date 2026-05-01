#include "subcli/config_service.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <map>

#include "subcli/registry.hpp"

namespace subcli {

namespace {

bool parseInt(const std::string& raw, const std::string& key, int minValue, int& out, std::string& error) {
    try {
        size_t consumed = 0;
        const int value = std::stoi(raw, &consumed);
        if (consumed != raw.size()) {
            error = "invalid integer for " + key + ": " + raw;
            return false;
        }
        if (value < minValue) {
            error = key + " must be >= " + std::to_string(minValue);
            return false;
        }
        out = value;
        return true;
    } catch (...) {
        error = "invalid integer for " + key + ": " + raw;
        return false;
    }
}

bool parseLong(const std::string& raw, const std::string& key, long minValue, long& out, std::string& error) {
    try {
        size_t consumed = 0;
        const long value = std::stol(raw, &consumed);
        if (consumed != raw.size()) {
            error = "invalid integer for " + key + ": " + raw;
            return false;
        }
        if (value < minValue) {
            error = key + " must be >= " + std::to_string(minValue);
            return false;
        }
        out = value;
        return true;
    } catch (...) {
        error = "invalid integer for " + key + ": " + raw;
        return false;
    }
}

bool parseBool(const std::string& raw, const std::string& key, bool& out, std::string& error) {
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
    error = "invalid boolean for " + key + ": " + raw;
    return false;
}

bool isSupportedProfile(const std::string& value) {
    return value == "bypass-cn" || value == "global" || value == "direct" || value == "custom";
}

bool parseTemplateTargetKind(const std::string& target, const std::string& kind, std::string& error) {
    if (target != "mihomo" && target != "sing-box" && target != "xray") {
        error = "invalid template target: " + target;
        return false;
    }
    if (kind != "normal" && kind != "tun") {
        error = "invalid template kind: " + kind;
        return false;
    }
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

std::string defaultTemplatePath(const std::string& dir, const std::string& filename) {
    return (std::filesystem::path(dir) / filename).string();
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

} // namespace

std::vector<ConfigEntryView> listConfigValues(const AppConfig& cfg) {
    std::vector<ConfigEntryView> entries;
    entries.push_back({"tun", cfg.tun ? "true" : "false"});
    entries.push_back({"profile", cfg.profile});
    entries.push_back({"profile_path", cfg.profilePath});
    entries.push_back({"output_dir", cfg.outputDir});
    entries.push_back({"template_dir", cfg.templateDir});
    entries.push_back({"asset_dir", cfg.assetDir});
    entries.push_back({"parallelism", std::to_string(cfg.parallelism)});
    entries.push_back({"timeout", std::to_string(cfg.timeout)});
    entries.push_back({"retry", std::to_string(cfg.retry)});
    entries.push_back({"fetch_max_bytes", std::to_string(cfg.fetchMaxBytes)});
    entries.push_back({"log_level", cfg.logLevel});
    entries.push_back({"core_paths.mihomo", cfg.mihomoPath});
    entries.push_back({"core_paths.sing_box", cfg.singBoxPath});
    entries.push_back({"core_paths.xray", cfg.xrayPath});
    entries.push_back({"node_management.dedupe", cfg.dedupeNodes ? "true" : "false"});
    entries.push_back({"node_management.rename_template", cfg.renameTemplate});
    entries.push_back({"node_management.include_regex", cfg.includeRegex});
    entries.push_back({"node_management.exclude_regex", cfg.excludeRegex});
    entries.push_back({"node_management.sort_by", cfg.sortBy});
    for (const auto& kv : cfg.templateNormal) {
        entries.push_back({"templates." + kv.first + ".normal", kv.second});
    }
    for (const auto& kv : cfg.templateTun) {
        entries.push_back({"templates." + kv.first + ".tun", kv.second});
    }
    for (const auto& kv : cfg.regionRules) {
        entries.push_back({"grouping.region_rules." + kv.first, kv.second});
    }
    for (const auto& kv : cfg.assetPaths) {
        entries.push_back({"assets.paths." + kv.first, kv.second});
    }
    for (const auto& kv : cfg.assetUrls) {
        entries.push_back({"assets.urls." + kv.first, kv.second});
    }
    return entries;
}

bool getConfigValue(const AppConfig& cfg, const std::string& key, std::string& value, std::string& error) {
    value.clear();
    error.clear();
    if (!findConfigKeyDescriptor(key)) {
        error = "unsupported key in v1";
        return false;
    }
    if (key == "tun") {
        value = cfg.tun ? "true" : "false";
        return true;
    }
    if (key == "output_dir") { value = cfg.outputDir; return true; }
    if (key == "profile") { value = cfg.profile; return true; }
    if (key == "profile_path") { value = cfg.profilePath; return true; }
    if (key == "asset_dir") { value = cfg.assetDir; return true; }
    if (key == "template_dir") { value = cfg.templateDir; return true; }
    if (key == "parallelism") { value = std::to_string(cfg.parallelism); return true; }
    if (key == "timeout") { value = std::to_string(cfg.timeout); return true; }
    if (key == "retry") { value = std::to_string(cfg.retry); return true; }
    if (key == "fetch_max_bytes") { value = std::to_string(cfg.fetchMaxBytes); return true; }
    if (key == "log_level") { value = cfg.logLevel; return true; }
    if (key == "core_paths.mihomo") { value = cfg.mihomoPath; return true; }
    if (key == "core_paths.sing_box") { value = cfg.singBoxPath; return true; }
    if (key == "core_paths.xray") { value = cfg.xrayPath; return true; }
    if (key == "node_management.dedupe") { value = cfg.dedupeNodes ? "true" : "false"; return true; }
    if (key == "node_management.rename_template") { value = cfg.renameTemplate; return true; }
    if (key == "node_management.include_regex") { value = cfg.includeRegex; return true; }
    if (key == "node_management.exclude_regex") { value = cfg.excludeRegex; return true; }
    if (key == "node_management.sort_by") { value = cfg.sortBy; return true; }

    if (key.rfind("templates.", 0) == 0) {
        const auto rest = key.substr(10);
        const auto dot = rest.find('.');
        if (dot == std::string::npos) {
            error = "invalid template key";
            return false;
        }
        const auto target = rest.substr(0, dot);
        const auto kind = rest.substr(dot + 1);
        std::string parseError;
        if (!parseTemplateTargetKind(target, kind, parseError)) {
            error = parseError;
            return false;
        }
        if (kind == "normal") {
            auto it = cfg.templateNormal.find(target);
            if (it != cfg.templateNormal.end()) {
                value = it->second;
                return true;
            }
        } else {
            auto it = cfg.templateTun.find(target);
            if (it != cfg.templateTun.end()) {
                value = it->second;
                return true;
            }
        }
        error = "unsupported key in v1";
        return false;
    }
    if (key.rfind("grouping.region_rules.", 0) == 0) {
        const auto region = key.substr(22);
        auto it = cfg.regionRules.find(region);
        if (it != cfg.regionRules.end()) {
            value = it->second;
            return true;
        }
        error = "unsupported key in v1";
        return false;
    }
    if (key.rfind("assets.paths.", 0) == 0) {
        const auto name = key.substr(13);
        auto it = cfg.assetPaths.find(name);
        if (it != cfg.assetPaths.end()) {
            value = it->second;
            return true;
        }
        error = "unsupported key in v1";
        return false;
    }
    if (key.rfind("assets.urls.", 0) == 0) {
        const auto name = key.substr(12);
        auto it = cfg.assetUrls.find(name);
        if (it != cfg.assetUrls.end()) {
            value = it->second;
            return true;
        }
        error = "unsupported key in v1";
        return false;
    }

    error = "unsupported key in v1";
    return false;
}

bool setConfigValue(
    AppConfig& cfg,
    const std::string& key,
    const std::string& value,
    const std::function<std::string(const std::string&)>& resolvePath,
    std::string& error
) {
    error.clear();
    if (!findConfigKeyDescriptor(key)) {
        error = "unsupported key in v1";
        return false;
    }

    if (key == "tun") {
        return parseBool(value, key, cfg.tun, error);
    }
    if (key == "output_dir") {
        cfg.outputDir = resolvePath(value);
        return true;
    }
    if (key == "profile") {
        if (!isSupportedProfile(value)) {
            error = "unsupported profile: " + value;
            return false;
        }
        cfg.profile = value;
        return true;
    }
    if (key == "profile_path") {
        cfg.profilePath = resolvePath(value);
        return true;
    }
    if (key == "asset_dir") {
        cfg.assetDir = resolvePath(value);
        return true;
    }
    if (key == "template_dir") {
        const std::string oldDir = cfg.templateDir;
        cfg.templateDir = resolvePath(value);
        updateTemplateDirDefaults(cfg, oldDir);
        return true;
    }
    if (key == "parallelism") {
        return parseInt(value, key, 1, cfg.parallelism, error);
    }
    if (key == "timeout") {
        return parseInt(value, key, 1, cfg.timeout, error);
    }
    if (key == "retry") {
        return parseInt(value, key, 0, cfg.retry, error);
    }
    if (key == "fetch_max_bytes") {
        return parseLong(value, key, 1024, cfg.fetchMaxBytes, error);
    }
    if (key == "log_level") {
        cfg.logLevel = value;
        return true;
    }
    if (key == "core_paths.mihomo") {
        cfg.mihomoPath = resolvePath(value);
        return true;
    }
    if (key == "core_paths.sing_box") {
        cfg.singBoxPath = resolvePath(value);
        return true;
    }
    if (key == "core_paths.xray") {
        cfg.xrayPath = resolvePath(value);
        return true;
    }
    if (key == "node_management.dedupe") {
        return parseBool(value, key, cfg.dedupeNodes, error);
    }
    if (key == "node_management.rename_template") {
        cfg.renameTemplate = value;
        return true;
    }
    if (key == "node_management.include_regex") {
        cfg.includeRegex = value;
        return true;
    }
    if (key == "node_management.exclude_regex") {
        cfg.excludeRegex = value;
        return true;
    }
    if (key == "node_management.sort_by") {
        cfg.sortBy = value;
        return true;
    }
    if (key.rfind("templates.", 0) == 0) {
        const auto rest = key.substr(10);
        const auto dot = rest.find('.');
        if (dot == std::string::npos) {
            error = "invalid template key";
            return false;
        }
        const auto target = rest.substr(0, dot);
        const auto kind = rest.substr(dot + 1);
        if (!parseTemplateTargetKind(target, kind, error)) {
            return false;
        }
        if (kind == "normal") {
            cfg.templateNormal[target] = value;
        } else {
            cfg.templateTun[target] = value;
        }
        return true;
    }
    if (key.rfind("grouping.region_rules.", 0) == 0) {
        const auto region = key.substr(22);
        if (region.empty()) {
            error = "invalid region rule key";
            return false;
        }
        cfg.regionRules[region] = value;
        return true;
    }
    if (key.rfind("assets.paths.", 0) == 0) {
        const auto assetKey = key.substr(13);
        if (assetKey.empty()) {
            error = "invalid asset path key";
            return false;
        }
        cfg.assetPaths[assetKey] = value;
        return true;
    }
    if (key.rfind("assets.urls.", 0) == 0) {
        const auto assetKey = key.substr(12);
        if (assetKey.empty()) {
            error = "invalid asset URL key";
            return false;
        }
        cfg.assetUrls[assetKey] = value;
        return true;
    }

    error = "unsupported key in v1";
    return false;
}

bool removeConfigValue(AppConfig& cfg, const std::string& key, const ConfigServiceOptions& options, std::string& error) {
    error.clear();
    if (!findConfigKeyDescriptor(key)) {
        error = "unsupported key in v1";
        return false;
    }

    if (key == "output_dir") { cfg.outputDir = options.defaultOutputDir; return true; }
    if (key == "profile") { cfg.profile = "bypass-cn"; return true; }
    if (key == "profile_path") { cfg.profilePath.clear(); return true; }
    if (key == "asset_dir") { cfg.assetDir = options.defaultAssetDir; return true; }
    if (key == "template_dir") {
        const std::string oldDir = cfg.templateDir;
        cfg.templateDir = options.defaultTemplateDir;
        updateTemplateDirDefaults(cfg, oldDir);
        return true;
    }
    if (key == "tun") { cfg.tun = false; return true; }
    if (key == "parallelism") { cfg.parallelism = 4; return true; }
    if (key == "timeout") { cfg.timeout = 15; return true; }
    if (key == "retry") { cfg.retry = 2; return true; }
    if (key == "fetch_max_bytes") { cfg.fetchMaxBytes = 10 * 1024 * 1024; return true; }
    if (key == "log_level") { cfg.logLevel = "info"; return true; }
    if (key == "core_paths.mihomo") { cfg.mihomoPath.clear(); return true; }
    if (key == "core_paths.sing_box") { cfg.singBoxPath.clear(); return true; }
    if (key == "core_paths.xray") { cfg.xrayPath.clear(); return true; }
    if (key == "node_management.dedupe") { cfg.dedupeNodes = true; return true; }
    if (key == "node_management.rename_template") { cfg.renameTemplate = "{name}"; return true; }
    if (key == "node_management.include_regex") { cfg.includeRegex.clear(); return true; }
    if (key == "node_management.exclude_regex") { cfg.excludeRegex.clear(); return true; }
    if (key == "node_management.sort_by") { cfg.sortBy = "region,name"; return true; }

    if (key.rfind("templates.", 0) == 0) {
        const auto rest = key.substr(10);
        const auto dot = rest.find('.');
        if (dot == std::string::npos) {
            error = "invalid template key";
            return false;
        }
        const auto target = rest.substr(0, dot);
        const auto kind = rest.substr(dot + 1);
        if (!parseTemplateTargetKind(target, kind, error)) {
            return false;
        }
        if (kind == "normal") {
            cfg.templateNormal.erase(target);
        } else {
            cfg.templateTun.erase(target);
        }
        return true;
    }
    if (key.rfind("grouping.region_rules.", 0) == 0) {
        cfg.regionRules.erase(key.substr(22));
        return true;
    }
    if (key.rfind("assets.paths.", 0) == 0) {
        cfg.assetPaths.erase(key.substr(13));
        return true;
    }
    if (key.rfind("assets.urls.", 0) == 0) {
        cfg.assetUrls.erase(key.substr(12));
        return true;
    }

    error = "unsupported key in v1";
    return false;
}

} // namespace subcli
