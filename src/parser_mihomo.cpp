#include "parser_internal.hpp"

#include <filesystem>

#include "subcli/fetch.hpp"
#include "subcli/util.hpp"

namespace subcli {

namespace {

void appendMihomoProxyNodes(const YAML::Node& proxies, const std::string& sourceId, const AppConfig& config, ParseResult& result) {
    if (!proxies || !proxies.IsSequence()) {
        return;
    }
    for (const auto& n : proxies) {
        auto proxy = fromMihomoProxy(n, sourceId, config);
        if (!proxy.server.empty() && proxy.port > 0) {
            result.nodes.push_back(proxy);
        } else {
            ++result.skipped;
        }
    }
}

void appendProviderContent(const std::string& content, const std::string& sourceId, const AppConfig& config, ParseResult& result) {
    const auto providerRoot = YAML::Load(content);
    appendMihomoProxyNodes(providerRoot["proxies"], sourceId, config, result);
}

void appendProviderNodes(const YAML::Node& providers, const std::string& sourceId, const AppConfig& config, ParseResult& result) {
    if (!providers || !providers.IsMap()) {
        return;
    }
    for (const auto& item : providers) {
        const auto provider = item.second;
        const auto type = provider["type"].as<std::string>("");
        try {
            if (type == "file") {
                const auto path = provider["path"].as<std::string>("");
                if (path.empty() || !std::filesystem::exists(path)) {
                    ++result.skipped;
                    continue;
                }
                appendProviderContent(readFile(path), sourceId, config, result);
                continue;
            }
            if (type == "http") {
                Subscription sub;
                sub.id = sourceId + ":" + item.first.as<std::string>("provider");
                sub.name = sub.id;
                sub.url = provider["url"].as<std::string>("");
                sub.timeout = config.timeout;
                sub.timeoutOverride = true;
                sub.retry = 0;
                sub.retryOverride = true;
                sub.fetchMaxBytes = config.fetchMaxBytes;
                const auto fetched = fetchSubscription(sub, false);
                if (!fetched.ok) {
                    ++result.skipped;
                    continue;
                }
                appendProviderContent(fetched.content, sourceId, config, result);
            }
        } catch (...) {
            ++result.skipped;
        }
    }
}

} // namespace

ParseResult parseMihomoSubscription(const std::string& content, const std::string& sourceId, const AppConfig& config) {
    ParseResult result;
    try {
        YAML::Node y = YAML::Load(content);
        if (!y.IsMap()) {
            return result;
        }
        appendMihomoProxyNodes(y["proxies"], sourceId, config, result);
        appendProviderNodes(y["proxy-providers"], sourceId, config, result);
    } catch (...) {
    }
    return result;
}

} // namespace subcli
