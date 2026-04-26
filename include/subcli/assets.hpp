#pragma once

#include <string>
#include <vector>

#include "subcli/models.hpp"

namespace subcli {

struct AssetRecord {
    std::string key;
    std::string path;
    std::string url;
    bool exists = false;
};

std::vector<AssetRecord> configuredAssets(const AppConfig& config);
std::vector<AssetRecord> missingAssets(const AppConfig& config);
bool updateAsset(const AssetRecord& asset, int timeoutSec, long maxBytes, std::string& error);

} // namespace subcli
