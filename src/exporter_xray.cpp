#include "exporter_internal.hpp"

#include <iomanip>
#include <map>
#include <set>
#include <sstream>

#include "subcli/capabilities.hpp"
#include "subcli/util.hpp"

namespace subcli {

namespace {

constexpr const char* kManagedTagPrefix = "SUBCLI_";

std::string makeManagedTag(size_t index) {
    std::ostringstream oss;
    oss << kManagedTagPrefix << std::setw(5) << std::setfill('0') << index;
    return oss.str();
}

void removeJsonArrayObjectsByTagPrefix(nlohmann::json& array, const std::string& prefix) {
    if (!array.is_array()) {
        array = nlohmann::json::array();
        return;
    }
    nlohmann::json kept = nlohmann::json::array();
    for (const auto& item : array) {
        if (!item.is_object()) {
            kept.push_back(item);
            continue;
        }
        const std::string tag = item.value("tag", "");
        if (!tag.empty() && tag.rfind(prefix, 0) == 0) {
            continue;
        }
        kept.push_back(item);
    }
    array = kept;
}

std::string detectXrayStrategy(const nlohmann::json& balancers) {
    if (!balancers.is_array()) {
        return "leastPing";
    }
    for (const auto& balancer : balancers) {
        if (!balancer.is_object() || balancer.value("tag", "") != "PROXY") {
            continue;
        }
        if (!balancer.contains("strategy") || !balancer["strategy"].is_object()) {
            continue;
        }
        const auto strategyType = balancer["strategy"].value("type", "");
        if (strategyType == "leastLoad") {
            return "leastLoad";
        }
    }
    return "leastPing";
}

void ensureObservatoryForStrategy(nlohmann::json& root, const std::string& strategy) {
    if (strategy == "leastLoad") {
        if (!root.contains("burstObservatory") || !root["burstObservatory"].is_object()) {
            root["burstObservatory"] = {
                {"subjectSelector", nlohmann::json::array({kManagedTagPrefix})},
                {"pingConfig",
                 {{"destination", "https://www.google.com/generate_204"}, {"interval", "30s"}, {"sampling", 10}, {"timeout", "5s"}, {"httpMethod", "HEAD"}}}
            };
        }
        return;
    }

    if (!root.contains("observatory") || !root["observatory"].is_object()) {
        root["observatory"] = {
            {"subjectSelector", nlohmann::json::array({kManagedTagPrefix})},
            {"probeUrl", "https://www.google.com/generate_204"},
            {"probeInterval", "30s"},
            {"enableConcurrency", true}
        };
    }
}

bool hasXrayDirectCnRule(const nlohmann::json& rules) {
    if (!rules.is_array()) {
        return false;
    }
    for (const auto& rule : rules) {
        if (!rule.is_object() || rule.value("outboundTag", "") != "DIRECT") {
            continue;
        }
        const auto domainText = rule.contains("domain") ? rule["domain"].dump() : "";
        const auto ipText = rule.contains("ip") ? rule["ip"].dump() : "";
        if (domainText.find("geosite:cn") != std::string::npos || ipText.find("geoip:cn") != std::string::npos) {
            return true;
        }
    }
    return false;
}

nlohmann::json xrayStringValues(const std::string& value, const std::vector<std::string>& values, const std::string& prefix = "") {
    nlohmann::json result = nlohmann::json::array();
    auto add = [&](const std::string& item) {
        if (!item.empty()) {
            result.push_back(prefix + item);
        }
    };
    add(value);
    for (const auto& item : values) {
        add(item);
    }
    return result;
}

std::string xrayJoinedValues(const std::string& value, const std::vector<std::string>& values) {
    std::vector<std::string> parts;
    if (!value.empty()) {
        parts.push_back(value);
    }
    for (const auto& item : values) {
        if (!item.empty()) {
            parts.push_back(item);
        }
    }
    std::ostringstream oss;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) {
            oss << ',';
        }
        oss << parts[i];
    }
    return oss.str();
}

