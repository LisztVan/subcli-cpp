#include "parser_internal.hpp"

#include <set>

#include "subcli/util.hpp"

namespace subcli {
namespace {

std::string optionalString(const nlohmann::json& object, const std::string& key, const std::string& fallback = "") {
    if (!object.contains(key) || !object[key].is_string()) {
        return fallback;
    }
    return object[key].get<std::string>();
}

bool isSingBoxManagedKey(const std::string& key) {
    static const std::set<std::string> managed = {
        "type", "tag", "server", "address", "server_port", "port", "uuid", "password", "method", "security",
        "flow", "packet_encoding", "alter_id", "global_padding", "authenticated_length", "udp_over_tcp", "tls",
        "transport", "up_mbps", "down_mbps", "obfs", "server_ports", "hop_interval", "hop_interval_max",
        "congestion_control", "udp_relay_mode", "zero_rtt_handshake", "heartbeat", "private_key", "peer_public_key",
        "pre_shared_key", "reserved", "workers", "mtu", "local_address", "peer_allowed_ips", "peers"
    };
    return managed.count(key) > 0;
}

void copySingBoxRawFields(const nlohmann::json& object, std::map<std::string, std::string>& out) {
    if (!object.is_object()) {
        return;
    }
    for (auto it = object.begin(); it != object.end(); ++it) {
        if (isSingBoxManagedKey(it.key())) {
            continue;
        }
        if (it.value().is_string()) {
            out["raw_singbox." + it.key()] = it.value().get<std::string>();
        } else if (it.value().is_boolean()) {
            out["raw_singbox." + it.key()] = it.value().get<bool>() ? "true" : "false";
        } else if (it.value().is_number_integer()) {
            out["raw_singbox." + it.key()] = std::to_string(it.value().get<int>());
        } else if (it.value().is_array()) {
            out["raw_singbox." + it.key()] = joinJsonArray(it.value());
        }
    }
}

ProxyNode fromSingBoxOutboundObject(const nlohmann::json& o, const std::string& sourceId, const AppConfig& config) {
    std::string type = o.value("type", "");
    std::string low = toLower(type);
    ProxyNode p;
    p.type = low == "shadowsocks" ? "ss" : type;
    p.name = optionalString(o, "tag", optionalString(o, "name", "node"));
    p.server = optionalString(o, "server", optionalString(o, "address", ""));
    p.port = o.value("server_port", o.value("port", 0));
    p.uuid = o.value("uuid", "");
    p.password = o.value("password", "");
    p.cipher = o.value("method", o.value("security", ""));
    p.flow = o.value("flow", "");
    if (o.contains("packet_encoding") && o["packet_encoding"].is_string()) {
        p.protocol.values["packet_encoding"] = o["packet_encoding"].get<std::string>();
    }
    if (o.contains("alter_id") && o["alter_id"].is_number_integer()) {
        p.protocol.values["alter_id"] = std::to_string(o["alter_id"].get<int>());
    }
    if (o.contains("global_padding") && o["global_padding"].is_boolean()) {
        p.protocol.values["global_padding"] = o["global_padding"].get<bool>() ? "true" : "false";
    }
    if (o.contains("authenticated_length") && o["authenticated_length"].is_boolean()) {
        p.protocol.values["authenticated_length"] = o["authenticated_length"].get<bool>() ? "true" : "false";
    }
    if (o.contains("udp_over_tcp")) {
        if (o["udp_over_tcp"].is_boolean()) {
            p.protocol.values["udp_over_tcp"] = o["udp_over_tcp"].get<bool>() ? "true" : "false";
        } else if (o["udp_over_tcp"].is_object()) {
            p.protocol.values["udp_over_tcp"] = "true";
            if (o["udp_over_tcp"].contains("version")) {
                p.protocol.values["udp_over_tcp_version"] = std::to_string(o["udp_over_tcp"].value("version", 0));
            }
        }
    }
    if (o.contains("tls") && o["tls"].is_boolean()) {
        p.tls = o["tls"].get<bool>();
    }
    if (low == "hysteria2") {
        p.protocol.password = o.value("password", "");
        if (o.contains("server_ports") && o["server_ports"].is_array()) {
            p.protocol.values["ports"] = joinJsonArray(o["server_ports"]);
        }
        p.protocol.values["hop_interval"] = o.value("hop_interval", "");
        p.protocol.values["hop_interval_max"] = o.value("hop_interval_max", "");
        p.protocol.values["up_mbps"] = std::to_string(o.value("up_mbps", 0));
        p.protocol.values["down_mbps"] = std::to_string(o.value("down_mbps", 0));
        if (o.contains("obfs") && o["obfs"].is_object()) {
            p.protocol.values["obfs_type"] = o["obfs"].value("type", "");
            p.protocol.values["obfs_password"] = o["obfs"].value("password", "");
        }
    } else if (low == "tuic") {
        p.protocol.uuid = o.value("uuid", "");
        p.protocol.password = o.value("password", "");
        p.protocol.values["congestion_control"] = o.value("congestion_control", "");
        p.protocol.values["udp_relay_mode"] = o.value("udp_relay_mode", "");
        if (o.contains("zero_rtt_handshake") && o["zero_rtt_handshake"].is_boolean()) {
            p.protocol.values["zero_rtt_handshake"] = o["zero_rtt_handshake"].get<bool>() ? "true" : "false";
        }
        p.protocol.values["heartbeat"] = o.value("heartbeat", "");
    } else if (low == "vless") {
        p.protocol.values["encryption"] = o.value("encryption", "none");
    } else if (low == "wireguard") {
        p.protocol.values["private_key"] = o.value("private_key", "");
        p.protocol.values["peer_public_key"] = o.value("peer_public_key", "");
        p.protocol.values["pre_shared_key"] = o.value("pre_shared_key", "");
        p.protocol.values["reserved"] = joinJsonArray(o.value("reserved", nlohmann::json::array()));
        p.protocol.values["workers"] = std::to_string(o.value("workers", 0));
        p.protocol.values["mtu"] = std::to_string(o.value("mtu", 0));
        if (o.contains("local_address") && o["local_address"].is_array()) {
            p.protocol.values["local_address"] = joinJsonArray(o["local_address"]);
        }
        if (o.contains("address") && o["address"].is_array()) {
            p.protocol.values["local_address"] = joinJsonArray(o["address"]);
        }
        if (o.contains("peer_allowed_ips") && o["peer_allowed_ips"].is_array()) {
            p.protocol.values["peer_allowed_ips"] = joinJsonArray(o["peer_allowed_ips"]);
        }
        if (o.contains("peers") && o["peers"].is_array() && !o["peers"].empty()) {
            const auto& peer = o["peers"][0];
            p.server = peer.value("address", p.server);
            p.port = peer.value("port", p.port);
            p.protocol.values["peer_public_key"] = peer.value("public_key", p.protocol.values["peer_public_key"]);
            p.protocol.values["pre_shared_key"] = peer.value("pre_shared_key", p.protocol.values["pre_shared_key"]);
            if (peer.contains("allowed_ips") && peer["allowed_ips"].is_array()) {
                p.protocol.values["peer_allowed_ips"] = joinJsonArray(peer["allowed_ips"]);
            }
            if (peer.contains("reserved") && peer["reserved"].is_array()) {
                p.protocol.values["reserved"] = joinJsonArray(peer["reserved"]);
            }
        }
    }
    if (o.contains("tls") && o["tls"].is_object()) {
        applyTlsObject(p, o["tls"]);
    }
    if (o.contains("transport") && o["transport"].is_object()) {
        applyTransportObject(p, o["transport"]);
    }
    if (p.network.empty()) {
        p.network = toLower(o.value("network", ""));
    }
    copySingBoxRawFields(o, p.protocol.values);
    p.sourceId = sourceId;
    p.region = detectRegion(p.name, config);
    p.normalize();
    return p;
}

} // namespace

ParseResult parseSingBoxSubscription(const std::string& content, const std::string& sourceId, const AppConfig& config) {
    ParseResult result;
    auto j = nlohmann::json::parse(content, nullptr, false);
    if (j.is_discarded()) {
        return result;
    }

    const auto outbounds = j.contains("outbounds") && j["outbounds"].is_array() ? j["outbounds"] : nlohmann::json::array();
    for (const auto& o : outbounds) {
        std::string type = o.value("type", "");
        std::string low = toLower(type);
        if (low.empty() || low == "direct" || low == "block" || low == "dns" || low == "selector" || low == "urltest") {
            continue;
        }
        ProxyNode p = fromSingBoxOutboundObject(o, sourceId, config);
        if (!p.server.empty() && p.port > 0) {
            result.nodes.push_back(p);
        } else {
            ++result.skipped;
        }
    }
    if (j.contains("endpoints") && j["endpoints"].is_array()) {
        for (const auto& endpoint : j["endpoints"]) {
            if (!endpoint.is_object() || toLower(endpoint.value("type", "")) != "wireguard") {
                continue;
            }
            ProxyNode p = fromSingBoxOutboundObject(endpoint, sourceId, config);
            if (!p.server.empty() && p.port > 0) {
                result.nodes.push_back(p);
            } else {
                ++result.skipped;
            }
        }
    }
    return result;
}

} // namespace subcli
