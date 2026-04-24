#include "subcli/node.hpp"

#include <sstream>

#include "subcli/protocol_registry.hpp"

namespace subcli {
namespace {

int intValue(const std::map<std::string, std::string>& values, const std::string& key, int fallback = 0) {
    auto it = values.find(key);
    if (it == values.end() || it->second.empty()) {
        return fallback;
    }
    try {
        return std::stoi(it->second);
    } catch (...) {
        return fallback;
    }
}

bool boolValue(const std::map<std::string, std::string>& values, const std::string& key, bool fallback = false) {
    auto it = values.find(key);
    if (it == values.end() || it->second.empty()) {
        return fallback;
    }
    return it->second == "1" || it->second == "true" || it->second == "yes" || it->second == "on";
}

std::string stringValue(const std::map<std::string, std::string>& values, const std::string& key, const std::string& fallback = "") {
    auto it = values.find(key);
    return it == values.end() ? fallback : it->second;
}

std::vector<std::string> stringListValue(const std::map<std::string, std::string>& values, const std::string& key) {
    std::vector<std::string> out;
    std::stringstream ss(stringValue(values, key));
    std::string part;
    while (std::getline(ss, part, ',')) {
        if (!part.empty()) {
            out.push_back(part);
        }
    }
    return out;
}

std::vector<int> intListValue(const std::map<std::string, std::string>& values, const std::string& key) {
    std::vector<int> out;
    std::stringstream ss(stringValue(values, key));
    std::string part;
    while (std::getline(ss, part, ',')) {
        if (part.empty()) {
            continue;
        }
        try {
            out.push_back(std::stoi(part));
        } catch (...) {
        }
    }
    return out;
}

} // namespace

Node legacyToStructuredNode(const ProxyNode& legacy) {
    ProxyNode normalized = legacy;
    normalized.normalize();

    Node node;
    node.protocol = canonicalProtocolName(normalized.type);
    node.sourceId = normalized.sourceId;
    node.name = normalized.name;
    node.server = {normalized.server, normalized.port};
    node.tls = normalized.tlsConfig;
    node.transport = normalized.transport;

    if (node.protocol == "shadowsocks") {
        ShadowsocksOptions options;
        options.method = normalized.protocol.cipher;
        options.password = normalized.protocol.password;
        options.udpOverTcp = boolValue(normalized.protocol.values, "udp_over_tcp");
        options.udpOverTcpVersion = intValue(normalized.protocol.values, "udp_over_tcp_version");
        options.plugin = stringValue(normalized.protocol.values, "plugin");
        node.options = options;
    } else if (node.protocol == "vmess") {
        VMessOptions options;
        options.uuid = normalized.protocol.uuid;
        options.security = normalized.protocol.cipher.empty() ? "auto" : normalized.protocol.cipher;
        options.alterId = intValue(normalized.protocol.values, "alter_id");
        options.packetEncoding = stringValue(normalized.protocol.values, "packet_encoding");
        options.globalPadding = boolValue(normalized.protocol.values, "global_padding");
        options.authenticatedLength = boolValue(normalized.protocol.values, "authenticated_length");
        node.options = options;
    } else if (node.protocol == "vless") {
        VLESSOptions options;
        options.uuid = normalized.protocol.uuid;
        options.flow = normalized.protocol.flow;
        options.encryption = stringValue(normalized.protocol.values, "encryption", "none");
        options.packetEncoding = stringValue(normalized.protocol.values, "packet_encoding");
        node.options = options;
    } else if (node.protocol == "trojan") {
        TrojanOptions options;
        options.password = normalized.protocol.password;
        node.options = options;
    } else if (node.protocol == "hysteria") {
        HysteriaOptions options;
        options.version = intValue(normalized.protocol.values, "version", 2);
        node.options = options;
    } else if (node.protocol == "hysteria2") {
        Hysteria2Options options;
        options.password = normalized.protocol.password;
        options.ports = stringValue(normalized.protocol.values, "ports");
        options.hopInterval = stringValue(normalized.protocol.values, "hop_interval");
        options.hopIntervalMax = stringValue(normalized.protocol.values, "hop_interval_max");
        options.upMbps = intValue(normalized.protocol.values, "up_mbps", intValue(normalized.protocol.values, "up"));
        options.downMbps = intValue(normalized.protocol.values, "down_mbps", intValue(normalized.protocol.values, "down"));
        auto obfsType = normalized.protocol.values.find("obfs_type");
        if (obfsType != normalized.protocol.values.end()) {
            options.obfsType = obfsType->second;
        }
        auto obfsPassword = normalized.protocol.values.find("obfs_password");
        if (obfsPassword != normalized.protocol.values.end()) {
            options.obfsPassword = obfsPassword->second;
        }
        node.options = options;
    } else if (node.protocol == "tuic") {
        TUICOptions options;
        options.token = stringValue(normalized.protocol.values, "token");
        options.uuid = normalized.protocol.uuid;
        options.password = normalized.protocol.password;
        auto congestion = normalized.protocol.values.find("congestion_control");
        if (congestion != normalized.protocol.values.end()) {
            options.congestionControl = congestion->second;
        }
        auto relay = normalized.protocol.values.find("udp_relay_mode");
        if (relay != normalized.protocol.values.end()) {
            options.udpRelayMode = relay->second;
        }
        options.zeroRttHandshake = boolValue(normalized.protocol.values, "zero_rtt_handshake");
        options.heartbeat = stringValue(normalized.protocol.values, "heartbeat");
        node.options = options;
    } else if (node.protocol == "wireguard") {
        WireGuardOptions options;
        options.privateKey = stringValue(normalized.protocol.values, "private_key");
        options.localAddress = stringListValue(normalized.protocol.values, "local_address");
        options.mtu = intValue(normalized.protocol.values, "mtu");
        options.workers = intValue(normalized.protocol.values, "workers");
        options.domainStrategy = stringValue(normalized.protocol.values, "domain_strategy");
        WireGuardPeer peer;
        peer.endpoint = {normalized.server, normalized.port};
        peer.publicKey = stringValue(normalized.protocol.values, "peer_public_key");
        peer.preSharedKey = stringValue(normalized.protocol.values, "pre_shared_key");
        peer.allowedIps = stringListValue(normalized.protocol.values, "peer_allowed_ips");
        peer.reserved = intListValue(normalized.protocol.values, "reserved");
        if (!peer.endpoint.host.empty() || !peer.publicKey.empty()) {
            options.peers.push_back(peer);
        }
        node.options = options;
    } else {
        GenericOptions options;
        options.values = normalized.protocol.values;
        node.options = options;
    }

    return node;
}

} // namespace subcli