std::string xrayQueryStrategy(std::string strategy) {
    strategy = toLower(strategy);
    if (strategy == "prefer_ipv6" || strategy == "ipv6_only" || strategy == "use_ipv6" || strategy == "useipv6") {
        return "UseIPv6";
    }
    if (strategy == "prefer_ipv4" || strategy == "ipv4_only" || strategy == "use_ipv4" || strategy == "useipv4") {
        return "UseIPv4";
    }
    return "UseIP";
}

void setXrayRuleTarget(nlohmann::json& rule, const std::string& target, const std::set<std::string>& balancerTags) {
    if (balancerTags.count(target)) {
        rule["balancerTag"] = target;
    } else {
        rule["outboundTag"] = target;
    }
}

std::string xrayGroupStrategy(std::string type) {
    type = toLower(type);
    if (type == "load-balance" || type == "loadbalance") {
        return "leastLoad";
    }
    return "leastPing";
}

bool isXrayFallbackGroup(std::string type) {
    type = toLower(type);
    return type == "fallback";
}

bool isXraySelectGroup(std::string type) {
    type = toLower(type);
    return type == "select" || type.empty();
}

void appendUniqueString(std::vector<std::string>& values, std::set<std::string>& seen, const std::string& value) {
    if (!value.empty() && seen.insert(value).second) {
        values.push_back(value);
    }
}

std::vector<std::string> expandXrayProfileMembers(
    const std::vector<std::string>& rawMembers,
    const GroupData& profileGroups,
    const GroupData& managedGroups,
    const std::vector<ProxyNode>& exportNodes,
    const std::map<std::string, std::string>& managedTagByName,
    const std::map<std::string, std::vector<std::string>>& profileMemberMap,
    const std::set<std::string>& validLiteralTags,
    std::vector<std::string>& unresolvedMembers,
    std::set<std::string>& visiting
) {
    const auto expanded = expandProfileMembers(rawMembers, profileGroups, exportNodes);
    std::vector<std::string> out;
    std::set<std::string> seen;
    for (const auto& member : expanded) {
        const auto nodeIt = managedTagByName.find(member);
        if (nodeIt != managedTagByName.end()) {
            appendUniqueString(out, seen, nodeIt->second);
            continue;
        }
        const auto managedIt = managedGroups.groups.find(member);
        if (managedIt != managedGroups.groups.end()) {
            for (const auto& tag : managedIt->second) {
                appendUniqueString(out, seen, tag);
            }
            continue;
        }
        const auto profileIt = profileMemberMap.find(member);
        if (profileIt != profileMemberMap.end() && !visiting.count(member)) {
            visiting.insert(member);
            const auto nested = expandXrayProfileMembers(profileIt->second, profileGroups, managedGroups, exportNodes, managedTagByName, profileMemberMap, validLiteralTags, unresolvedMembers, visiting);
            visiting.erase(member);
            for (const auto& tag : nested) {
                appendUniqueString(out, seen, tag);
            }
            continue;
        }
        if (validLiteralTags.count(member)) {
            appendUniqueString(out, seen, member);
            continue;
        }
        unresolvedMembers.push_back(member);
    }
    return out;
}

std::vector<std::string> resolveXrayFallbackMember(
    const std::string& member,
    const GroupData& profileGroups,
    const GroupData& managedGroups,
    const std::vector<ProxyNode>& exportNodes,
    const std::map<std::string, std::string>& managedTagByName,
    const std::set<std::string>& validLiteralTags,
    std::vector<std::string>& unresolvedMembers
) {
    std::vector<std::string> out;
    std::set<std::string> seen;
    if (member.empty()) {
        return out;
    }
    const auto nodeIt = managedTagByName.find(member);
    if (nodeIt != managedTagByName.end()) {
        appendUniqueString(out, seen, nodeIt->second);
        return out;
    }
    const auto expanded = expandProfileMembers({member}, profileGroups, exportNodes);
    for (const auto& item : expanded) {
        const auto expandedNodeIt = managedTagByName.find(item);
        if (expandedNodeIt != managedTagByName.end()) {
            appendUniqueString(out, seen, expandedNodeIt->second);
            continue;
        }
        const auto managedIt = managedGroups.groups.find(item);
        if (managedIt != managedGroups.groups.end()) {
            for (const auto& tag : managedIt->second) {
                appendUniqueString(out, seen, tag);
            }
            continue;
        }
        if (validLiteralTags.count(item)) {
            appendUniqueString(out, seen, item);
            continue;
        }
        unresolvedMembers.push_back(item);
    }
    return out;
}

