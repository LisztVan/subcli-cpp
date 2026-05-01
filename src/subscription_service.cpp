#include "subcli/subscription_service.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

#include <yaml-cpp/yaml.h>

#include "subcli/tag_utils.hpp"
#include "subcli/util.hpp"

namespace subcli {

namespace {

std::string trim(const std::string& input) {
    size_t start = 0;
    while (start < input.size() && std::isspace(static_cast<unsigned char>(input[start]))) {
        ++start;
    }
    size_t end = input.size();
    while (end > start && std::isspace(static_cast<unsigned char>(input[end - 1]))) {
        --end;
    }
    return input.substr(start, end - start);
}

Subscription parseSubscriptionFromYaml(const YAML::Node& n) {
    Subscription s;
    s.id = n["id"].as<std::string>("");
    s.name = n["name"].as<std::string>("");
    s.url = n["url"].as<std::string>("");
    s.enabled = n["enabled"].as<bool>(true);
    s.group = n["group"].as<std::string>("default");
    s.formatHint = n["format_hint"].as<std::string>("auto");
    s.userAgent = n["user_agent"].as<std::string>("");
    s.timeout = n["timeout"].as<int>(15);
    s.timeoutOverride = n["timeout_override"].as<bool>(s.timeout != 15);
    s.retry = n["retry"].as<int>(2);
    s.retryOverride = n["retry_override"].as<bool>(s.retry != 2);
    s.updateInterval = n["update_interval"].as<int>(3600);
    s.lastUpdated = n["last_updated"].as<std::string>("");
    s.lastSuccess = n["last_success"].as<std::string>("");
    s.lastError = n["last_error"].as<std::string>("");
    s.etag = n["etag"].as<std::string>("");
    s.lastModified = n["last_modified"].as<std::string>("");
    s.cachePath = n["cache_path"].as<std::string>("");
    s.priority = n["priority"].as<int>(100);
    if (n["tags"] && n["tags"].IsSequence()) {
        for (const auto& tag : n["tags"]) {
            s.tags.push_back(tag.as<std::string>());
        }
        s.tags = normalizeTags(s.tags);
    }
    if (n["headers"] && n["headers"].IsMap()) {
        for (const auto& kv : n["headers"]) {
            s.headers[kv.first.as<std::string>()] = kv.second.as<std::string>();
        }
    }
    if (s.id.empty()) {
        s.id = makeIdFromName(s.name);
    }
    if (s.cachePath.empty()) {
        s.cachePath = "subscriptions/" + s.id + ".cache";
    }
    return s;
}

YAML::Node toYaml(const Subscription& s) {
    YAML::Node n;
    n["id"] = s.id;
    n["name"] = s.name;
    n["url"] = s.url;
    n["enabled"] = s.enabled;
    n["group"] = s.group;
    n["tags"] = YAML::Node(YAML::NodeType::Sequence);
    for (const auto& t : s.tags) {
        n["tags"].push_back(t);
    }
    n["format_hint"] = s.formatHint;
    n["user_agent"] = s.userAgent;
    n["timeout"] = s.timeout;
    n["timeout_override"] = s.timeoutOverride;
    n["retry"] = s.retry;
    n["retry_override"] = s.retryOverride;
    n["headers"] = YAML::Node(YAML::NodeType::Map);
    for (const auto& kv : s.headers) {
        n["headers"][kv.first] = kv.second;
    }
    n["update_interval"] = s.updateInterval;
    n["last_updated"] = s.lastUpdated;
    n["last_success"] = s.lastSuccess;
    n["last_error"] = s.lastError;
    n["etag"] = s.etag;
    n["last_modified"] = s.lastModified;
    n["cache_path"] = s.cachePath;
    n["priority"] = s.priority;
    return n;
}

} // namespace

std::string exportSubscriptionsToYaml(const std::vector<Subscription>& subscriptions) {
    YAML::Node root;
    root["version"] = 1;
    root["subscriptions"] = YAML::Node(YAML::NodeType::Sequence);
    for (const auto& sub : subscriptions) {
        root["subscriptions"].push_back(toYaml(sub));
    }
    YAML::Emitter out;
    out << root;
    return out.c_str();
}

SubscriptionImportResult mergeImportedSubscriptions(
    const std::vector<Subscription>& existing,
    const std::vector<Subscription>& imported,
    SubscriptionImportMode mode
) {
    SubscriptionImportResult result;
    if (mode == SubscriptionImportMode::Replace) {
        result.created = static_cast<int>(imported.size());
        result.subscriptions = imported;
        return result;
    }

    result.subscriptions = existing;
    for (auto importedSub : imported) {
        auto it = result.subscriptions.end();
        if (!importedSub.id.empty()) {
            it = std::find_if(result.subscriptions.begin(), result.subscriptions.end(), [&](const Subscription& s) {
                return !s.id.empty() && s.id == importedSub.id;
            });
        }
        if (it == result.subscriptions.end()) {
            it = std::find_if(result.subscriptions.begin(), result.subscriptions.end(), [&](const Subscription& s) {
                return s.name == importedSub.name;
            });
        }
        if (it == result.subscriptions.end()) {
            result.subscriptions.push_back(importedSub);
            ++result.created;
            continue;
        }
        if (importedSub.id.empty()) {
            importedSub.id = it->id;
        }
        if (importedSub.cachePath.empty()) {
            importedSub.cachePath = it->cachePath;
        }
        *it = importedSub;
        ++result.updated;
    }
    return result;
}

SubscriptionImportResult importSubscriptionsFromYaml(
    const std::string& yamlContent,
    const std::vector<Subscription>& existing,
    SubscriptionImportMode mode
) {
    SubscriptionImportResult result;
    YAML::Node root;
    try {
        root = YAML::Load(yamlContent);
    } catch (const std::exception& ex) {
        result.rejected = 1;
        result.messages.push_back(std::string("yaml parse failed: ") + ex.what());
        return result;
    }

    if (!root["subscriptions"] || !root["subscriptions"].IsSequence()) {
        result.rejected = 1;
        result.messages.push_back("missing required subscription field: subscriptions");
        return result;
    }

    std::vector<Subscription> imported;
    for (const auto& item : root["subscriptions"]) {
        const std::string name = item["name"].as<std::string>("");
        const std::string url = item["url"].as<std::string>("");
        if (name.empty() || url.empty()) {
            result.rejected = static_cast<int>(root["subscriptions"].size());
            result.messages.push_back("missing required subscription field");
            return result;
        }
        imported.push_back(parseSubscriptionFromYaml(item));
    }

    return mergeImportedSubscriptions(existing, imported, mode);
}

SubscriptionImportResult importSubscriptionsFromUriList(
    const std::string& uriListContent,
    const std::vector<Subscription>& existing,
    SubscriptionImportMode mode
) {
    std::vector<Subscription> imported;
    std::istringstream input(uriListContent);
    std::string line;
    int index = 0;
    while (std::getline(input, line)) {
        const auto trimmed = trim(line);
        if (trimmed.empty()) {
            continue;
        }
        ++index;
        Subscription sub;
        sub.name = "imported-" + std::to_string(index);
        sub.id = makeIdFromName(sub.name);
        sub.url = trimmed;
        sub.cachePath = "subscriptions/" + sub.id + ".cache";
        imported.push_back(sub);
    }
    return mergeImportedSubscriptions(existing, imported, mode);
}

} // namespace subcli
