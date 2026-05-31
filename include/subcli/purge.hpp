#pragma once

#include <string>
#include <vector>

#include "subcli/models.hpp"

namespace subcli {

struct PurgeOptions {
    bool assets = false;
    bool cache = false;
    bool outputs = false;
    bool state = false;
    bool logs = false;
    bool config = false;
    bool all = false;
    bool dryRun = false;
    bool yes = false;
};

struct PurgePlan {
    std::vector<std::string> paths;
};

struct PurgeResult {
    bool ok = false;
    std::vector<std::string> removed;
    std::vector<std::string> skipped;
    std::string error;
};

PurgePlan planPurge(const AppConfig& config, const std::string& configPath, const PurgeOptions& options);
PurgeResult executePurge(const AppConfig& config, const std::string& configPath, const PurgeOptions& options);

} // namespace subcli