void ensureXrayProfileRouting(
    nlohmann::json& root,
    const ResolvedProfile& profile,
    const std::set<std::string>& balancerTags
) {
    if (!root.contains("dns") || !root["dns"].is_object()) {
        root["dns"] = nlohmann::json::object();
    }
    root["dns"]["queryStrategy"] = xrayQueryStrategy(profile.dns.strategy);
    root["dns"]["servers"] = nlohmann::json::array();
    for (const auto& server : profile.dns.directServers) {
        if (!server.empty()) {
            root["dns"]["servers"].push_back(server);
        }
    }
    for (const auto& server : profile.dns.remoteServers) {
        if (!server.empty()) {
            root["dns"]["servers"].push_back(server);
        }
    }
    if (root["dns"]["servers"].empty()) {
        root["dns"]["servers"].push_back("1.1.1.1");
    }

    if (!root.contains("routing") || !root["routing"].is_object()) {
        root["routing"] = nlohmann::json::object();
    }
    root["routing"]["rules"] = nlohmann::json::array();
    root["routing"]["domainStrategy"] = "IPIfNonMatch";

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

        nlohmann::json rule = {{"type", "field"}};
        if (type == "geosite" && !value.empty()) {
            rule["domain"] = nlohmann::json::array({"geosite:" + value});
        } else if (type == "geoip" && !value.empty()) {
            rule["ip"] = nlohmann::json::array({"geoip:" + value});
        } else if (type == "domain") {
            rule["domain"] = xrayStringValues(item.value, item.domains, "full:");
        } else if (type == "domain_suffix") {
            rule["domain"] = xrayStringValues(item.value, item.domains, "domain:");
        } else if (type == "domain_keyword") {
            rule["domain"] = xrayStringValues(item.value, item.domains, "keyword:");
        } else if (type == "ip_cidr") {
            rule["ip"] = xrayStringValues(item.value, item.ipCidrs);
        } else if (type == "port") {
            const auto ports = xrayJoinedValues(item.value, item.ports);
            if (!ports.empty()) {
                rule["port"] = ports;
            }
        } else if (type == "network") {
            const auto networks = xrayJoinedValues(item.value, item.networks);
            if (!networks.empty()) {
                rule["network"] = networks;
            }
        } else {
            continue;
        }
        if (rule.size() <= 1) {
            continue;
        }
        setXrayRuleTarget(rule, item.outbound, balancerTags);
        root["routing"]["rules"].push_back(rule);
    }

    if (finalTarget.empty()) {
        finalTarget = profile.defaultOutbound.empty() ? "PROXY" : profile.defaultOutbound;
    }
    nlohmann::json finalRule = {{"type", "field"}, {"network", "tcp,udp"}};
    setXrayRuleTarget(finalRule, finalTarget, balancerTags);
    root["routing"]["rules"].push_back(finalRule);
}

void appendXrayBalancer(nlohmann::json& balancers, const std::string& tag, const std::vector<std::string>& members, const std::string& strategy) {
    nlohmann::json selector = nlohmann::json::array();
    for (const auto& member : members) {
        if (!member.empty()) {
            selector.push_back(member);
        }
    }
    if (selector.empty()) {
        selector.push_back("DIRECT");
    }
    balancers.push_back({{"tag", tag}, {"selector", selector}, {"strategy", {{"type", strategy}}}});
}

