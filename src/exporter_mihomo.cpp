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

void setMihomoStringSequence(YAML::Node& parent, const std::string& key, const std::vector<std::string>& values) {
    YAML::Node sequence(YAML::NodeType::Sequence);
    for (const auto& value : values) {
        if (!value.empty()) {
            sequence.push_back(value);
        }
    }
    parent[key] = sequence;
}

void addMihomoRule(YAML::Node& rules, std::set<std::string>& seen, const std::string& rendered) {
    if (rendered.empty() || seen.count(rendered)) {
        return;
    }
    seen.insert(rendered);
    rules.push_back(rendered);
}

std::vector<std::string> valuesOrScalar(const std::vector<std::string>& values, const std::string& scalar) {
    if (!values.empty()) {
        return values;
    }
    if (!scalar.empty()) {
        return {scalar};
    }
    return {};
}

void applyMihomoSequencePolicy(
    YAML::Node& parent,
    const std::string& key,
    const YAML::Node* templateValue,
    bool templateHad,
    const YAML::Node& generatedValue,
    TemplatePolicyAction action,
    const std::string& path,
    const std::string& mergeKey,
    std::vector<DiagnosticMessage>& warnings
) {
    if (action == TemplatePolicyAction::Reject) {
        if (templateHad && templateValue != nullptr) {
            parent[key] = *templateValue;
            addTemplatePolicyRejectWarning(warnings, path);
        } else {
            parent.remove(key);
        }
        return;
    }

    if (action == TemplatePolicyAction::Replace) {
        parent[key] = generatedValue;
        return;
    }

    if (action == TemplatePolicyAction::Append) {
        if (!templateHad || templateValue == nullptr || !templateValue->IsSequence() || !generatedValue.IsSequence()) {
            parent[key] = generatedValue;
            return;
        }
        YAML::Node merged(YAML::NodeType::Sequence);
        for (const auto& item : *templateValue) {
            merged.push_back(item);
        }
        for (const auto& item : generatedValue) {
            merged.push_back(item);
        }
        parent[key] = merged;
        return;
    }

    if (action == TemplatePolicyAction::Merge) {
        if (!templateHad || templateValue == nullptr || !templateValue->IsSequence() || !generatedValue.IsSequence()) {
            parent[key] = generatedValue;
            return;
        }
        if (mergeKey.empty()) {
            YAML::Node merged(YAML::NodeType::Sequence);
            for (const auto& item : *templateValue) {
                merged.push_back(item);
            }
            for (const auto& item : generatedValue) {
                merged.push_back(item);
            }
            parent[key] = merged;
            return;
        }

        YAML::Node merged(YAML::NodeType::Sequence);
        std::map<std::string, size_t> indexByKey;
        for (const auto item : *templateValue) {
            YAML::Node itemNode = item;
            if (itemNode.IsMap() && itemNode[mergeKey] && itemNode[mergeKey].IsScalar()) {
                indexByKey[itemNode[mergeKey].as<std::string>()] = merged.size();
            }
            merged.push_back(itemNode);
        }
        for (const auto item : generatedValue) {
            YAML::Node itemNode = item;
            if (itemNode.IsMap() && itemNode[mergeKey] && itemNode[mergeKey].IsScalar()) {
                const std::string value = itemNode[mergeKey].as<std::string>();
                const auto existing = indexByKey.find(value);
                if (existing != indexByKey.end()) {
                    merged[existing->second] = itemNode;
                } else {
                    indexByKey[value] = merged.size();
                    merged.push_back(itemNode);
                }
            } else {
                merged.push_back(itemNode);
            }
        }
        parent[key] = merged;
    }
}

void applyMihomoMapPolicy(
    YAML::Node& parent,
    const std::string& key,
    const YAML::Node* templateValue,
    bool templateHad,
    const YAML::Node& generatedValue,
    TemplatePolicyAction action,
    const std::string& path,
    std::vector<DiagnosticMessage>& warnings
) {
    if (action == TemplatePolicyAction::Reject) {
        if (templateHad && templateValue != nullptr) {
            parent[key] = *templateValue;
            addTemplatePolicyRejectWarning(warnings, path);
        } else {
            parent.remove(key);
        }
        return;
    }
    if (action == TemplatePolicyAction::Replace) {
        parent[key] = generatedValue;
        return;
    }
    if (action == TemplatePolicyAction::Merge) {
        if (!templateHad || templateValue == nullptr || !templateValue->IsMap() || !generatedValue.IsMap()) {
            parent[key] = generatedValue;
            return;
        }
        YAML::Node merged(YAML::NodeType::Map);
        for (const auto item : *templateValue) {
            merged[item.first.as<std::string>()] = item.second;
        }
        for (const auto item : generatedValue) {
            merged[item.first.as<std::string>()] = item.second;
        }
        parent[key] = merged;
    }
}

