#include "exporter_internal.hpp"

#include "subcli/capabilities.hpp"
#include "subcli/protocol_registry.hpp"
#include "subcli/util.hpp"

namespace subcli {

ExportResult exportSingBoxImpl(
    const std::vector<ProxyNode>& nodes,
    const AppConfig& config,
    bool tun,
    const std::string& outPath,
    std::string& error
) {
    ExportResult result;
    std::vector<ProxyNode> supported;
    auto prepared = preprocessNodes(nodes, config, result.warnings);
    for (const auto& node : makeExportNodes(prepared)) {
        std::string reason;
        if (supportsNode(ExportTarget::SingBox, node, reason)) {
            supported.push_back(node);
        } else {
            ++result.skipped;
            result.warnings.push_back({"unsupported_node", node.name + ": " + reason});
        }
    }
    const auto groups = buildGroups(supported, config);
    const std::string tpl = resolveTemplatePath(
        config,
        tun ? config.templateTun.at("sing-box") : config.templateNormal.at("sing-box"),
        tun ? "singbox_tun.json" : "singbox_base.json"
    );
    if (!fileExists(tpl)) {
        error = "sing-box template not found: " + tpl;
        return result;
    }

    auto root = nlohmann::json::parse(readFile(tpl), nullptr, false);
    if (root.is_discarded()) {
        error = "invalid sing-box template json";
        return result;
    }

    if (!root.contains("outbounds") || !root["outbounds"].is_array()) {
        root["outbounds"] = nlohmann::json::array();
    }
    removeJsonArrayObjectsByTag(root["outbounds"], generatedSingBoxTags(supported, groups));
    for (const auto& n : supported) {
        if (canonicalProtocolName(n.type) == "wireguard") {
            continue;
        }
        root["outbounds"].push_back(makeSingBoxOutbound(n));
    }
    if (!root.contains("endpoints") || !root["endpoints"].is_array()) {
        root["endpoints"] = nlohmann::json::array();
    }
    removeJsonArrayObjectsByTag(root["endpoints"], generatedSingBoxTags(supported, groups));
    for (const auto& n : supported) {
        if (canonicalProtocolName(n.type) == "wireguard") {
            root["endpoints"].push_back(makeSingBoxWireGuardEndpoint(n));
        }
    }

    nlohmann::json proxySelector = {{"type", "selector"}, {"tag", "PROXY"}, {"outbounds", nlohmann::json::array()}};
    proxySelector["outbounds"].push_back("AUTO");
    proxySelector["outbounds"].push_back("DIRECT");
    for (const auto& name : groups.groups.at("PROXY")) {
        proxySelector["outbounds"].push_back(name);
    }
    proxySelector["default"] = "AUTO";
    root["outbounds"].push_back(proxySelector);

    nlohmann::json autoMembers = groups.groups.at("AUTO");
    if (autoMembers.empty()) {
        autoMembers = nlohmann::json::array({"DIRECT"});
    }
    root["outbounds"].push_back(
        {{"type", "urltest"}, {"tag", "AUTO"}, {"url", "http://www.gstatic.com/generate_204"}, {"interval", "300s"}, {"outbounds", autoMembers}}
    );
    for (const auto& region : groups.regionOrder) {
        nlohmann::json members = nlohmann::json::array();
        for (const auto& member : groups.groups.at(region)) {
            members.push_back(member);
        }
        if (members.empty()) {
            members.push_back("DIRECT");
        }
        root["outbounds"].push_back(
            {{"type", "selector"}, {"tag", region}, {"outbounds", members}, {"default", members.front()}}
        );
    }

    if (!root.contains("dns") || !root["dns"].is_object()) {
        root["dns"] = nlohmann::json::object();
    }
    if (!root["dns"].contains("servers") || !root["dns"]["servers"].is_array()) {
        root["dns"]["servers"] = nlohmann::json::array();
    }
    removeJsonArrayObjectsByTag(root["dns"]["servers"], {"dns-remote"});
    root["dns"]["servers"].push_back(
        {{"type", "udp"}, {"tag", "dns-remote"}, {"server", "1.1.1.1"}, {"server_port", 53}}
    );
    if (!root["dns"].contains("rules") || !root["dns"]["rules"].is_array()) {
        root["dns"]["rules"] = nlohmann::json::array();
    }
    if (root["dns"]["rules"].empty()) {
        root["dns"]["rules"].push_back({{"server", "dns-remote"}});
    }
    if (!root["dns"].contains("final")) {
        root["dns"]["final"] = "dns-remote";
    }

    if (!root.contains("route") || !root["route"].is_object()) {
        root["route"] = nlohmann::json::object();
    }
    if (!root["route"].contains("rules") || !root["route"]["rules"].is_array()) {
        root["route"]["rules"] = nlohmann::json::array();
    }
    if (!hasSingBoxDnsDirectRule(root["route"]["rules"])) {
        root["route"]["rules"].push_back(
            {{"port", 53}, {"network", nlohmann::json::array({"udp"})}, {"action", "route"}, {"outbound", "DIRECT"}}
        );
    }
    if (!root["route"].contains("auto_detect_interface")) {
        root["route"]["auto_detect_interface"] = true;
    }
    if (!root["route"].contains("default_domain_resolver")) {
        root["route"]["default_domain_resolver"] = "dns-remote";
    }
    if (!root["route"].contains("final")) {
        root["route"]["final"] = "PROXY";
    }

    result.ok = writeFile(outPath, root.dump(2), error);
    return result;
}

} // namespace subcli
