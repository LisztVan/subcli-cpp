#include "parser_internal.hpp"

#include <filesystem>

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

void appendLocalProviderNodes(const YAML::Node& providers, const std::string& sourceId, const AppConfig& config, ParseResult& result) {
    if (!providers || !providers.IsMap()) {
        return;
    }
    for (const auto& item : providers) {
        const auto provider = item.second;
        const auto type = provider["type"].as<std::string>("");
        if (type != "file") {
            continue;
        }
        const auto path = provider["path"].as<std::string>("");
        if (path.empty() || !std::filesystem::exists(path)) {
            ++result.skipped;
            continue;
        }
        try {
            const auto providerRoot = YAML::Load(readFile(path));
            appendMihomoProxyNodes(providerRoot["proxies"], sourceId, config, result);
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
        appendLocalProviderNodes(y["proxy-providers"], sourceId, config, result);
    } catch (...) {
    }
    return result;
}

} // namespace subcli
