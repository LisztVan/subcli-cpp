#include "exporter_internal.hpp"

#include "subcli/capabilities.hpp"
#include "subcli/util.hpp"

namespace subcli {

namespace {

std::string normalizeMihomoGroupType(std::string type) {
    type = toLower(type);
    if (type == "url-test" || type == "urltest") {
        return "url-test";
    }
    if (type == "fallback") {
        return "fallback";
    }
    if (type == "load-balance" || type == "loadbalance") {
        return "load-balance";
    }
    return "select";
}

void ensureMihomoBypassCnProfile(YAML::Node& root) {
    if (!root["dns"] || !root["dns"].IsMap()) {
        root["dns"] = YAML::Node(YAML::NodeType::Map);
    }
    root["dns"]["enable"] = true;
    root["dns"]["enhanced-mode"] = "fake-ip";
    if (!root["dns"]["nameserver"] || !root["dns"]["nameserver"].IsSequence()) {
        root["dns"]["nameserver"] = YAML::Node(YAML::NodeType::Sequence);
        root["dns"]["nameserver"].push_back("https://dns.alidns.com/dns-query");
        root["dns"]["nameserver"].push_back("https://doh.pub/dns-query");
    }

    if (!root["rules"] || !root["rules"].IsSequence()) {
        root["rules"] = YAML::Node(YAML::NodeType::Sequence);
    }
    const std::vector<std::string> desired = {
        "GEOIP,LAN,DIRECT",
        "GEOSITE,private,DIRECT",
        "GEOSITE,cn,DIRECT",
        "GEOIP,CN,DIRECT",
        "MATCH,PROXY",
    };
    for (const auto& rule : desired) {
        if (!hasMihomoRule(root["rules"], rule)) {
            root["rules"].push_back(rule);
        }
    }
}

void ensureMihomoCustomRoutingRules(YAML::Node& root, const AppConfig& config) {
    if (!root["rules"] || !root["rules"].IsSequence()) {
        root["rules"] = YAML::Node(YAML::NodeType::Sequence);
    }
    for (const auto& rule : config.routingRules) {
        const std::string type = toLower(rule.type);
        std::string rendered;
        if (type == "geosite") {
            rendered = "GEOSITE," + rule.value + "," + rule.outbound;
        } else if (type == "geoip") {
            rendered = "GEOIP," + rule.value + "," + rule.outbound;
        } else if (type == "match" || type == "final") {
            rendered = "MATCH," + rule.outbound;
        } else {
            continue;
        }
        if (!hasMihomoRule(root["rules"], rendered)) {
            root["rules"].push_back(rendered);
        }
    }
}

void ensureMihomoProfileRules(YAML::Node& root, const std::string& finalTarget) {
    if (!root["rules"] || !root["rules"].IsSequence()) {
        root["rules"] = YAML::Node(YAML::NodeType::Sequence);
    }
    YAML::Node filtered(YAML::NodeType::Sequence);
    for (const auto& rule : root["rules"]) {
        const auto text = rule.as<std::string>("");
        if (text == "MATCH,PROXY" || text == "MATCH,DIRECT") {
            continue;
        }
        filtered.push_back(rule);
    }
    root["rules"] = filtered;
    const std::string rule = "MATCH," + finalTarget;
    if (!hasMihomoRule(root["rules"], rule)) {
        root["rules"].push_back(rule);
    }
}

} // namespace

ExportResult exportMihomoImpl(
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
        if (supportsNode(ExportTarget::Mihomo, node, reason)) {
            supported.push_back(node);
        } else {
            ++result.skipped;
            result.warnings.push_back({"unsupported_node", node.name + ": " + reason});
        }
    }
    if (supported.empty()) {
        error = "mihomo has no supported nodes after filtering";
        return result;
    }
    const auto groups = buildGroups(supported, config);
    const std::string tpl = resolveTemplatePath(
        config,
        tun ? config.templateTun.at("mihomo") : config.templateNormal.at("mihomo"),
        tun ? "mihomo_tun.yaml" : "mihomo_base.yaml"
    );
    if (!fileExists(tpl)) {
        error = "mihomo template not found: " + tpl;
        return result;
    }

    YAML::Node root = YAML::LoadFile(tpl);
    std::set<std::string> generatedNames;
    for (const auto& n : supported) {
        generatedNames.insert(n.name);
    }
    YAML::Node proxies(YAML::NodeType::Sequence);
    if (root["proxies"] && root["proxies"].IsSequence()) {
        for (const auto& proxy : root["proxies"]) {
            const auto name = proxy["name"].as<std::string>("");
            if (!name.empty() && generatedNames.count(name)) {
                continue;
            }
            proxies.push_back(proxy);
        }
    }
    for (const auto& n : supported) {
        proxies.push_back(makeMihomoProxy(n));
    }
    root["proxies"] = proxies;

    const bool useProfileGroups = profile != nullptr && !profile->groups.empty();