void ensureXrayBypassCnProfile(nlohmann::json& root) {
    if (!root.contains("dns") || !root["dns"].is_object()) {
        root["dns"] = nlohmann::json::object();
    }
    root["dns"]["queryStrategy"] = "UseIPv4";
    root["dns"]["servers"] = nlohmann::json::array({"223.5.5.5", "119.29.29.29", "1.1.1.1"});

    if (!root.contains("routing") || !root["routing"].is_object()) {
        root["routing"] = nlohmann::json::object();
    }
    root["routing"]["domainStrategy"] = "IPIfNonMatch";
    if (!root["routing"].contains("rules") || !root["routing"]["rules"].is_array()) {
        root["routing"]["rules"] = nlohmann::json::array();
    }
    if (!hasXrayDirectCnRule(root["routing"]["rules"])) {
        nlohmann::json rule = {
            {"type", "field"},
            {"domain", nlohmann::json::array({"geosite:private", "geosite:cn"})},
            {"ip", nlohmann::json::array({"geoip:private", "geoip:cn"})},
            {"outboundTag", "DIRECT"}
        };
        root["routing"]["rules"].insert(root["routing"]["rules"].begin(), rule);
    }
}

std::string customXrayFinalOutbound(const AppConfig& config) {
    for (const auto& item : config.routingRules) {
        const std::string type = toLower(item.type);
        if ((type == "final" || type == "match") && !item.outbound.empty()) {
            return item.outbound;
        }
    }
    return "";
}

void ensureXrayCustomRoutingRules(nlohmann::json& root, const AppConfig& config) {
    if (!root.contains("routing") || !root["routing"].is_object()) {
        root["routing"] = nlohmann::json::object();
    }
    if (!root["routing"].contains("rules") || !root["routing"]["rules"].is_array()) {
        root["routing"]["rules"] = nlohmann::json::array();
    }
    for (const auto& item : config.routingRules) {
        const std::string type = toLower(item.type);
        const std::string value = toLower(item.value);
        if (type == "geosite" && !value.empty() && !item.outbound.empty()) {
            root["routing"]["rules"].push_back(
                {{"type", "field"}, {"domain", nlohmann::json::array({"geosite:" + value})}, {"outboundTag", item.outbound}}
            );
        } else if (type == "geoip" && !value.empty() && !item.outbound.empty()) {
            root["routing"]["rules"].push_back(
                {{"type", "field"}, {"ip", nlohmann::json::array({"geoip:" + value})}, {"outboundTag", item.outbound}}
            );
        }
    }
}

void ensureXrayProfileRoutingRoot(nlohmann::json& root) {
    if (!root.contains("routing") || !root["routing"].is_object()) {
        root["routing"] = nlohmann::json::object();
    }
    if (!root["routing"].contains("rules") || !root["routing"]["rules"].is_array()) {
        root["routing"]["rules"] = nlohmann::json::array();
    }
}

} // namespace

