#include "parser_internal.hpp"

#include "subcli/util.hpp"

namespace subcli {

ParseResult parseXraySubscription(const std::string& content, const std::string& sourceId, const AppConfig& config) {
    ParseResult result;
    auto j = nlohmann::json::parse(content, nullptr, false);
    if (j.is_discarded() || !j.contains("outbounds") || !j["outbounds"].is_array()) {
        return result;
    }

    for (const auto& o : j["outbounds"]) {
        std::string type = o.value("protocol", o.value("type", ""));
        std::string low = toLower(type);
        if (low.empty() || low == "direct" || low == "block" || low == "selector" || low == "urltest") {
            continue;
        }

        ProxyNode p;
        p.type = low == "shadowsocks" ? "ss" : type;
        p.name = o.value("tag", "node");
        p.server = o.value("server", "");
        p.port = o.value("server_port", o.value("port", 0));
        p.uuid = o.value("uuid", "");
        p.password = o.value("password", "");
        p.cipher = o.value("method", o.value("security", ""));
        p.flow = o.value("flow", "");
        if ((p.server.empty() || p.port == 0) && o.contains("settings") && o["settings"].is_object()) {
            const auto& settings = o["settings"];
            if (low == "hysteria") {
                p.server = settings.value("address", p.server);
                p.port = settings.value("port", p.port);
                p.protocol.values["version"] = std::to_string(settings.value("version", 2));
            }
            if (settings.contains("vnext") && settings["vnext"].is_array() && !settings["vnext"].empty()) {
                const auto& first = settings["vnext"][0];
                p.server = first.value("address", p.server);
                p.port = first.value("port", p.port);
                if (first.contains("users") && first["users"].is_array() && !first["users"].empty()) {
                    const auto& user = first["users"][0];
                    p.uuid = user.value("id", p.uuid);
                    p.cipher = user.value("security", p.cipher);
                    p.flow = user.value("flow", p.flow);
                    if (user.contains("encryption") && user["encryption"].is_string()) {
                        p.protocol.values["encryption"] = user["encryption"].get<std::string>();
                    }
                    if (user.contains("alterId") && user["alterId"].is_number_integer()) {
                        p.protocol.values["alter_id"] = std::to_string(user["alterId"].get<int>());
                    }
                    if (user.contains("experiments") && user["experiments"].is_string()) {
                        const auto experiments = user["experiments"].get<std::string>();
                        if (experiments.find("AuthenticatedLength") != std::string::npos) {
                            p.protocol.values["authenticated_length"] = "true";
                        }
                    }
                }
            }
            if (settings.contains("servers") && settings["servers"].is_array() && !settings["servers"].empty()) {
                const auto& first = settings["servers"][0];
                p.server = first.value("address", p.server);
                p.port = first.value("port", p.port);
                p.password = first.value("password", p.password);
                p.cipher = first.value("method", p.cipher);
                if (first.contains("uot") && first["uot"].is_boolean()) {
                    p.protocol.values["udp_over_tcp"] = first["uot"].get<bool>() ? "true" : "false";
                }
                if (first.contains("UoTVersion") && first["UoTVersion"].is_number_integer()) {
                    p.protocol.values["udp_over_tcp_version"] = std::to_string(first["UoTVersion"].get<int>());
                }
            }
            if (low == "wireguard") {
                p.protocol.values["private_key"] = settings.value("secretKey", "");
                p.protocol.values["domain_strategy"] = settings.value("domainStrategy", "");
                if (settings.contains("address") && settings["address"].is_array()) {
                    p.protocol.values["local_address"] = joinJsonArray(settings["address"]);
                }
                if (settings.contains("reserved") && settings["reserved"].is_array()) {
                    p.protocol.values["reserved"] = joinJsonArray(settings["reserved"]);
                }
                if (settings.contains("workers") && settings["workers"].is_number_integer()) {
                    p.protocol.values["workers"] = std::to_string(settings["workers"].get<int>());
                }
                if (settings.contains("mtu") && settings["mtu"].is_number_integer()) {
                    p.protocol.values["mtu"] = std::to_string(settings["mtu"].get<int>());
                }
                if (settings.contains("peers") && settings["peers"].is_array() && !settings["peers"].empty()) {
                    const auto& peer = settings["peers"][0];
                    std::string endpoint = peer.value("endpoint", "");
                    int endpointPort = 0;
                    if (parseHostPort(endpoint, p.server, endpointPort)) {
                        p.port = endpointPort;
                    }
                    p.protocol.values["peer_public_key"] = peer.value("publicKey", "");
                    p.protocol.values["pre_shared_key"] = peer.value("preSharedKey", "");
                    if (peer.contains("allowedIPs") && peer["allowedIPs"].is_array()) {
                        p.protocol.values["peer_allowed_ips"] = joinJsonArray(peer["allowedIPs"]);
                    }
                }
            }
        }
        if (o.contains("streamSettings") && o["streamSettings"].is_object()) {
            applyXrayStreamSettings(p, o["streamSettings"]);
        }
        p.sourceId = sourceId;
        p.region = detectRegion(p.name, config);
        p.normalize();
        if (!p.server.empty() && p.port > 0) {
            result.nodes.push_back(p);
        } else if (low == "freedom" || low == "blackhole" || low == "dns" || low == "loopback") {
            result.nodes.push_back(p);
        } else {
            ++result.skipped;
        }
    }

    return result;
}

} // namespace subcli
