#pragma once

#include <functional>
#include <string>
#include <vector>

#include "subcli/models.hpp"

namespace subcli {

struct ConfigEntryView {
    std::string key;
    std::string value;
};

struct ConfigServiceOptions {
    std::string defaultOutputDir;
    std::string defaultAssetDir;
    std::string defaultTemplateDir;
};

std::vector<ConfigEntryView> listConfigValues(const AppConfig& cfg);
bool getConfigValue(const AppConfig& cfg, const std::string& key, std::string& value, std::string& error);
bool setConfigValue(
    AppConfig& cfg,
    const std::string& key,
    const std::string& value,
    const std::function<std::string(const std::string&)>& resolvePath,
    std::string& error
);
bool removeConfigValue(AppConfig& cfg, const std::string& key, const ConfigServiceOptions& options, std::string& error);

} // namespace subcli