ExportResult exportXrayImpl(
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
        if (supportsNode(ExportTarget::Xray, node, reason)) {
            supported.push_back(node);
        } else {
            ++result.skipped;
            result.warnings.push_back({"unsupported_node", node.name + ": " + reason});
        }
    }
    if (supported.empty()) {
        error = "xray has no supported nodes after filtering";
        return result;
    }

    std::vector<ProxyNode> managed = supported;
    for (size_t i = 0; i < managed.size(); ++i) {
        managed[i].name = makeManagedTag(i + 1);
    }
    const auto profileGroups = buildGroups(supported, config);
    const auto managedGroups = buildGroups(managed, config);

    const std::string tpl = resolveTemplatePath(
        config,
        tun ? config.templateTun.at("xray") : config.templateNormal.at("xray"),
        tun ? "xray_tun.json" : "xray_base.json"
    );
    if (!fileExists(tpl)) {
        error = "xray template not found: " + tpl;
        return result;
    }

    auto root = nlohmann::json::parse(readFile(tpl), nullptr, false);
    if (root.is_discarded()) {
        error = "invalid xray template json";
        return result;
    }
    if (!root.contains("outbounds") || !root["outbounds"].is_array()) {
        root["outbounds"] = nlohmann::json::array();
    }
    removeJsonArrayObjectsByTag(root["outbounds"], generatedXrayTags(supported));
    removeJsonArrayObjectsByTagPrefix(root["outbounds"], kManagedTagPrefix);
    for (const auto& n : managed) {
        root["outbounds"].push_back(makeXrayOutbound(n));
    }

    if (!root.contains("routing") || !root["routing"].is_object()) {
        root["routing"] = nlohmann::json::object();
    }
    if (!root["routing"].contains("balancers") || !root["routing"]["balancers"].is_array()) {
        root["routing"]["balancers"] = nlohmann::json::array();
    }

    std::set<std::string> managedBalancerTags = {"PROXY"};
    if (profile != nullptr) {
        for (const auto& group : profile->groups) {
            if (!group.tag.empty()) {
                managedBalancerTags.insert(group.tag);
            }
        }
        removeJsonArrayObjectsByTag(root["routing"]["balancers"], managedBalancerTags);

        std::map<std::string, std::string> managedTagByName;
        for (size_t i = 0; i < supported.size(); ++i) {
            managedTagByName[supported[i].name] = managed[i].name;
        }
        std::map<std::string, std::vector<std::string>> profileMemberMap;
        for (const auto& group : profile->groups) {
            if (!group.tag.empty()) {
                profileMemberMap[group.tag] = group.members;
            }
        }

        std::set<std::string> renderedProfileGroups;
        bool proxyReferencesAuto = false;
        std::set<std::string> validLiteralTags = {"DIRECT", "REJECT"};
        for (const auto& group : profile->groups) {
            if (!group.tag.empty()) {
                validLiteralTags.insert(group.tag);
            }
        }
        for (const auto& group : profile->groups) {
            if (group.tag.empty()) {
                continue;
            }
            std::set<std::string> visiting = {group.tag};
            std::vector<std::string> unresolvedMembers;
            auto members = expandXrayProfileMembers(group.members, profileGroups, managedGroups, supported, managedTagByName, profileMemberMap, validLiteralTags, unresolvedMembers, visiting);
            for (const auto& member : unresolvedMembers) {
                result.warnings.push_back({"profile_group_degraded", group.tag + ": unresolved member " + member + " omitted"});
            }
            if (members.empty()) {
                for (const auto& tag : managedGroups.groups.at("PROXY")) {
                    members.push_back(tag);
                }
                if (members.empty()) {
                    members.push_back("DIRECT");
                }
                result.warnings.push_back({"profile_group_degraded", group.tag + ": no resolvable members, using safe fallback selector"});
            }
            nlohmann::json selector = nlohmann::json::array();
            for (const auto& member : members) {
                selector.push_back(member);
                if (group.tag == "PROXY" && member == "AUTO") {
                    proxyReferencesAuto = true;
                }
            }
            nlohmann::json balancer = {{"tag", group.tag}, {"selector", selector}, {"strategy", {{"type", xrayGroupStrategy(group.type)}}}};
            const std::string type = toLower(group.type);
            if (isXraySelectGroup(type)) {
                result.warnings.push_back({"profile_group_degraded", group.tag + ": select rendered as leastPing balancer"});
            }
            if (isXrayFallbackGroup(type)) {
                if (!group.defaultMember.empty()) {
                    std::vector<std::string> unresolvedFallback;
                    const auto fallbackTags = resolveXrayFallbackMember(group.defaultMember, profileGroups, managedGroups, supported, managedTagByName, validLiteralTags, unresolvedFallback);
                    for (const auto& member : unresolvedFallback) {
                        result.warnings.push_back({"profile_group_degraded", group.tag + ": unresolved fallback default " + member + " omitted"});
                    }
                    if (!fallbackTags.empty()) {
                        balancer["fallbackTag"] = fallbackTags.front();
                    } else {
                        result.warnings.push_back({"profile_group_degraded", group.tag + ": fallback rendered as leastPing without fallbackTag"});
                    }
                } else {
                    result.warnings.push_back({"profile_group_degraded", group.tag + ": fallback rendered as leastPing without fallbackTag"});
                }
            }
            root["routing"]["balancers"].push_back(balancer);
            renderedProfileGroups.insert(group.tag);
        }
        const bool hasAuto = renderedProfileGroups.count("AUTO") > 0;
        const bool hasProxy = renderedProfileGroups.count("PROXY") > 0;
        if (!hasAuto && (!hasProxy || proxyReferencesAuto)) {
            appendXrayBalancer(root["routing"]["balancers"], "AUTO", managedGroups.groups.at("AUTO"), "leastPing");
            managedBalancerTags.insert("AUTO");
        }
        if (!hasProxy) {
            std::vector<std::string> proxyMembers;
            std::set<std::string> seen;
            appendUniqueString(proxyMembers, seen, "AUTO");
            appendUniqueString(proxyMembers, seen, "DIRECT");
            for (const auto& tag : managedGroups.groups.at("PROXY")) {
                appendUniqueString(proxyMembers, seen, tag);
            }
            appendXrayBalancer(root["routing"]["balancers"], "PROXY", proxyMembers, "leastPing");
            managedBalancerTags.insert("PROXY");
        }
        ensureXrayProfileRouting(root, *profile, managedBalancerTags);
        const std::string strategy = detectXrayStrategy(root["routing"]["balancers"]);
        ensureObservatoryForStrategy(root, strategy);
    } else if (!config.routingRules.empty()) {
        ensureXrayCustomRoutingRules(root, config);
    } else if (config.profile == "bypass-cn") {
        ensureXrayBypassCnProfile(root);
    } else if (config.profile == "direct") {
        ensureXrayProfileRoutingRoot(root);
    }
    const std::string strategy = detectXrayStrategy(root["routing"]["balancers"]);
    if (profile == nullptr) {
        removeJsonArrayObjectsByTag(root["routing"]["balancers"], {"PROXY"});
    }
    if (!root["routing"].contains("rules") || !root["routing"]["rules"].is_array()) {
        root["routing"]["rules"] = nlohmann::json::array();
    }
    if (!managed.empty() && profile == nullptr) {
        root["routing"]["balancers"].push_back(
            {{"tag", "PROXY"}, {"selector", nlohmann::json::array({kManagedTagPrefix})}, {"fallbackTag", "DIRECT"}, {"strategy", {{"type", strategy}}}}
        );
        const std::string customFinalOutbound = customXrayFinalOutbound(config);
        std::string finalOutbound = customFinalOutbound;
        if (finalOutbound.empty()) {
            finalOutbound = config.profile == "direct" ? "DIRECT" : "PROXY";
        }
        if (finalOutbound == "PROXY") {
            if (!hasXrayCatchAllRule(root["routing"]["rules"], "PROXY")) {
                root["routing"]["rules"].push_back(
                    {{"type", "field"}, {"network", "tcp,udp"}, {"balancerTag", "PROXY"}}
                );
            }
        } else if (!hasXrayCatchAllRule(root["routing"]["rules"], finalOutbound)) {
            root["routing"]["rules"].push_back(
                {{"type", "field"}, {"network", "tcp,udp"}, {"outboundTag", finalOutbound}}
            );
        }
        ensureObservatoryForStrategy(root, strategy);
    } else if (profile == nullptr && !hasXrayCatchAllRule(root["routing"]["rules"], "DIRECT")) {
        root["routing"]["rules"].push_back(
            {{"type", "field"}, {"network", "tcp,udp"}, {"outboundTag", "DIRECT"}}
        );
    }

    result.ok = writeFile(outPath, root.dump(2), error);
    return result;
}

} // namespace subcli
