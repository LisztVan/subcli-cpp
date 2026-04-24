#include "parser_internal.hpp"

#include <sstream>

namespace subcli {

ParseResult parseUriSubscription(const std::string& content, const std::string& sourceId, const AppConfig& config) {
    ParseResult result;
    std::string raw = content;
    if (content.find("://") == std::string::npos) {
        std::string maybe = decodeUrlSafeB64(content);
        if (maybe.find("://") != std::string::npos) {
            raw = maybe;
        }
    }

    std::stringstream ss(raw);
    std::string line;
    while (std::getline(ss, line)) {
        line = trim(line);
        if (line.empty()) {
            continue;
        }
        if (line.rfind("vmess://", 0) == 0 || line.rfind("vless://", 0) == 0 ||
            line.rfind("trojan://", 0) == 0 || line.rfind("ss://", 0) == 0) {
            try {
                auto p = fromUri(line, sourceId, config);
                if (!p.type.empty() && !p.server.empty() && p.port > 0) {
                    result.nodes.push_back(p);
                } else {
                    ++result.skipped;
                }
            } catch (...) {
                ++result.skipped;
                result.warnings.push_back({"invalid_uri_node", "invalid uri node skipped"});
            }
        }
    }
    return result;
}

} // namespace subcli
