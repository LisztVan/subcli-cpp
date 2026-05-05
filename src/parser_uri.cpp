#include "parser_internal.hpp"

#include <sstream>

namespace subcli {

namespace {

std::string resolveUriFingerprintAlias(const std::map<std::string, std::string>& query) {
    auto it = query.find("fp");
    if (it != query.end() && !it->second.empty()) {
        return it->second;
    }
    it = query.find("client-fingerprint");
    if (it != query.end() && !it->second.empty()) {
        return it->second;
    }
    it = query.find("fingerprint");
    if (it != query.end() && !it->second.empty()) {
        return it->second;
    }
    return "";
}

void applyUriClientFingerprint(ProxyNode& node, const std::string& line) {
    auto queryStart = line.find('?');
    if (queryStart == std::string::npos) {
        return;
    }
    auto queryEnd = line.find('#', queryStart + 1);
    const auto queryText = line.substr(queryStart + 1, queryEnd == std::string::npos ? std::string::npos : queryEnd - queryStart - 1);
    const auto query = parseQuery(queryText);

    const std::string fingerprint = resolveUriFingerprintAlias(query);
    if (fingerprint.empty()) {
        return;
    }

    node.fingerprint = fingerprint;
    node.protocol.values["client-fingerprint"] = fingerprint;
    if (node.tlsConfig.enabled || node.tlsConfig.reality.enabled) {
        node.tlsConfig.fingerprint = fingerprint;
    }
}

} // namespace

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
            line.rfind("trojan://", 0) == 0 || line.rfind("ss://", 0) == 0 ||
            line.rfind("hy2://", 0) == 0 || line.rfind("hysteria2://", 0) == 0 ||
            line.rfind("tuic://", 0) == 0 || line.rfind("wireguard://", 0) == 0) {
            try {
                auto p = fromUri(line, sourceId, config);
                if (line.rfind("vless://", 0) == 0 || line.rfind("trojan://", 0) == 0) {
                    applyUriClientFingerprint(p, line);
                }
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
