#include "exporter_internal.hpp"

#include "subcli/capabilities.hpp"
#include "subcli/protocol_registry.hpp"
#include "subcli/util.hpp"

namespace subcli {

namespace {

std::string singBoxIntervalString(int seconds) {
    return std::to_string(seconds > 0 ? seconds : 300) + "s";
}

std::string normalizeSingBoxGroupType(std::string type) {
    type = toLower(type);
    if (type == "url-test" || type == "urltest") {
        return "urltest";
    }
    return "selector";
}

std::string assetPathOrEmpty(const AppConfig& config, const std::string& key) {
    const auto it = config.assetPaths.find(key);
    return it == config.assetPaths.end() ? "" : it->second;
}

void removeSingBoxManagedRuleSets(nlohmann::json& ruleSets) {
    if (!ruleSets.is_array()) {
        ruleSets = nlohmann::json::array();
        return;
    }
    nlohmann::json kept = nlohmann::json::array();
    for (const auto& item : ruleSets) {
        const std::string tag = item.is_object() ? item.value("tag", "") : "";
        if (tag == "geosite-cn" || tag == "geoip-cn") {
            continue;
        }
        kept.push_back(item);
    }
    ruleSets = kept;
}

bool hasRuleSetOutboundRule(const nlohmann::json& rules, const std::string& tag, const std::string& outbound) {
    if (!rules.is_array()) {
        return false;
    }
    for (const auto& rule : rules) {
        if (!rule.is_object() || rule.value("outbound", "") != outbound || !rule.contains("rule_set")) {
            continue;
        }
        for (const auto& item : rule["rule_set"]) {
            if (item.get<std::string>() == tag) {
                return true;
            }
        }
    }
    return false;
}

void ensureSingBoxBypassCnProfile(nlohmann::json& root, const AppConfig& config) {
    if (!root.contains("dns") || !root["dns"].is_object()) {
        root["dns"] = nlohmann::json::object();
    }
    root["dns"]["final"] = "dns-direct";
    root["dns"]["strategy"] = "prefer_ipv4";
    root["dns"]["servers"] = nlohmann::json::array({
        {{"type", "https"}, {"tag", "dns-direct"}, {"server", "223.5.5.5"}},
        {{"type", "https"}, {"tag", "dns-remote"}, {"server", "1.1.1.1"}},
    });

    if (!root.contains("route") || !root["route"].is_object()) {
        root["route"] = nlohmann::json::object();
    }
    root["route"]["auto_detect_interface"] = true;
    root["route"]["default_domain_resolver"] = "dns-direct";
    root["route"]["final"] = "PROXY";

    removeSingBoxManagedRuleSets(root["route"]["rule_set"]);
    root["route"]["rule_set"].push_back(
        {{"type", "local"}, {"tag", "geosite-cn"}, {"format", "binary"}, {"path", assetPathOrEmpty(config, "sing-box.geosite-cn")}}
    );
    root["route"]["rule_set"].push_back(
        {{"type", "local"}, {"tag", "geoip-cn"}, {"format", "binary"}, {"path", assetPathOrEmpty(config, "sing-box.geoip-cn")}}
    );

    if (!root["route"].contains("rules") || !root["route"]["rules"].is_array()) {
        root["route"]["rules"] = nlohmann::json::array();
    }
    if (!hasSingBoxDnsDirectRule(root["route"]["rules"])) {
        root["route"]["rules"].push_back(
            {{"port", 53}, {"network", nlohmann::json::array({"udp"})}, {"action", "route"}, {"outbound", "DIRECT"}}
        );
    }
    if (!hasRuleSetOutboundRule(root["route"]["rules"], "geosite-cn", "DIRECT")) {
        root["route"]["rules"].push_back({{"rule_set", nlohmann::json::array({"geosite-cn"})}, {"outbound", "DIRECT"}});
    }
    if (!hasRuleSetOutboundRule(root["route"]["rules"], "geoip-cn", "DIRECT")) {
        root["route"]["rules"].push_back({{"rule_set", nlohmann::json::array({"geoip-cn"})}, {"outbound", "DIRECT"}});
    }
}

void ensureSingBoxCustomRoutingRules(nlohmann::json& root, const AppConfig& config) {
    if (!root.contains("route") || !root["route"].is_object()) {
        root["route"] = nlohmann::json::object();
    }
    if (!root["route"].contains("rules") || !root["route"]["rules"].is_array()) {
        root["route"]["rules"] = nlohmann::json::array();
    }
    if (!root["route"].contains("rule_set") || !root["route"]["rule_set"].is_array()) {
        root["route"]["rule_set"] = nlohmann::json::array();
    }

    bool needGeositeCn = false;
    bool needGeoipCn = false;
    for (const auto& item : config.routingRules) {
        const std::string type = toLower(item.type);
        const std::string value = toLower(item.value);
        if (type == "geosite" && value == "cn") {
            needGeositeCn = true;
        }
        if (type == "geoip" && value == "cn") {
            needGeoipCn = true;
        }
    }

    removeSingBoxManagedRuleSets(root["route"]["rule_set"]);
    if (needGeositeCn) {
        root["route"]["rule_set"].push_back(
            {{"type", "local"}, {"tag", "geosite-cn"}, {"format", "binary"}, {"path", assetPathOrEmpty(config, "sing-box.geosite-cn")}}
        );
    }
    if (needGeoipCn) {
        root["route"]["rule_set"].push_back(
            {{"type", "local"}, {"tag", "geoip-cn"}, {"format", "binary"}, {"path", assetPathOrEmpty(config, "sing-box.geoip-cn")}}
        );
    }

    for (const auto& item : config.routingRules) {
        const std::string type = toLower(item.type);
        const std::string value = toLower(item.value);
        if ((type == "match" || type == "final") && !item.outbound.empty()) {
            root["route"]["final"] = item.outbound;
            continue;
        }
        if (type == "geosite" && value == "cn" && !item.outbound.empty()) {
            if (!hasRuleSetOutboundRule(root["route"]["rules"], "geosite-cn", item.outbound)) {
                root["route"]["rules"].push_back({{"rule_set", nlohmann::json::array({"geosite-cn"})}, {"outbound", item.outbound}});
            }
            continue;
        }
        if (type == "geoip" && value == "cn" && !item.outbound.empty()) {
            if (!hasRuleSetOutboundRule(root["route"]["rules"], "geoip-cn", item.outbound)) {
                root["route"]["rules"].push_back({{"rule_set", nlohmann::json::array({"geoip-cn"})}, {"outbound", item.outbound}});
            }
            continue;
        }
    }
}

} // namespace

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
    if (supported.empty()) {
        error = "sing-box has no supported nodes after filtering";
        return result;
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
    auto managedSingBoxTags = generatedSingBoxTags(supported, groups);
    for (const auto& configured : config.strategyGroups) {
        managedSingBoxTags.insert(configured.name);
    }
    removeJsonArrayObjectsByTag(root["outbounds"], managedSingBoxTags);
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

    if (!config.strategyGroups.empty()) {
        for (const auto& configured : config.strategyGroups) {
            const auto type = normalizeSingBoxGroupType(configured.type);
            nlohmann::json members = nlohmann::json::array();
            for (const auto& member : configured.members) {
                members.push_back(member);
            }
            if (members.empty()) {
                members.push_back("DIRECT");
            }
            if (type == "urltest") {
                root["outbounds"].push_back(
                    {{"type", "urltest"},
                     {"tag", configured.name},
                     {"url", configured.url.empty() ? "http://www.gstatic.com/generate_204" : configured.url},
                     {"interval", singBoxIntervalString(configured.interval)},
                     {"outbounds", members}}
                );
            } else {
                nlohmann::json selector = {{"type", "selector"}, {"tag", configured.name}, {"outbounds", members}};
                if (!configured.defaultMember.empty()) {
                    selector["default"] = configured.defaultMember;
                }
                root["outbounds"].push_back(selector);
            }
        }
    } else {
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
    }

    if (!config.routingRules.empty()) {
        ensureSingBoxCustomRoutingRules(root, config);
    } else if (config.profile == "bypass-cn") {
        ensureSingBoxBypassCnProfile(root, config);
    } else {
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
    }

    result.ok = writeFile(outPath, root.dump(2), error);
    return result;
}

} // namespace subcli
