#include "subcli/assets.hpp"

#include <algorithm>
#include <filesystem>

#include <nlohmann/json.hpp>

#include "subcli/fetch.hpp"
#include "subcli/util.hpp"

namespace subcli {

namespace {

std::filesystem::path assetMetadataPath(const std::string& assetPath) {
    return std::filesystem::path(assetPath + ".meta.json");
}

void fillAssetMetadata(AssetRecord& record) {
    const auto metaPath = assetMetadataPath(record.path);
    if (!fileExists(metaPath.string())) {
        return;
    }
    const auto parsed = nlohmann::json::parse(readFile(metaPath.string()), nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) {
        return;
    }
    record.updatedAt = parsed.value("updated_at", "");
    record.sourceUrl = parsed.value("source_url", "");
    record.sizeBytes = parsed.value("size_bytes", record.sizeBytes);
}

bool writeAssetMetadata(const AssetRecord& asset, long sizeBytes, std::string& error) {
    const nlohmann::json meta = {
        {"updated_at", nowIso8601()},
        {"source_url", asset.url},
        {"size_bytes", sizeBytes},
    };
    return writeFile(assetMetadataPath(asset.path).string(), meta.dump(2), error);
}

bool writeAssetAtomically(const std::string& path, const std::string& content, std::string& error) {
    const std::filesystem::path target(path);
    const std::filesystem::path tmp(path + ".tmp");
    if (!writeFile(tmp.string(), content, error)) {
        return false;
    }

    std::error_code ec;
    std::filesystem::rename(tmp, target, ec);
    if (!ec) {
        return true;
    }

    std::error_code cleanupEc;
    std::filesystem::remove(tmp, cleanupEc);
    error = "failed to replace asset file: " + ec.message();
    return false;
}

} // namespace

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
        record.sourceUrl = record.url;
        if (record.exists) {
            std::error_code ec;
            record.sizeBytes = static_cast<long>(std::filesystem::file_size(record.path, ec));
            if (ec) {
                record.sizeBytes = -1;
            }
        }
        fillAssetMetadata(record);
        out.push_back(record);
    }
    std::sort(out.begin(), out.end(), [](const AssetRecord& a, const AssetRecord& b) { return a.key < b.key; });
    return out;
}

std::vector<AssetRecord> missingAssets(const AppConfig& config) {
    std::vector<AssetRecord> out;
    for (const auto& asset : configuredAssets(config)) {
        if (!asset.exists) {
            out.push_back(asset);
        }
    }
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
    if (!writeAssetAtomically(asset.path, fetched.content, error)) {
        return false;
    }
    if (!writeAssetMetadata(asset, static_cast<long>(fetched.content.size()), error)) {
        return false;
    }
    return true;
}

} // namespace subcli
