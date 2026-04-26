#include "subcli/assets.hpp"

#include <algorithm>

#include "subcli/fetch.hpp"
#include "subcli/util.hpp"

namespace subcli {

std::vector<AssetRecord> configuredAssets(const AppConfig& config) {
    std::vector<AssetRecord> out;
    out.reserve(config.assetPaths.size());
    for (const auto& kv : config.assetPaths) {
        AssetRecord record;
        record.key = kv.first;
        record.path = kv.second;
        const auto url = config.assetUrls.find(kv.first);
        if (url != config.assetUrls.end()) {
            record.url = url->second;
        }
        record.exists = fileExists(record.path);
        out.push_back(record);
    }
    std::sort(out.begin(), out.end(), [](const AssetRecord& a, const AssetRecord& b) { return a.key < b.key; });
    return out;
}

bool updateAsset(const AssetRecord& asset, int timeoutSec, long maxBytes, std::string& error) {
    if (asset.url.empty()) {
        error = "asset has no download URL: " + asset.key;
        return false;
    }
    if (asset.path.empty()) {
        error = "asset has no output path: " + asset.key;
        return false;
    }

    Subscription request;
    request.id = asset.key;
    request.name = asset.key;
    request.url = asset.url;
    request.timeout = std::max(1, timeoutSec);
    request.timeoutOverride = true;
    request.retry = 1;
    request.retryOverride = true;
    request.fetchMaxBytes = std::max<long>(1024, maxBytes);

    const auto fetched = fetchSubscription(request, true);
    if (!fetched.ok) {
        error = fetched.error;
        return false;
    }
    return writeFile(asset.path, fetched.content, error);
}

} // namespace subcli
