#pragma once

#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace subcli {

struct Subscription {
    std::string id;
    std::string name;
    std::string url;
    bool enabled = true;
    std::string group = "default";
    std::vector<std::string> tags;
    std::string formatHint = "auto";
    std::string userAgent;
    int timeout = 15;
    bool timeoutOverride = false;
    int retry = 2;
    bool retryOverride = false;
    std::map<std::string, std::string> headers;
    int updateInterval = 3600;
    long fetchMaxBytes = 10 * 1024 * 1024;

    std::string lastUpdated;
    std::string lastSuccess;
    std::string lastError;
    std::string etag;
    std::string lastModified;
    std::string cachePath;
    int priority = 100;
};

struct AppConfig {
    struct StrategyGroup {
        std::string name;
        std::string type;
        std::vector<std::string> members;
        std::string url;
        int interval = 300;
        std::string defaultMember;
    };

    bool tun = false;
    std::string logLevel = "info";
    int parallelism = 4;
    int timeout = 15;
    int retry = 2;
    long fetchMaxBytes = 10 * 1024 * 1024;
    std::string templateDir = "./templates";
    std::string outputDir = "./outputs";
    std::string mihomoPath;
    std::string singBoxPath;
    std::string xrayPath;

    std::map<std::string, std::string> templateNormal;
    std::map<std::string, std::string> templateTun;
    std::map<std::string, std::string> regionRules;
    std::vector<StrategyGroup> strategyGroups;
    std::string profile = "bypass-cn";
    std::string profilePath;
    struct RoutingRule {
        std::string type;
        std::string value;
        std::string outbound;
    };
    std::vector<RoutingRule> routingRules;
    std::string assetDir = "./assets";
    std::map<std::string, std::string> assetPaths;
    std::map<std::string, std::string> assetUrls;

    bool dedupeNodes = true;
    std::string renameTemplate = "{name}";
    std::string includeRegex;
    std::string excludeRegex;
    std::string sortBy = "region,name";
};

struct TransportConfig {
    std::string network;
    std::string host;
    std::string path;
    std::string serviceName;
    std::string authority;
    std::map<std::string, std::string> headers;
    std::vector<std::string> hostList;
};

struct RealityConfig {
    bool enabled = false;
    std::string publicKey;
    std::string shortId;
    std::string spiderX;
};

struct TlsConfig {
    bool enabled = false;
    bool allowInsecure = false;
    std::string sni;
    std::vector<std::string> alpn;
    std::string fingerprint;
    RealityConfig reality;
};

struct ProtocolExtras {
    std::string uuid;
    std::string password;
    std::string cipher;
    std::string flow;
    std::map<std::string, std::string> values;
};

struct DiagnosticMessage {
    std::string code;
    std::string message;
};

struct ProxyNode {
    std::string type;
    std::string name;
    std::string server;
    int port = 0;

    TransportConfig transport;
    TlsConfig tlsConfig;
    ProtocolExtras protocol;

    std::string uuid;
    std::string password;
    std::string cipher;
    bool tls = false;
    bool allowInsecure = false;
    bool reality = false;
    std::string network;
    std::string host;
    std::string path;
    std::string sni;
    std::string serviceName;
    std::string authority;
    std::string flow;
    std::string alpn;
    std::string fingerprint;
    std::string publicKey;
    std::string shortId;
    std::string spiderX;
    std::string sourceId;
    std::vector<std::string> sourceTags;
    std::string region = "OTHER";
    std::vector<DiagnosticMessage> parseWarnings;
    std::vector<DiagnosticMessage> exportWarnings;

    static std::vector<std::string> splitList(const std::string& value) {
        std::vector<std::string> out;
        std::stringstream ss(value);
        std::string part;
        while (std::getline(ss, part, ',')) {
            if (!part.empty()) {
                out.push_back(part);
            }
        }
        return out;
    }

    void syncStructuredFromLegacy() {
        if (transport.network.empty()) {
            transport.network = network;
        }
        if (transport.host.empty()) {
            transport.host = host;
        }
        if (transport.path.empty()) {
            transport.path = path;
        }
        if (transport.serviceName.empty()) {
            transport.serviceName = serviceName;
        }
        if (transport.authority.empty()) {
            transport.authority = authority;
        }
        tlsConfig.enabled = tlsConfig.enabled || tls || reality;
        tlsConfig.allowInsecure = tlsConfig.allowInsecure || allowInsecure;
        if (tlsConfig.sni.empty()) {
            tlsConfig.sni = sni;
        }
        if (tlsConfig.fingerprint.empty()) {
            tlsConfig.fingerprint = fingerprint;
        }
        tlsConfig.reality.enabled = tlsConfig.reality.enabled || reality;
        if (tlsConfig.reality.publicKey.empty()) {
            tlsConfig.reality.publicKey = publicKey;
        }
        if (tlsConfig.reality.shortId.empty()) {
            tlsConfig.reality.shortId = shortId;
        }
        if (tlsConfig.reality.spiderX.empty()) {
            tlsConfig.reality.spiderX = spiderX;
        }
        if (protocol.uuid.empty()) {
            protocol.uuid = uuid;
        }
        if (protocol.password.empty()) {
            protocol.password = password;
        }
        if (protocol.cipher.empty()) {
            protocol.cipher = cipher;
        }
        if (protocol.flow.empty()) {
            protocol.flow = flow;
        }
        if (tlsConfig.alpn.empty() && !alpn.empty()) {
            tlsConfig.alpn = splitList(alpn);
        }
    }

    void syncLegacyFromStructured() {
        network = transport.network.empty() ? network : transport.network;
        host = transport.host.empty() ? host : transport.host;
        path = transport.path.empty() ? path : transport.path;
        serviceName = transport.serviceName.empty() ? serviceName : transport.serviceName;
        authority = transport.authority.empty() ? authority : transport.authority;
        tls = tlsConfig.enabled;
        allowInsecure = tlsConfig.allowInsecure;
        sni = tlsConfig.sni.empty() ? sni : tlsConfig.sni;
        fingerprint = tlsConfig.fingerprint.empty() ? fingerprint : tlsConfig.fingerprint;
        reality = tlsConfig.reality.enabled;
        publicKey = tlsConfig.reality.publicKey.empty() ? publicKey : tlsConfig.reality.publicKey;
        shortId = tlsConfig.reality.shortId.empty() ? shortId : tlsConfig.reality.shortId;
        spiderX = tlsConfig.reality.spiderX.empty() ? spiderX : tlsConfig.reality.spiderX;
        uuid = protocol.uuid.empty() ? uuid : protocol.uuid;
        password = protocol.password.empty() ? password : protocol.password;
        cipher = protocol.cipher.empty() ? cipher : protocol.cipher;
        flow = protocol.flow.empty() ? flow : protocol.flow;
        alpn.clear();
        for (size_t i = 0; i < tlsConfig.alpn.size(); ++i) {
            if (i) {
                alpn += ",";
            }
            alpn += tlsConfig.alpn[i];
        }
    }

    void normalize() {
        syncStructuredFromLegacy();
        syncLegacyFromStructured();
    }
};

struct ParseResult {
    std::vector<ProxyNode> nodes;
    std::vector<DiagnosticMessage> warnings;
    int skipped = 0;
};

struct ExportResult {
    bool ok = false;
    std::vector<DiagnosticMessage> warnings;
    int skipped = 0;
};

} // namespace subcli
