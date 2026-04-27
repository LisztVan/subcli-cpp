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
    if (type == "fallback") {
        return "urltest";
    }
    if (type == "load-balance" || type == "loadbalance") {
        return "selector";
    }
    return "selector";
}

bool isDegradedSingBoxProfileGroupType(std::string type) {
    type = toLower(type);
    return type == "fallback" || type == "load-balance" || type == "loadbalance";
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

nlohmann::json valuesWithScalar(const std::string& value, const std::vector<std::string>& values) {
    nlohmann::json result = nlohmann::json::array();
    if (!value.empty()) {
        result.push_back(value);
    }
    for (const auto& item : values) {
        if (!item.empty()) {
            result.push_back(item);
        }
    }
    return result;
}

void appendSingBoxDnsServers(nlohmann::json& servers, const std::vector<std::string>& configured, const std::string& tagPrefix) {
    for (size_t i = 0; i < configured.size(); ++i) {
        if (configured[i].empty()) {
            continue;
        }
        const std::string tag = i == 0 ? tagPrefix : tagPrefix + "-" + std::to_string(i + 1);
        servers.push_back(
            {{"type", "udp"}, {"tag", tag}, {"server", configured[i]}, {"server_port", 53}}
        );
    }
}

void ensureSingBoxRuleSet(nlohmann::json& ruleSets, const AppConfig& config, const std::string& tag, const std::string& assetKey) {
    for (const auto& item : ruleSets) {
        if (item.is_object() && item.value("tag", "") == tag) {
            return;
        }
    }
    ruleSets.push_back({{"type", "local"}, {"tag", tag}, {"format", "binary"}, {"path", assetPathOrEmpty(config, assetKey)}});
}

void ensureSingBoxProfileRouting(nlohmann::json& root, const AppConfig& config, const ResolvedProfile& profile) {
    if (!root.contains("dns") || !root["dns"].is_object()) {
        root["dns"] = nlohmann::json::object();
    }
    root["dns"]["strategy"] = profile.dns.strategy.empty() ? "prefer_ipv4" : profile.dns.strategy;
    root["dns"]["servers"] = nlohmann::json::array();
    appendSingBoxDnsServers(root["dns"]["servers"], profile.dns.directServers, "dns-direct");
    appendSingBoxDnsServers(root["dns"]["servers"], profile.dns.remoteServers, "dns-remote");
    if (root["dns"]["servers"].empty()) {
        root["dns"]["servers"].push_back({{"type", "udp"}, {"tag", "dns-remote"}, {"server", "1.1.1.1"}, {"server_port", 53}});
    }
    root["dns"]["final"] = profile.dns.remoteServers.empty() ? "dns-direct" : "dns-remote";
    root["dns"]["rules"] = nlohmann::json::array();
    if (!profile.dns.directServers.empty()) {
        root["dns"]["rules"].push_back({{"domain", nlohmann::json::array({"private"})}, {"server", "dns-direct"}});
    }

    if (!root.contains("route") || !root["route"].is_object()) {
        root["route"] = nlohmann::json::object();
    }
    root["route"]["auto_detect_interface"] = true;
    root["route"]["default_domain_resolver"] = root["dns"]["final"];
    root["route"]["rules"] = nlohmann::json::array();
    root["route"]["rule_set"] = nlohmann::json::array();

    if (!hasSingBoxDnsDirectRule(root["route"]["rules"])) {
        root["route"]["rules"].push_back(
            {{"port", 53}, {"network", nlohmann::json::array({"udp"})}, {"action", "route"}, {"outbound", "DIRECT"}}
        );
    }

    std::string finalTarget;
    for (const auto& item : profile.rules) {
        const std::string type = toLower(item.type);
        const std::string value = toLower(item.value);
        if (type == "final" || type == "match") {
            finalTarget = !item.outbound.empty() ? item.outbound : item.value;
            continue;
        }
        if (item.outbound.empty()) {
            continue;
        }

        if (type == "geosite" && value == "cn") {
            ensureSingBoxRuleSet(root["route"]["rule_set"], config, "geosite-cn", "sing-box.geosite-cn");
            root["route"]["rules"].push_back({{"rule_set", nlohmann::json::array({"geosite-cn"})}, {"outbound", item.outbound}});
            continue;
        }
        if (type == "geoip" && value == "cn") {
            ensureSingBoxRuleSet(root["route"]["rule_set"], config, "geoip-cn", "sing-box.geoip-cn");
            root["route"]["rules"].push_back({{"rule_set", nlohmann::json::array({"geoip-cn"})}, {"outbound", item.outbound}});
            continue;
        }
        if (type == "geosite" && value == "private") {
            root["route"]["rules"].push_back({{"domain", nlohmann::json::array({"private"})}, {"outbound", item.outbound}});
            continue;
        }
        if (type == "geoip" && value == "private") {
            root["route"]["rules"].push_back({{"ip_cidr", nlohmann::json::array({"private"})}, {"outbound", item.outbound}});
            continue;
        }

        nlohmann::json rule = {{"outbound", item.outbound}};
        if (type == "domain") {
            rule["domain"] = valuesWithScalar(item.value, item.domains);
        } else if (type == "domain_suffix") {
            rule["domain_suffix"] = valuesWithScalar(item.value, item.domains);
        } else if (type == "domain_keyword") {
            rule["domain_keyword"] = valuesWithScalar(item.value, item.domains);
        } else if (type == "ip_cidr") {
            rule["ip_cidr"] = valuesWithScalar(item.value, item.ipCidrs);
        } else if (type == "port") {
            rule["port"] = valuesWithScalar(item.value, item.ports);
        } else if (type == "network") {
            rule["network"] = valuesWithScalar(item.value, item.networks);
        } else {
            continue;
        }
        if (rule.size() > 1) {
            root["route"]["rules"].push_back(rule);
        }
    }

    if (finalTarget.empty()) {
        finalTarget = profile.defaultOutbound.empty() ? "PROXY" : profile.defaultOutbound;
    }
    root["route"]["final"] = finalTarget;
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
        if (type == "geosite" && value == "private" && !item.outbound.empty()) {
            root["route"]["rules"].push_back({{"domain", nlohmann::json::array({"private"})}, {"outbound", item.outbound}});
            continue;
        }
        if (type == "geoip" && value == "private" && !item.outbound.empty()) {
            root["route"]["rules"].push_back({{"ip_cidr", nlohmann::json::array({"private"})}, {"outbound", item.outbound}});
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

void ensureSingBoxProfileRoute(nlohmann::json& root, const std::string& finalTarget) {
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
    root["route"]["auto_detect_interface"] = true;
    root["route"]["default_domain_resolver"] = "dns-remote";
    root["route"]["final"] = finalTarget;
}

} // namespace

ExportResult exportSingBoxImpl(
    const std::vector<ProxyNode>& nodes,
    const AppConfig& config,
    bool tun,
    const ResolvedProfile* profile,
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
    const bool useProfileGroups = profile != nullptr && !profile->groups.empty();

    auto managedSingBoxTags = generatedSingBoxTags(supported, groups);
    if (useProfileGroups) {
        managedSingBoxTags.insert("PROXY");
        managedSingBoxTags.insert("AUTO");
        for (const auto& configured : profile->groups) {
            if (!configured.tag.empty()) {
                managedSingBoxTags.insert(configured.tag);
            }
        }
    } else {
        for (const auto& configured : config.strategyGroups) {
            managedSingBoxTags.insert(configured.name);
        }
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

    auto addProfileGroup = [&](const std::string& tag,
                               const std::string& type,
                               const std::vector<std::string>& expandedMembers,
                               const std::string& url,
                               int interval,
                               const std::string& defaultMember) {
        const auto groupType = normalizeSingBoxGroupType(type);
        nlohmann::json members = nlohmann::json::array();
        for (const auto& member : expandedMembers) {
            members.push_back(member);
        }
        if (members.empty()) {
            members.push_back("DIRECT");
        }
        if (groupType == "urltest") {
            root["outbounds"].push_back(
                {{"type", "urltest"},
                 {"tag", tag},
                 {"url", url.empty() ? "http://www.gstatic.com/generate_204" : url},
                 {"interval", singBoxIntervalString(interval)},
                 {"outbounds", members}}
            );
        } else {
            nlohmann::json selector = {{"type", "selector"}, {"tag", tag}, {"outbounds", members}};
            if (!defaultMember.empty()) {
                selector["default"] = defaultMember;
            }
            root["outbounds"].push_back(selector);
        }
    };

    if (useProfileGroups) {
        std::set<std::string> renderedProfileGroups;
        std::set<std::string> referencedGeneratedRegions;
        bool proxyReferencesAuto = false;

        for (const auto& configured : profile->groups) {
            if (configured.tag.empty()) {
                continue;
            }
            if (isDegradedSingBoxProfileGroupType(configured.type)) {
                result.warnings.push_back({"profile_group_degraded", configured.tag + ": " + configured.type + " rendered as " + normalizeSingBoxGroupType(configured.type)});
            }
            const auto members = expandProfileMembers(configured.members, groups, supported);
            for (const auto& member : members) {
                if (groups.groups.count(member) > 0) {
                    referencedGeneratedRegions.insert(member);
                }
            }
            if (configured.tag == "PROXY") {
                for (const auto& member : members) {
                    if (member == "AUTO") {
                        proxyReferencesAuto = true;
                        break;
                    }
                }
            }
            addProfileGroup(configured.tag, configured.type, members, configured.url, configured.interval, configured.defaultMember);
            renderedProfileGroups.insert(configured.tag);
        }

        const bool hasProxy = renderedProfileGroups.count("PROXY") > 0;
        const bool hasAuto = renderedProfileGroups.count("AUTO") > 0;
        if (!hasAuto && (!hasProxy || proxyReferencesAuto)) {
            addProfileGroup("AUTO", "url-test", groups.groups.at("AUTO"), "http://www.gstatic.com/generate_204", 300, "");
            renderedProfileGroups.insert("AUTO");
        }
        if (!hasProxy) {
            std::vector<std::string> members;
            std::set<std::string> seen;
            auto addMember = [&](const std::string& value) {
                if (value.empty() || seen.count(value)) {
                    return;
                }
                seen.insert(value);
                members.push_back(value);
            };
            addMember("AUTO");
            addMember("DIRECT");
            for (const auto& nodeName : groups.groups.at("PROXY")) {
                addMember(nodeName);
            }
            addProfileGroup("PROXY", "select", members, "", 300, "AUTO");
        }
        for (const auto& region : groups.regionOrder) {
            if (renderedProfileGroups.count(region) > 0 || referencedGeneratedRegions.count(region) == 0) {
                continue;
            }
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
    } else if (!config.strategyGroups.empty()) {
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

    if (profile != nullptr) {
        ensureSingBoxProfileRouting(root, config, *profile);
    } else if (!config.routingRules.empty()) {
        ensureSingBoxCustomRoutingRules(root, config);
    } else if (config.profile == "bypass-cn") {
        ensureSingBoxBypassCnProfile(root, config);
    } else if (config.profile == "direct") {
        ensureSingBoxProfileRoute(root, "DIRECT");
    } else {
        ensureSingBoxProfileRoute(root, "PROXY");
    }

    result.ok = writeFile(outPath, root.dump(2), error);
    return result;
}

} // namespace subcli