    YAML::Node proxyGroups(YAML::NodeType::Sequence);
    std::set<std::string> managedGroups = {"PROXY", "AUTO"};
    if (useProfileGroups) {
        for (const auto& group : profile->groups) {
            if (!group.tag.empty()) {
                managedGroups.insert(group.tag);
            }
        }
    } else if (!config.strategyGroups.empty()) {
        for (const auto& group : config.strategyGroups) {
            managedGroups.insert(group.name);
        }
    } else {
        for (const auto& region : groups.regionOrder) {
            managedGroups.insert(region);
        }
    }
    if (root["proxy-groups"] && root["proxy-groups"].IsSequence()) {
        for (const auto& group : root["proxy-groups"]) {
            const auto name = group["name"].as<std::string>("");
            if (!name.empty() && managedGroups.count(name)) {
                continue;
            }
            proxyGroups.push_back(group);
        }
    }

    auto addGroup = [&](const std::string& name, const std::vector<std::string>& members) {
        YAML::Node group;
        group["name"] = name;
        group["type"] = name == "AUTO" ? "url-test" : "select";
        if (name == "AUTO") {
            group["url"] = "http://www.gstatic.com/generate_204";
            group["interval"] = 300;
        }
        group["proxies"] = YAML::Node(YAML::NodeType::Sequence);
        if (name == "PROXY") {
            group["proxies"].push_back("AUTO");
            group["proxies"].push_back("DIRECT");
        }
        for (const auto& member : members) {
            group["proxies"].push_back(member);
        }
        if (name != "PROXY" && group["proxies"].size() == 0) {
            group["proxies"].push_back("DIRECT");
        }
        proxyGroups.push_back(group);
    };

    auto addProfileGroup = [&](const std::string& name,
                               const std::string& type,
                               const std::vector<std::string>& members,
                               const std::string& url,
                               int interval,
                               const std::string& strategy) {
        YAML::Node group;
        group["name"] = name;
        const auto groupType = normalizeMihomoGroupType(type);
        group["type"] = groupType;
        if (groupType == "url-test" || groupType == "fallback") {
            group["url"] = url.empty() ? "http://www.gstatic.com/generate_204" : url;
            group["interval"] = interval > 0 ? interval : 300;
        }
        if (groupType == "load-balance") {
            group["strategy"] = strategy.empty() ? "round-robin" : strategy;
        }
        group["proxies"] = YAML::Node(YAML::NodeType::Sequence);
        for (const auto& member : members) {
            group["proxies"].push_back(member);
        }
        if (group["proxies"].size() == 0) {
            group["proxies"].push_back("DIRECT");
        }
        proxyGroups.push_back(group);
    };

    if (useProfileGroups) {
        std::set<std::string> renderedProfileGroups;
        bool proxyReferencesAuto = false;

        for (const auto& configured : profile->groups) {
            if (configured.tag.empty()) {
                continue;
            }
            const auto members = expandProfileMembers(configured.members, groups, supported);
            if (configured.tag == "PROXY") {
                for (const auto& member : members) {
                    if (member == "AUTO") {
                        proxyReferencesAuto = true;
                        break;
                    }
                }
            }
            addProfileGroup(configured.tag, configured.type, members, configured.url, configured.interval, configured.strategy);
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
            addProfileGroup("PROXY", "select", members, "", 300, "");
        }
    } else if (!config.strategyGroups.empty()) {
        for (const auto& configured : config.strategyGroups) {
            YAML::Node group;
            group["name"] = configured.name;
            const auto groupType = normalizeMihomoGroupType(configured.type);
            group["type"] = groupType;
            if (groupType == "url-test" || groupType == "fallback") {
                group["url"] = configured.url.empty() ? "http://www.gstatic.com/generate_204" : configured.url;
                group["interval"] = configured.interval > 0 ? configured.interval : 300;
            }
            if (groupType == "load-balance") {
                group["strategy"] = "round-robin";
            }
            group["proxies"] = YAML::Node(YAML::NodeType::Sequence);
            for (const auto& member : configured.members) {
                group["proxies"].push_back(member);
            }
            if (group["proxies"].size() == 0) {
                group["proxies"].push_back("DIRECT");
            }
            proxyGroups.push_back(group);
        }
    } else {
        addGroup("PROXY", groups.groups.at("PROXY"));
        addGroup("AUTO", groups.groups.at("AUTO"));
        for (const auto& region : groups.regionOrder) {
            addGroup(region, groups.groups.at(region));
        }
    }
    root["proxy-groups"] = proxyGroups;

    if (!config.routingRules.empty()) {
        ensureMihomoCustomRoutingRules(root, config);
    } else if (config.profile == "bypass-cn") {
        ensureMihomoBypassCnProfile(root);
    } else if (config.profile == "direct") {
        ensureMihomoProfileRules(root, "DIRECT");
    } else {
        ensureMihomoProfileRules(root, "PROXY");
    }

    YAML::Emitter emitter;
    emitter << root;
    result.ok = writeFile(outPath, emitter.c_str(), error);
    return result;
}

} // namespace subcli
