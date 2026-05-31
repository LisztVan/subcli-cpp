#include "subcli/purge.hpp"

#include <algorithm>
#include <filesystem>
#include <set>
#include <system_error>

namespace subcli {
namespace fs = std::filesystem;

namespace {

std::string normalized(const fs::path& path) {
    std::error_code ec;
    fs::path absolute = fs::absolute(path, ec);
    if (ec) {
        return path.lexically_normal().string();
    }
    return absolute.lexically_normal().string();
}

bool isDangerousPath(const std::string& raw) {
    if (raw.empty()) {
        return true;
    }
    const fs::path path(raw);
    const fs::path norm = fs::path(normalized(path));
    if (norm == norm.root_path()) {
        return true;
    }
#ifdef _WIN32
    if (norm.has_root_name() && norm == norm.root_name() / norm.root_directory()) {
        return true;
    }
#endif
    return false;
}

void addPath(std::set<std::string>& paths, const std::string& path) {
    if (!path.empty()) {
        paths.insert(normalized(path));
    }
}

} // namespace

PurgePlan planPurge(const AppConfig& config, const std::string& configPath, const PurgeOptions& options) {
    std::set<std::string> paths;
    const bool all = options.all;

    if (all || options.assets) {
        addPath(paths, config.assetDir);
        for (const auto& kv : config.assetPaths) {
            addPath(paths, kv.second);
            addPath(paths, kv.second + ".meta.json");
        }
    }
    if (all || options.cache) {
        addPath(paths, config.cacheDir);
    }
    if (all || options.outputs) {
        addPath(paths, config.outputDir);
    }
    if (all || options.state) {
        addPath(paths, config.stateDir);
    }
    if (all || options.logs) {
        addPath(paths, config.logDir);
    }
    if (all || options.config) {
        addPath(paths, configPath);
    }

    PurgePlan plan;
    plan.paths.assign(paths.begin(), paths.end());
    return plan;
}

PurgeResult executePurge(const AppConfig& config, const std::string& configPath, const PurgeOptions& options) {
    PurgeResult result;
    if (!options.assets && !options.cache && !options.outputs && !options.state && !options.logs && !options.config && !options.all) {
        result.error = "purge requires at least one target: --assets, --cache, --outputs, --state, --logs, --config, or --all";
        return result;
    }
    const auto plan = planPurge(config, configPath, options);
    for (const auto& path : plan.paths) {
        if (isDangerousPath(path)) {
            result.error = "refusing to remove dangerous path: " + path;
            return result;
        }
    }
    if (options.dryRun) {
        result.ok = true;
        result.skipped = plan.paths;
        return result;
    }
    if ((options.all || options.config) && !options.yes) {
        result.error = "purge --config or --all requires --yes";
        return result;
    }

    for (const auto& path : plan.paths) {
        std::error_code ec;
        if (!fs::exists(path, ec)) {
            result.skipped.push_back(path);
            continue;
        }
        fs::remove_all(path, ec);
        if (ec) {
            result.error = "failed to remove " + path + ": " + ec.message();
            return result;
        }
        result.removed.push_back(path);
    }
    result.ok = true;
    return result;
}

} // namespace subcli