void applyMihomoProfileDns(YAML::Node& root, const ResolvedProfile& profile) {
    if (!root["dns"] || !root["dns"].IsMap()) {
        root["dns"] = YAML::Node(YAML::NodeType::Map);
    }
    YAML::Node dns = root["dns"];
    dns["enable"] = true;
    if (!profile.dns.mode.empty()) {
        dns["enhanced-mode"] = profile.dns.mode;
    }
    if (!profile.dns.directServers.empty()) {
        setMihomoStringSequence(dns, "nameserver", profile.dns.directServers);
    }
    if (!profile.dns.remoteServers.empty()) {
        setMihomoStringSequence(dns, "fallback", profile.dns.remoteServers);
    }
}

void applyMihomoProfileRules(YAML::Node& root, const ResolvedProfile& profile) {
    YAML::Node rules(YAML::NodeType::Sequence);
    std::set<std::string> seen;
    for (const auto& rule : profile.rules) {
        const std::string type = toLower(rule.type);
        const auto outbound = rule.outbound.empty() && type == "final" ? profile.defaultOutbound : rule.outbound;
        if (type == "geosite" && !rule.value.empty()) {
            addMihomoRule(rules, seen, "GEOSITE," + rule.value + "," + rule.outbound);
        } else if (type == "geoip" && !rule.value.empty()) {
            addMihomoRule(rules, seen, "GEOIP," + rule.value + "," + rule.outbound);
        } else if (type == "domain") {
            for (const auto& domain : valuesOrScalar(rule.domains, rule.value)) {
                addMihomoRule(rules, seen, "DOMAIN," + domain + "," + rule.outbound);
            }
        } else if (type == "domain_suffix") {
            for (const auto& domain : valuesOrScalar(rule.domains, rule.value)) {
                addMihomoRule(rules, seen, "DOMAIN-SUFFIX," + domain + "," + rule.outbound);
            }
        } else if (type == "domain_keyword") {
            for (const auto& domain : valuesOrScalar(rule.domains, rule.value)) {
                addMihomoRule(rules, seen, "DOMAIN-KEYWORD," + domain + "," + rule.outbound);
            }
        } else if (type == "ip_cidr") {
            for (const auto& ipCidr : valuesOrScalar(rule.ipCidrs, rule.value)) {
                addMihomoRule(rules, seen, "IP-CIDR," + ipCidr + "," + rule.outbound);
            }
        } else if (type == "port") {
            for (const auto& port : valuesOrScalar(rule.ports, rule.value)) {
                addMihomoRule(rules, seen, "DST-PORT," + port + "," + rule.outbound);
            }
        } else if (type == "network") {
            for (const auto& network : valuesOrScalar(rule.networks, rule.value)) {
                addMihomoRule(rules, seen, "NETWORK," + network + "," + rule.outbound);
            }
        } else if (type == "final" && !outbound.empty()) {
            addMihomoRule(rules, seen, "MATCH," + outbound);
        }
    }
    if (rules.size() == 0) {
        addMihomoRule(rules, seen, "MATCH," + (profile.defaultOutbound.empty() ? std::string("PROXY") : profile.defaultOutbound));
    }
    root["rules"] = rules;
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
    for (auto node : makeExportNodes(prepared)) {
        if (node.tlsConfig.fingerprint.empty() && !node.fingerprint.empty()) {
            node.tlsConfig.fingerprint = node.fingerprint;
        }
        std::string reason;
        if (supportsNode(ExportTarget::Mihomo, node, reason)) {
            supported.push_back(node);
        } else {
            ++result.skipped;
            result.warnings.push_back({"capability_unsupported", node.name + ": " + reason});
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
    YAML::Node proxiesTemplate;
    const bool hasProxiesTemplate = root["proxies"] && root["proxies"].IsSequence();
    if (hasProxiesTemplate) {
        proxiesTemplate = YAML::Clone(root["proxies"]);
    }
    YAML::Node proxyGroupsTemplate;
    const bool hasProxyGroupsTemplate = root["proxy-groups"] && root["proxy-groups"].IsSequence();
    if (hasProxyGroupsTemplate) {
        proxyGroupsTemplate = YAML::Clone(root["proxy-groups"]);
    }
    YAML::Node rulesTemplate;
    const bool hasRulesTemplate = root["rules"] && root["rules"].IsSequence();
    if (hasRulesTemplate) {
        rulesTemplate = YAML::Clone(root["rules"]);
    }
    YAML::Node dnsTemplate;
    const bool hasDnsTemplate = root["dns"] && root["dns"].IsMap();
    if (hasDnsTemplate) {
        dnsTemplate = YAML::Clone(root["dns"]);
    }
    YAML::Node dnsNameserverTemplate;
    bool hasDnsNameserverTemplate = false;
    YAML::Node dnsFallbackTemplate;
    bool hasDnsFallbackTemplate = false;
    if (hasDnsTemplate && dnsTemplate["nameserver"]) {
        dnsNameserverTemplate = YAML::Clone(dnsTemplate["nameserver"]);
        hasDnsNameserverTemplate = true;
    }
    if (hasDnsTemplate && dnsTemplate["fallback"]) {
        dnsFallbackTemplate = YAML::Clone(dnsTemplate["fallback"]);
        hasDnsFallbackTemplate = true;
    }
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

    if (profile != nullptr) {
        applyMihomoProfileDns(root, *profile);
        applyMihomoProfileRules(root, *profile);

        TemplatePolicyAction explicitAction;
        if (getExplicitTemplatePolicyAction(ExportTarget::Mihomo, profile, "proxies", explicitAction)) {
            applyMihomoSequencePolicy(root, "proxies", hasProxiesTemplate ? &proxiesTemplate : nullptr, hasProxiesTemplate, root["proxies"], explicitAction, "proxies", "name", result.warnings);
        }
        if (getExplicitTemplatePolicyAction(ExportTarget::Mihomo, profile, "proxy-groups", explicitAction)) {
            applyMihomoSequencePolicy(root, "proxy-groups", hasProxyGroupsTemplate ? &proxyGroupsTemplate : nullptr, hasProxyGroupsTemplate, root["proxy-groups"], explicitAction, "proxy-groups", "name", result.warnings);
        }
        if (getExplicitTemplatePolicyAction(ExportTarget::Mihomo, profile, "rules", explicitAction)) {
            applyMihomoSequencePolicy(root, "rules", hasRulesTemplate ? &rulesTemplate : nullptr, hasRulesTemplate, root["rules"], explicitAction, "rules", "", result.warnings);
        }
        if (getExplicitTemplatePolicyAction(ExportTarget::Mihomo, profile, "dns", explicitAction)) {
            applyMihomoMapPolicy(root, "dns", hasDnsTemplate ? &dnsTemplate : nullptr, hasDnsTemplate, root["dns"], explicitAction, "dns", result.warnings);
        }
        if (!root["dns"] || !root["dns"].IsMap()) {
            root["dns"] = YAML::Node(YAML::NodeType::Map);
        }
        if (getExplicitTemplatePolicyAction(ExportTarget::Mihomo, profile, "dns.nameserver", explicitAction)) {
            YAML::Node dnsRoot = root["dns"];
            applyMihomoSequencePolicy(dnsRoot, "nameserver", hasDnsNameserverTemplate ? &dnsNameserverTemplate : nullptr, hasDnsNameserverTemplate, dnsRoot["nameserver"], explicitAction, "dns.nameserver", "", result.warnings);
        }
        if (getExplicitTemplatePolicyAction(ExportTarget::Mihomo, profile, "dns.fallback", explicitAction)) {
            YAML::Node dnsRoot = root["dns"];
            applyMihomoSequencePolicy(dnsRoot, "fallback", hasDnsFallbackTemplate ? &dnsFallbackTemplate : nullptr, hasDnsFallbackTemplate, dnsRoot["fallback"], explicitAction, "dns.fallback", "", result.warnings);
        }
    } else if (!config.routingRules.empty()) {
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
