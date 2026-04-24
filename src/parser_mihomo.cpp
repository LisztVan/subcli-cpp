#include "parser_internal.hpp"

namespace subcli {

ParseResult parseMihomoSubscription(const std::string& content, const std::string& sourceId, const AppConfig& config) {
    ParseResult result;
    try {
        YAML::Node y = YAML::Load(content);
        if (!(y.IsMap() && y["proxies"] && y["proxies"].IsSequence())) {
            return result;
        }
        for (const auto& n : y["proxies"]) {
            auto proxy = fromMihomoProxy(n, sourceId, config);
            if (!proxy.server.empty() && proxy.port > 0) {
                result.nodes.push_back(proxy);
            } else {
                ++result.skipped;
            }
        }
    } catch (...) {
    }
    return result;
}

} // namespace subcli
