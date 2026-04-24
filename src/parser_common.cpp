#include "parser_internal.hpp"

#include <algorithm>
#include <cctype>
#include <regex>
#include <set>
#include <sstream>

#include "subcli/util.hpp"

namespace subcli {

std::string b64Decode(const std::string& in) {
    static const std::string table =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string clean;
    clean.reserve(in.size());
    for (char c : in) {
        if (table.find(c) != std::string::npos || c == '=') {
            clean.push_back(c);
        }
    }

    std::string out;
    int val = 0;
    int valb = -8;
    for (unsigned char c : clean) {
        if (c == '=') {
            break;
        }
        int idx = static_cast<int>(table.find(c));
        if (idx < 0) {
            continue;
        }
        val = (val << 6) + idx;
        valb += 6;
        if (valb >= 0) {
            out.push_back(static_cast<char>((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}

std::string decodeUrlSafeB64(std::string s) {
    std::replace(s.begin(), s.end(), '-', '+');
    std::replace(s.begin(), s.end(), '_', '/');
    while (s.size() % 4 != 0) {
        s.push_back('=');
    }
    return b64Decode(s);
}

std::string trim(std::string v) {
    while (!v.empty() && std::isspace(static_cast<unsigned char>(v.back()))) {
        v.pop_back();
    }
    size_t i = 0;
    while (i < v.size() && std::isspace(static_cast<unsigned char>(v[i]))) {
        ++i;
    }
    return v.substr(i);
}

namespace {

int hexVal(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

std::vector<std::string> splitCommaValues(const std::string& input) {
    std::vector<std::string> out;
    std::stringstream ss(input);
    std::string part;
    while (std::getline(ss, part, ',')) {
        part = trim(part);
        if (!part.empty()) {
            out.push_back(part);
        }
    }
    return out;
}

int parseIntOrDefault(const std::string& value, int fallback = 0) {
    try {
        return std::stoi(value);
    } catch (...) {
        return fallback;
    }
}

std::string joinYamlStringList(const YAML::Node& node) {
    if (!node) {
        return "";
    }
    if (node.IsScalar()) {
        return node.as<std::string>("");
    }
    if (!node.IsSequence()) {
        return "";
    }
    std::string out;
    for (const auto& item : node) {
        const auto part = item.as<std::string>("");
        if (part.empty()) {
            continue;
        }
        if (!out.empty()) {
            out += ",";
        }
        out += part;
    }
    return out;
}

std::string joinYamlIntList(const YAML::Node& node) {
    if (!node) {
        return "";
    }
    if (node.IsScalar()) {
        return node.as<std::string>("");
    }
    if (!node.IsSequence()) {
        return "";
    }
    std::string out;
    for (const auto& item : node) {
        if (!out.empty()) {
            out += ",";
        }
        out += std::to_string(item.as<int>(0));
    }
    return out;
}

std::string yamlScalarOrJoined(const YAML::Node& node) {
    return joinYamlStringList(node);
}

void copyYamlMapScalars(const YAML::Node& node, const std::string& prefix, std::map<std::string, std::string>& out) {
    if (!node || !node.IsMap()) {
        return;
    }
    for (const auto& item : node) {
        const auto key = item.first.as<std::string>("");
        if (key.empty()) {
            continue;
        }
        if (item.second.IsScalar() || item.second.IsSequence()) {
            out[prefix + key] = yamlScalarOrJoined(item.second);
        }
    }
}

bool isMihomoManagedKey(const std::string& key) {
    static const std::set<std::string> managed = {
        "name", "type", "server", "port", "uuid", "password", "cipher", "method", "tls", "skip-cert-verify",
        "network", "servername", "sni", "client-fingerprint", "flow", "ws-opts", "http-opts", "h2-opts",
        "grpc-opts", "reality-opts", "alterId", "packet-encoding", "global-padding", "authenticated-length",
        "encryption", "udp-over-tcp", "udp-over-tcp-version", "plugin", "plugin-opts", "ss-opts", "private-key",
        "public-key", "pre-shared-key", "allowed-ips", "reserved", "mtu", "ip", "ipv6", "peers", "version",
        "ports", "hop-interval", "up", "down", "up-mbps", "down-mbps", "token",
        "congestion-controller", "udp-relay-mode", "reduce-rtt", "heartbeat-interval"
    };
    return managed.count(key) > 0;
}

void copyMihomoRawFields(const YAML::Node& node, std::map<std::string, std::string>& out) {
    if (!node || !node.IsMap()) {
        return;
    }
    for (const auto& item : node) {
        const auto key = item.first.as<std::string>("");
        if (key.empty() || isMihomoManagedKey(key)) {
            continue;
        }
        if (item.second.IsScalar() || item.second.IsSequence()) {
            out["raw_mihomo." + key] = yamlScalarOrJoined(item.second);
        }
    }
}

} // namespace

std::string urlDecode(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '%' && i + 2 < in.size()) {
            int hi = hexVal(in[i + 1]);
            int lo = hexVal(in[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        out.push_back(in[i] == '+' ? ' ' : in[i]);
    }
    return out;
}

std::map<std::string, std::string> parseQuery(const std::string& query) {
    std::map<std::string, std::string> out;
    std::stringstream ss(query);
    std::string part;
    while (std::getline(ss, part, '&')) {
        if (part.empty()) {
            continue;
        }
        auto eq = part.find('=');
        if (eq == std::string::npos) {
            out[urlDecode(part)] = "";
            continue;
        }
        out[urlDecode(part.substr(0, eq))] = urlDecode(part.substr(eq + 1));
    }
    return out;
}

bool parseBoolish(const std::string& value) {
    const auto low = toLower(value);
    return low == "1" || low == "true" || low == "yes" || low == "on";
}

std::string joinJsonArray(const nlohmann::json& value) {
    if (!value.is_array()) {
        return "";
    }
    std::string out;
    for (const auto& item : value) {
        std::string part;
        if (item.is_string()) {
            part = item.get<std::string>();
        } else if (item.is_number_integer()) {
            part = std::to_string(item.get<int>());
        } else if (item.is_number_unsigned()) {
            part = std::to_string(item.get<unsigned int>());
        } else if (item.is_number_float()) {
            part = std::to_string(item.get<double>());
        }
        if (part.empty()) {
            continue;
        }
        if (!out.empty()) {
            out += ",";
        }
        out += part;
    }
    return out;
}

bool parseHostPort(const std::string& hostPort, std::string& host, int& port) {
    if (hostPort.empty()) {
        return false;
    }

    if (hostPort.front() == '[') {
        auto end = hostPort.find(']');
        if (end == std::string::npos || end + 2 > hostPort.size() || hostPort[end + 1] != ':') {
            return false;
        }
        host = hostPort.substr(1, end - 1);
        try {
            port = std::stoi(hostPort.substr(end + 2));
        } catch (...) {
            return false;
        }
        return true;
    }

    auto sep = hostPort.rfind(':');
    if (sep == std::string::npos || sep == 0 || sep + 1 >= hostPort.size()) {
        return false;
    }
    host = hostPort.substr(0, sep);
    try {
        port = std::stoi(hostPort.substr(sep + 1));
    } catch (...) {
        return false;
    }
    return true;
}

void applyCommonUriFields(ProxyNode& p, const std::map<std::string, std::string>& query) {
    const auto get = [&](const std::string& key) -> std::string {
        auto it = query.find(key);
        return it == query.end() ? "" : it->second;
    };

    const auto security = toLower(get("security"));
    p.tls = p.tls || security == "tls" || security == "reality";
    p.reality = security == "reality";
    if (p.network.empty()) {
        p.network = toLower(get("type"));
    }
    if (p.host.empty()) {
        p.host = get("host");
    }
    if (p.path.empty()) {
        p.path = get("path");
    }
    p.sni = get("sni");
    if (p.sni.empty()) {
        p.sni = get("servername");
    }
    p.serviceName = get("serviceName");
    if (p.serviceName.empty()) {
        p.serviceName = get("service_name");
    }
    p.flow = get("flow");
    if (p.flow.empty()) {
        p.flow = get("flowName");
    }
    p.alpn = get("alpn");
    p.authority = get("authority");
    if (p.authority.empty()) {
        p.authority = get("grpc-authority");
    }
    p.fingerprint = get("fp");
    if (p.fingerprint.empty()) {
        p.fingerprint = get("fingerprint");
    }
    p.publicKey = get("pbk");
    if (p.publicKey.empty()) {
        p.publicKey = get("publicKey");
    }
    p.shortId = get("sid");
    if (p.shortId.empty()) {
        p.shortId = get("shortId");
    }
    p.spiderX = get("spx");
    if (p.spiderX.empty()) {
        p.spiderX = get("spiderX");
    }
    p.allowInsecure = parseBoolish(get("allowInsecure"));
    const auto packetEncoding = get("packetEncoding").empty() ? get("packet-encoding") : get("packetEncoding");
    if (!packetEncoding.empty()) {
        p.protocol.values["packet_encoding"] = packetEncoding;
    }
    const auto encryption = get("encryption");
    if (!encryption.empty()) {
        p.protocol.values["encryption"] = encryption;
    }
    p.normalize();
}

void applyTlsObject(ProxyNode& p, const nlohmann::json& tls) {
    if (!tls.is_object()) {
        return;
    }
    const bool enabled = tls.contains("enabled") ? tls.value("enabled", true) : true;
    p.tls = enabled;
    p.allowInsecure = tls.value("insecure", false);
    p.sni = tls.value("server_name", p.sni);
    p.alpn = joinJsonArray(tls.value("alpn", nlohmann::json::array()));
    if (tls.contains("utls") && tls["utls"].is_object()) {
        p.fingerprint = tls["utls"].value("fingerprint", p.fingerprint);
    }
    if (tls.contains("reality") && tls["reality"].is_object()) {
        p.reality = tls["reality"].value("enabled", true);
        p.publicKey = tls["reality"].value("public_key", p.publicKey);
        p.shortId = tls["reality"].value("short_id", p.shortId);
    }
    p.normalize();
}

void applyTransportObject(ProxyNode& p, const nlohmann::json& transport) {
    if (!transport.is_object()) {
        return;
    }
    p.network = toLower(transport.value("type", p.network));
    if (p.network == "ws") {
        p.path = transport.value("path", p.path);
        if (transport.contains("headers") && transport["headers"].is_object()) {
            p.host = transport["headers"].value("Host", p.host);
            if (p.host.empty()) {
                p.host = transport["headers"].value("host", p.host);
            }
        }
        p.host = transport.value("host", p.host);
    } else if (p.network == "grpc") {
        p.serviceName = transport.value("service_name", p.serviceName);
        p.authority = transport.value("authority", p.authority);
    } else if (p.network == "http" || p.network == "h2" || p.network == "httpupgrade") {
        p.path = transport.value("path", p.path);
        p.host = joinJsonArray(transport.value("host", nlohmann::json::array()));
    }
    p.normalize();
}

void applyXrayStreamSettings(ProxyNode& p, const nlohmann::json& streamSettings) {
    if (!streamSettings.is_object()) {
        return;
    }
    p.network = toLower(streamSettings.value("network", p.network));
    const auto security = toLower(streamSettings.value("security", ""));
    p.tls = p.tls || security == "tls" || security == "reality";
    p.reality = p.reality || security == "reality";
    if (streamSettings.contains("tlsSettings") && streamSettings["tlsSettings"].is_object()) {
        const auto& tlsSettings = streamSettings["tlsSettings"];
        p.sni = tlsSettings.value("serverName", p.sni);
        p.allowInsecure = tlsSettings.value("allowInsecure", p.allowInsecure);
        p.alpn = joinJsonArray(tlsSettings.value("alpn", nlohmann::json::array()));
        p.fingerprint = tlsSettings.value("fingerprint", p.fingerprint);
    }
    if (streamSettings.contains("realitySettings") && streamSettings["realitySettings"].is_object()) {
        const auto& realitySettings = streamSettings["realitySettings"];
        p.sni = realitySettings.value("serverName", p.sni);
        p.publicKey = realitySettings.value("publicKey", p.publicKey);
        p.shortId = realitySettings.value("shortId", p.shortId);
        p.spiderX = realitySettings.value("spiderX", p.spiderX);
        p.fingerprint = realitySettings.value("fingerprint", p.fingerprint);
    }
    if (streamSettings.contains("wsSettings") && streamSettings["wsSettings"].is_object()) {
        const auto& wsSettings = streamSettings["wsSettings"];
        p.path = wsSettings.value("path", p.path);
        if (wsSettings.contains("headers") && wsSettings["headers"].is_object()) {
            p.host = wsSettings["headers"].value("Host", p.host);
            if (p.host.empty()) {
                p.host = wsSettings["headers"].value("host", p.host);
            }
        }
    }
    if (streamSettings.contains("grpcSettings") && streamSettings["grpcSettings"].is_object()) {
        const auto& grpcSettings = streamSettings["grpcSettings"];
        p.serviceName = grpcSettings.value("serviceName", p.serviceName);
        p.authority = grpcSettings.value("authority", p.authority);
    }
    if (streamSettings.contains("httpSettings") && streamSettings["httpSettings"].is_object()) {
        const auto& httpSettings = streamSettings["httpSettings"];
        p.path = httpSettings.value("path", p.path);
        p.host = joinJsonArray(httpSettings.value("host", nlohmann::json::array()));
    }
    if (streamSettings.contains("httpupgradeSettings") && streamSettings["httpupgradeSettings"].is_object()) {
        const auto& upgradeSettings = streamSettings["httpupgradeSettings"];
        p.path = upgradeSettings.value("path", p.path);
        p.host = upgradeSettings.value("host", p.host);
    }
    p.normalize();
}

std::string detectRegion(const std::string& name, const AppConfig& config) {
    static const std::map<std::string, std::string> defaults = {
        {"HK", "(?i)(hong kong|hongkong|hk|香港)"},
        {"SG", "(?i)(singapore|sg|新加坡)"},
        {"JP", "(?i)(japan|jp|tokyo|osaka|日本)"},
        {"TW", "(?i)(taiwan|tw|台灣|台湾)"},
        {"US", "(?i)(united states|usa|us|america|美国)"},
    };
    const auto& rules = config.regionRules.empty() ? defaults : config.regionRules;
    for (const auto& kv : rules) {
        try {
            std::string pattern = kv.second;
            std::regex_constants::syntax_option_type opt = std::regex_constants::ECMAScript;
            if (pattern.rfind("(?i)", 0) == 0) {
                pattern = pattern.substr(4);
                opt |= std::regex_constants::icase;
            }
            if (std::regex_search(name, std::regex(pattern, opt))) {
                return kv.first;
            }
        } catch (...) {
        }
    }
    return "OTHER";
}

void parseMihomoBaseFields(ProxyNode& p, const YAML::Node& n) {
    p.type = n["type"].as<std::string>("unknown");
    p.name = n["name"].as<std::string>("node");
    p.server = n["server"].as<std::string>("");
    p.port = n["port"].as<int>(0);
    p.uuid = n["uuid"].as<std::string>("");
    p.password = n["password"].as<std::string>("");
    p.cipher = n["cipher"].as<std::string>("");
    if (p.cipher.empty()) {
        p.cipher = n["method"].as<std::string>("");
    }
    p.flow = n["flow"].as<std::string>("");
}

void parseMihomoTlsFields(ProxyNode& p, const YAML::Node& n) {
    p.tls = n["tls"].as<bool>(false);
    p.allowInsecure = n["skip-cert-verify"].as<bool>(false);
    p.sni = n["servername"].as<std::string>(n["sni"].as<std::string>(""));
    p.fingerprint = n["client-fingerprint"].as<std::string>("");
    if (n["reality-opts"]) {
        p.reality = true;
        p.publicKey = n["reality-opts"]["public-key"].as<std::string>("");
        p.shortId = n["reality-opts"]["short-id"].as<std::string>("");
    }
}

void parseMihomoTransportFields(ProxyNode& p, const YAML::Node& n) {
    p.network = n["network"].as<std::string>("");
    p.path = n["ws-opts"] && n["ws-opts"]["path"] ? n["ws-opts"]["path"].as<std::string>("") : "";
    p.host = n["ws-opts"] && n["ws-opts"]["headers"] && n["ws-opts"]["headers"]["Host"]
                 ? n["ws-opts"]["headers"]["Host"].as<std::string>("")
                 : "";
    const auto network = toLower(p.network);
    if (network == "http") {
        if (n["http-opts"] && n["http-opts"]["path"]) {
            p.path = n["http-opts"]["path"].as<std::string>(p.path);
        }
        if (n["http-opts"] && n["http-opts"]["headers"] && n["http-opts"]["headers"]["Host"]) {
            p.host = joinYamlStringList(n["http-opts"]["headers"]["Host"]);
        }
    } else if (network == "h2") {
        if (n["h2-opts"] && n["h2-opts"]["path"]) {
            p.path = n["h2-opts"]["path"].as<std::string>(p.path);
        }
        if (n["h2-opts"] && n["h2-opts"]["host"]) {
            p.host = joinYamlStringList(n["h2-opts"]["host"]);
        }
    }
    p.serviceName = n["grpc-opts"] && n["grpc-opts"]["grpc-service-name"]
                        ? n["grpc-opts"]["grpc-service-name"].as<std::string>("")
                        : "";
    p.authority = n["grpc-opts"] && n["grpc-opts"]["grpc-authority"]
                      ? n["grpc-opts"]["grpc-authority"].as<std::string>("")
                      : "";
}

void parseMihomoHighFrequencyFields(ProxyNode& p, const YAML::Node& n) {
    if (n["alterId"]) {
        p.protocol.values["alter_id"] = std::to_string(n["alterId"].as<int>(0));
    }
    if (n["packet-encoding"]) {
        p.protocol.values["packet_encoding"] = n["packet-encoding"].as<std::string>("");
    }
    if (n["global-padding"]) {
        p.protocol.values["global_padding"] = n["global-padding"].as<bool>(false) ? "true" : "false";
    }
    if (n["authenticated-length"]) {
        p.protocol.values["authenticated_length"] = n["authenticated-length"].as<bool>(false) ? "true" : "false";
    }
    if (n["encryption"]) {
        p.protocol.values["encryption"] = n["encryption"].as<std::string>("");
    }
    if (n["udp-over-tcp"]) {
        p.protocol.values["udp_over_tcp"] = n["udp-over-tcp"].as<bool>(false) ? "true" : "false";
    }
    if (n["udp-over-tcp-version"]) {
        p.protocol.values["udp_over_tcp_version"] = std::to_string(n["udp-over-tcp-version"].as<int>(0));
    }
    if (n["plugin"]) {
        p.protocol.values["plugin"] = n["plugin"].as<std::string>("");
    }
    copyYamlMapScalars(n["plugin-opts"], "plugin.", p.protocol.values);
    copyYamlMapScalars(n["ss-opts"], "trojan_ss.", p.protocol.values);
}

void parseMihomoWireGuardFields(ProxyNode& p, const YAML::Node& n) {
    if (toLower(p.type) != "wireguard") {
        return;
    }
    p.protocol.values["private_key"] = n["private-key"].as<std::string>("");
    p.protocol.values["peer_public_key"] = n["public-key"].as<std::string>("");
    p.protocol.values["pre_shared_key"] = n["pre-shared-key"].as<std::string>("");
    p.protocol.values["peer_allowed_ips"] = joinYamlStringList(n["allowed-ips"]);
    p.protocol.values["reserved"] = joinYamlIntList(n["reserved"]);
    if (n["mtu"]) {
        p.protocol.values["mtu"] = std::to_string(n["mtu"].as<int>(0));
    }
    std::string localAddress = n["ip"].as<std::string>("");
    const auto ipv6 = n["ipv6"].as<std::string>("");
    if (!ipv6.empty()) {
        localAddress += localAddress.empty() ? ipv6 : "," + ipv6;
    }
    p.protocol.values["local_address"] = localAddress;
    if (n["peers"] && n["peers"].IsSequence() && n["peers"].size() > 0) {
        const auto peer = n["peers"][0];
        p.server = peer["server"].as<std::string>(p.server);
        p.port = peer["port"].as<int>(p.port);
        p.protocol.values["peer_public_key"] = peer["public-key"].as<std::string>(p.protocol.values["peer_public_key"]);
        p.protocol.values["pre_shared_key"] = peer["pre-shared-key"].as<std::string>(p.protocol.values["pre_shared_key"]);
        p.protocol.values["peer_allowed_ips"] = joinYamlStringList(peer["allowed-ips"]);
        p.protocol.values["reserved"] = joinYamlIntList(peer["reserved"]);
    }
}

void parseMihomoModernUdpFields(ProxyNode& p, const YAML::Node& n) {
    const auto lowType = toLower(p.type);
    if (lowType == "hysteria" || lowType == "hysteria2") {
        if (n["version"]) {
            p.protocol.values["version"] = std::to_string(n["version"].as<int>(2));
        }
        if (n["ports"]) {
            p.protocol.values["ports"] = n["ports"].as<std::string>("");
        }
        if (n["hop-interval"]) {
            p.protocol.values["hop_interval"] = n["hop-interval"].as<std::string>("");
        }
        if (n["up"]) {
            p.protocol.values["up"] = n["up"].as<std::string>("");
        }
        if (n["down"]) {
            p.protocol.values["down"] = n["down"].as<std::string>("");
        }
        if (n["up-mbps"]) {
            p.protocol.values["up_mbps"] = std::to_string(n["up-mbps"].as<int>(0));
        }
        if (n["down-mbps"]) {
            p.protocol.values["down_mbps"] = std::to_string(n["down-mbps"].as<int>(0));
        }
        if (n["obfs"]) {
            p.protocol.values["obfs_type"] = n["obfs"].as<std::string>("");
        }
        if (n["obfs-password"]) {
            p.protocol.values["obfs_password"] = n["obfs-password"].as<std::string>("");
        }
    }
    if (lowType == "tuic") {
        if (n["token"]) {
            p.protocol.values["token"] = n["token"].as<std::string>("");
        }
        if (n["congestion-controller"]) {
            p.protocol.values["congestion_control"] = n["congestion-controller"].as<std::string>("");
        }
        if (n["udp-relay-mode"]) {
            p.protocol.values["udp_relay_mode"] = n["udp-relay-mode"].as<std::string>("");
        }
        if (n["reduce-rtt"]) {
            p.protocol.values["zero_rtt_handshake"] = n["reduce-rtt"].as<bool>(false) ? "true" : "false";
        }
        if (n["heartbeat-interval"]) {
            p.protocol.values["heartbeat"] = n["heartbeat-interval"].as<std::string>("");
        }
    }
}

ProxyNode fromMihomoProxy(const YAML::Node& n, const std::string& sourceId, const AppConfig& config) {
    ProxyNode p;
    parseMihomoBaseFields(p, n);
    parseMihomoTlsFields(p, n);
    parseMihomoTransportFields(p, n);
    parseMihomoHighFrequencyFields(p, n);
    parseMihomoWireGuardFields(p, n);
    parseMihomoModernUdpFields(p, n);
    copyMihomoRawFields(n, p.protocol.values);
    p.sourceId = sourceId;
    p.region = detectRegion(p.name, config);
    p.normalize();
    return p;
}

ProxyNode fromUri(const std::string& line, const std::string& sourceId, const AppConfig& config) {
    ProxyNode p;
    p.sourceId = sourceId;
    p.name = "node";

    if (line.rfind("vmess://", 0) == 0) {
        p.type = "vmess";
        auto jsonText = decodeUrlSafeB64(line.substr(8));
        auto j = nlohmann::json::parse(jsonText, nullptr, false);
        if (!j.is_discarded()) {
            p.name = j.value("ps", "vmess");
            p.server = j.value("add", "");
            p.port = parseIntOrDefault(j.value("port", "0"), 0);
            p.uuid = j.value("id", "");
            p.cipher = j.value("scy", "auto");
            p.protocol.values["alter_id"] = j.value("aid", "0");
            p.network = j.value("net", "");
            p.host = j.value("host", "");
            p.path = j.value("path", "");
            p.tls = j.value("tls", "") == "tls";
            p.sni = j.value("sni", j.value("servername", ""));
            p.alpn = j.value("alpn", "");
            p.allowInsecure = parseBoolish(j.value("allowInsecure", "false"));
            p.fingerprint = j.value("fp", j.value("fingerprint", ""));
            if (j.contains("packetEncoding") && j["packetEncoding"].is_string()) {
                p.protocol.values["packet_encoding"] = j["packetEncoding"].get<std::string>();
            }
        }
    } else if (line.rfind("vless://", 0) == 0) {
        p.type = "vless";
        std::string rest = line.substr(8);
        auto hashPos = rest.find('#');
        if (hashPos != std::string::npos) {
            p.name = urlDecode(rest.substr(hashPos + 1));
            rest = rest.substr(0, hashPos);
        }
        std::map<std::string, std::string> query;
        auto qPos = rest.find('?');
        if (qPos != std::string::npos) {
            query = parseQuery(rest.substr(qPos + 1));
            rest = rest.substr(0, qPos);
        }
        auto atPos = rest.rfind('@');
        if (atPos != std::string::npos) {
            p.uuid = urlDecode(rest.substr(0, atPos));
            parseHostPort(rest.substr(atPos + 1), p.server, p.port);
            applyCommonUriFields(p, query);
        }
    } else if (line.rfind("trojan://", 0) == 0) {
        p.type = "trojan";
        std::string rest = line.substr(9);
        auto hashPos = rest.find('#');
        if (hashPos != std::string::npos) {
            p.name = urlDecode(rest.substr(hashPos + 1));
            rest = rest.substr(0, hashPos);
        }
        std::map<std::string, std::string> query;
        auto qPos = rest.find('?');
        if (qPos != std::string::npos) {
            query = parseQuery(rest.substr(qPos + 1));
            rest = rest.substr(0, qPos);
        }
        auto atPos = rest.rfind('@');
        if (atPos != std::string::npos) {
            p.password = urlDecode(rest.substr(0, atPos));
            parseHostPort(rest.substr(atPos + 1), p.server, p.port);
            p.tls = true;
            applyCommonUriFields(p, query);
        }
    } else if (line.rfind("ss://", 0) == 0) {
        p.type = "ss";
        auto payload = trim(line.substr(5));
        auto hashPos = payload.find('#');
        if (hashPos != std::string::npos) {
            p.name = urlDecode(payload.substr(hashPos + 1));
            payload = payload.substr(0, hashPos);
        }

        auto qPos = payload.find('?');
        if (qPos != std::string::npos) {
            const auto query = parseQuery(payload.substr(qPos + 1));
            auto plugin = query.find("plugin");
            if (plugin != query.end()) {
                p.protocol.values["plugin"] = plugin->second;
            }
            auto uot = query.find("uot");
            if (uot != query.end()) {
                p.protocol.values["udp_over_tcp"] = parseBoolish(uot->second) ? "true" : "false";
            }
            auto uotVersion = query.find("UoTVersion");
            if (uotVersion != query.end()) {
                p.protocol.values["udp_over_tcp_version"] = uotVersion->second;
            }
            payload = payload.substr(0, qPos);
        }

        auto atPos = payload.rfind('@');
        if (atPos == std::string::npos) {
            auto decoded = decodeUrlSafeB64(payload);
            if (!decoded.empty()) {
                payload = decoded;
                atPos = payload.rfind('@');
            }
        }

        if (atPos != std::string::npos) {
            std::string userInfo = payload.substr(0, atPos);
            const std::string hostPort = payload.substr(atPos + 1);

            if (userInfo.find(':') == std::string::npos) {
                auto decodedUser = decodeUrlSafeB64(userInfo);
                if (!decodedUser.empty()) {
                    userInfo = decodedUser;
                }
            }

            auto methodSep = userInfo.find(':');
            auto portSep = hostPort.rfind(':');
            if (methodSep != std::string::npos && portSep != std::string::npos && portSep + 1 < hostPort.size()) {
                p.cipher = urlDecode(userInfo.substr(0, methodSep));
                p.password = urlDecode(userInfo.substr(methodSep + 1));
                p.server = hostPort.substr(0, portSep);
                try {
                    p.port = std::stoi(hostPort.substr(portSep + 1));
                } catch (...) {
                    p.port = 0;
                }
            }
        }
    }

    p.region = detectRegion(p.name, config);
    p.normalize();
    return p;
}

} // namespace subcli
