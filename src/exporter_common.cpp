#include "exporter_internal.hpp"

#include <algorithm>
#include <filesystem>
#include <regex>
#include <sstream>
#include <unordered_map>

#include "subcli/node.hpp"
#include "subcli/protocol_registry.hpp"
#include "subcli/util.hpp"

namespace subcli {

std::string joinTemplatePath(const std::string& dir, const std::string& filename) {
    return (std::filesystem::path(dir.empty() ? "./templates" : dir) / filename).string();
}

std::string resolveTemplatePath(const AppConfig& config, const std::string& configuredPath, const std::string& fallbackName) {
    if (configuredPath.empty()) {
        return joinTemplatePath(config.templateDir, fallbackName);
    }
    std::filesystem::path path(configuredPath);
    if (path.is_absolute()) {
        return path.string();
    }
    const auto configured = path.string();
    if (configured.find('/') == std::string::npos && configured.find('\\') == std::string::npos) {
        return joinTemplatePath(config.templateDir, configured);
    }
    return configured;
}

std::vector<std::string> splitCommaValues(const std::string& input) {
    std::vector<std::string> out;
    std::stringstream ss(input);
    std::string part;
    while (std::getline(ss, part, ',')) {
        if (!part.empty()) {
            out.push_back(part);
        }
    }
    return out;
}

namespace {

std::string replaceAll(std::string text, const std::string& from, const std::string& to) {
    size_t pos = 0;
    while ((pos = text.find(from, pos)) != std::string::npos) {
        text.replace(pos, from.size(), to);
        pos += to.size();
    }
    return text;
}

std::string renderNodeName(const ProxyNode& node, const std::string& tpl) {
    std::string out = tpl.empty() ? "{name}" : tpl;
    out = replaceAll(out, "{name}", node.name);
    out = replaceAll(out, "{region}", node.region);
    out = replaceAll(out, "{source}", node.sourceId);
    out = replaceAll(out, "{protocol}", node.type);
    return out;
}

void setJsonScalar(nlohmann::json& object, const std::string& key, const std::string& value) {
    if (value == "true") {
        object[key] = true;
        return;
    }
    if (value == "false") {
        object[key] = false;
        return;
    }
    try {
        size_t parsed = 0;
        const int number = std::stoi(value, &parsed);
        if (parsed == value.size()) {
            object[key] = number;
            return;
        }
    } catch (...) {
    }
    object[key] = value;
}

std::string nodeDedupeKey(const ProxyNode& node) {
    return node.type + "|" + node.server + "|" + std::to_string(node.port) + "|" + node.protocol.uuid + "|" +
           node.protocol.password + "|" + node.protocol.cipher + "|" + node.protocol.flow + "|" + node.transport.network + "|" +
           node.transport.host + "|" + node.transport.path + "|" + node.transport.serviceName + "|" + node.transport.authority + "|" +
           node.tlsConfig.sni + "|" + node.tlsConfig.fingerprint + "|" + node.tlsConfig.reality.publicKey + "|" + node.tlsConfig.reality.shortId;
}

int regionRank(const ProxyNode& node, const AppConfig& config) {
    int rank = 0;
    for (const auto& kv : config.regionRules) {
        if (kv.first == node.region) {
            return rank;
        }
        ++rank;
    }
    return rank + 100;
}

} // namespace

std::vector<ProxyNode> preprocessNodes(const std::vector<ProxyNode>& nodes, const AppConfig& config, std::vector<DiagnosticMessage>& warnings) {
    std::vector<ProxyNode> out;
    out.reserve(nodes.size());
    std::set<std::string> seenKeys;

    bool includeValid = true;
    std::regex includeRegex;
    if (!config.includeRegex.empty()) {
        try {
            includeRegex = std::regex(config.includeRegex, std::regex_constants::ECMAScript);
        } catch (...) {
            includeValid = false;
            warnings.push_back({"invalid_include_regex", "include_regex is invalid and was ignored"});
        }
    }

    bool excludeValid = true;
    std::regex excludeRegex;
    if (!config.excludeRegex.empty()) {
        try {
            excludeRegex = std::regex(config.excludeRegex, std::regex_constants::ECMAScript);
        } catch (...) {
            excludeValid = false;
            warnings.push_back({"invalid_exclude_regex", "exclude_regex is invalid and was ignored"});
        }
    }

    for (auto node : nodes) {
        node.normalize();
        if (node.tlsConfig.fingerprint.empty() && !node.fingerprint.empty() &&
            (node.tlsConfig.enabled || node.tlsConfig.reality.enabled)) {
            node.tlsConfig.fingerprint = node.fingerprint;
            node.normalize();
        }
        node.name = renderNodeName(node, config.renameTemplate);
        const std::string matchText = node.name + " " + node.region + " " + node.type + " " + node.sourceId;
        if (!config.includeRegex.empty() && includeValid && !std::regex_search(matchText, includeRegex)) {
            warnings.push_back({"filtered_node", node.name + ": excluded by include_regex"});
            continue;
        }
        if (!config.excludeRegex.empty() && excludeValid && std::regex_search(matchText, excludeRegex)) {
            warnings.push_back({"filtered_node", node.name + ": excluded by exclude_regex"});
            continue;
        }
        if (config.dedupeNodes) {
            const auto key = nodeDedupeKey(node);
            if (seenKeys.count(key)) {
                warnings.push_back({"deduped_node", node.name + ": dropped as duplicate"});
                continue;
            }
            seenKeys.insert(key);
        }
        out.push_back(node);
    }

    if (config.sortBy == "name") {
        std::sort(out.begin(), out.end(), [](const ProxyNode& a, const ProxyNode& b) { return a.name < b.name; });
    } else if (config.sortBy == "source,name") {
        std::sort(out.begin(), out.end(), [](const ProxyNode& a, const ProxyNode& b) {
            if (a.sourceId != b.sourceId) {
                return a.sourceId < b.sourceId;
            }
            return a.name < b.name;
        });
    } else {
        std::sort(out.begin(), out.end(), [&](const ProxyNode& a, const ProxyNode& b) {
            const int rankA = regionRank(a, config);
            const int rankB = regionRank(b, config);
            if (rankA != rankB) {
                return rankA < rankB;
            }
            return a.name < b.name;
        });
    }

    return out;
}

std::vector<ProxyNode> makeExportNodes(const std::vector<ProxyNode>& nodes) {
    std::unordered_map<std::string, int> counts;
    std::vector<ProxyNode> out;
    out.reserve(nodes.size());
    for (const auto& node : nodes) {
        ProxyNode copy = node;
        const std::string base = copy.name.empty() ? "node" : copy.name;
        int& seen = counts[base];
        ++seen;
        copy.name = base;
        if (seen > 1) {
            copy.name += copy.sourceId.empty() ? "" : " [" + copy.sourceId + "]";
            if (counts.count(copy.name)) {
                copy.name += " #" + std::to_string(seen);
            }
            ++counts[copy.name];
        }
        copy.normalize();
        out.push_back(copy);
    }
    return out;
}

GroupData buildGroups(const std::vector<ProxyNode>& nodes, const AppConfig& config) {
    GroupData data;
    data.groups["PROXY"] = {};
    data.groups["AUTO"] = {};
    for (const auto& kv : config.regionRules) {
        data.regionOrder.push_back(kv.first);
        data.groups[kv.first] = {};
    }
    data.regionOrder.push_back("OTHER");
    data.groups["OTHER"] = {};
    std::set<std::string> seen;
    for (const auto& n : nodes) {
        if (n.name.empty() || seen.count(n.name)) {
            continue;
        }
        seen.insert(n.name);
        data.groups["PROXY"].push_back(n.name);
        data.groups["AUTO"].push_back(n.name);
        if (data.groups.count(n.region)) {
            data.groups[n.region].push_back(n.name);
        } else {
            data.groups["OTHER"].push_back(n.name);
        }
    }
    return data;
}

std::vector<std::string> expandProfileMembers(
    const std::vector<std::string>& rawMembers,
    const GroupData& groups,
    const std::vector<ProxyNode>& exportNodes
) {
    std::vector<std::string> expanded;
    std::set<std::string> seen;

    auto appendUnique = [&](const std::string& value) {
        if (value.empty() || seen.count(value)) {
            return;
        }
        seen.insert(value);
        expanded.push_back(value);
    };

    for (const auto& member : rawMembers) {
        if (member == "REGION:*") {
            for (const auto& region : groups.regionOrder) {
                appendUnique(region);
            }
            continue;
        }
        if (member.rfind("REGION:", 0) == 0) {
            const auto region = member.substr(std::string("REGION:").size());
            if (std::find(groups.regionOrder.begin(), groups.regionOrder.end(), region) != groups.regionOrder.end()) {
                appendUnique(region);
            } else {
                appendUnique(member);
            }
            continue;
        }
        if (member == "NODE:*") {
            for (const auto& node : exportNodes) {
                appendUnique(node.name);
            }
            continue;
        }
        if (member == "SOURCE:*") {
            for (const auto& node : exportNodes) {
                appendUnique(node.name);
            }
            continue;
        }
        if (member.rfind("SOURCE:", 0) == 0) {
            const auto source = member.substr(std::string("SOURCE:").size());
            bool matched = false;
            for (const auto& node : exportNodes) {
                if (node.sourceId == source) {
                    appendUnique(node.name);
                    matched = true;
                }
            }
            if (!matched) {
                appendUnique(member);
            }
            continue;
        }
        if (member.rfind("PROTOCOL:", 0) == 0) {
            const auto protocol = canonicalProtocolName(member.substr(std::string("PROTOCOL:").size()));
            bool matched = false;
            for (const auto& node : exportNodes) {
                if (canonicalProtocolName(node.type) == protocol) {
                    appendUnique(node.name);
                    matched = true;
                }
            }
            if (!matched) {
                appendUnique(member);
            }
            continue;
        }
        if (member.rfind("TAG:", 0) == 0) {
            const auto tag = member.substr(std::string("TAG:").size());
            bool matched = false;
            for (const auto& node : exportNodes) {
                if (std::find(node.sourceTags.begin(), node.sourceTags.end(), tag) != node.sourceTags.end()) {
                    appendUnique(node.name);
                    matched = true;
                }
            }
            if (!matched) {
                appendUnique(member);
            }
            continue;
        }
        appendUnique(member);
    }

    return expanded;
}

std::set<std::string> generatedSingBoxTags(const std::vector<ProxyNode>& nodes, const GroupData& groups) {
    std::set<std::string> tags = {"PROXY", "AUTO", "dns-remote"};
    for (const auto& n : nodes) {
        tags.insert(n.name);
    }
    for (const auto& region : groups.regionOrder) {
        tags.insert(region);
    }
    return tags;
}

std::set<std::string> generatedXrayTags(const std::vector<ProxyNode>& nodes) {
    std::set<std::string> tags = {"PROXY"};
    for (const auto& n : nodes) {
        tags.insert(n.name);
    }
    return tags;
}

void removeJsonArrayObjectsByTag(nlohmann::json& array, const std::set<std::string>& tags) {
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
        if (!tag.empty() && tags.count(tag)) {
            continue;
        }
        kept.push_back(item);
    }
    array = kept;
}

bool hasRouteRuleForOutbound(const nlohmann::json& rules, const std::string& outbound) {
    if (!rules.is_array()) {
        return false;
    }
    for (const auto& rule : rules) {
        if (!rule.is_object()) {
            continue;
        }
        if (rule.value("outbound", "") == outbound || rule.value("outboundTag", "") == outbound ||
            rule.value("balancerTag", "") == outbound) {
            return true;
        }
    }
    return false;
}

bool hasXrayCatchAllRule(const nlohmann::json& rules, const std::string& target) {
    if (!rules.is_array()) {
        return false;
    }
    for (const auto& rule : rules) {
        if (!rule.is_object()) {
            continue;
        }
        if (rule.value("outboundTag", "") != target && rule.value("balancerTag", "") != target) {
            continue;
        }
        if (rule.value("network", "") == "tcp,udp") {
            return true;
        }
    }
    return false;
}

bool hasSingBoxDnsDirectRule(const nlohmann::json& rules) {
    if (!rules.is_array()) {
        return false;
    }
    for (const auto& rule : rules) {
        if (!rule.is_object()) {
            continue;
        }
        if (rule.value("action", "") != "route" || rule.value("outbound", "") != "DIRECT") {
            continue;
        }
        if (rule.contains("port") && rule["port"].is_number_integer() && rule["port"].get<int>() == 53) {
            return true;
        }
        if (rule.contains("port") && rule["port"].is_array()) {
            for (const auto& port : rule["port"]) {
                if (port.is_number_integer() && port.get<int>() == 53) {
                    return true;
                }
            }
        }
        if (rule.contains("port_range") && rule["port_range"].is_array()) {
            for (const auto& range : rule["port_range"]) {
                if (!range.is_string()) {
                    continue;
                }
                const auto value = range.get<std::string>();
                if (value == "53:53" || value == ":53" || value == "53:") {
                    return true;
                }
            }
        }
        if (rule.contains("network") && rule["network"].is_array()) {
            for (const auto& network : rule["network"]) {
                if (network.is_string() && network.get<std::string>() == "udp") {
                    return true;
                }
            }
        } else if (rule.contains("network") && rule["network"].is_string()) {
            const auto network = rule["network"].get<std::string>();
            if (network == "udp") {
                return true;
            }
        }
    }
    return false;
}

bool hasMihomoRule(const YAML::Node& rules, const std::string& value) {
    if (!rules || !rules.IsSequence()) {
        return false;
    }
    for (const auto& rule : rules) {
        if (rule.as<std::string>("") == value) {
            return true;
        }
    }
    return false;
}

bool parseTemplatePolicyAction(const std::string& value, TemplatePolicyAction& action) {
    if (value == "replace") {
        action = TemplatePolicyAction::Replace;
        return true;
    }
    if (value == "append") {
        action = TemplatePolicyAction::Append;
        return true;
    }
    if (value == "merge") {
        action = TemplatePolicyAction::Merge;
        return true;
    }
    if (value == "reject") {
        action = TemplatePolicyAction::Reject;
        return true;
    }
    return false;
}

bool parseTemplatePolicyTarget(const std::string& value, ExportTarget& target) {
    if (value == "mihomo") {
        target = ExportTarget::Mihomo;
        return true;
    }
    if (value == "sing-box") {
        target = ExportTarget::SingBox;
        return true;
    }
    if (value == "xray") {
        target = ExportTarget::Xray;
        return true;
    }
    return false;
}

std::string templatePolicyTargetKey(ExportTarget target) {
    switch (target) {
        case ExportTarget::Mihomo:
            return "mihomo";
        case ExportTarget::SingBox:
            return "sing-box";
        case ExportTarget::Xray:
            return "xray";
    }
    return "";
}

bool isTemplatePolicyPathSupported(ExportTarget target, const std::string& path) {
    switch (target) {
        case ExportTarget::SingBox:
            return path == "outbounds" || path == "dns" || path == "dns.servers" || path == "dns.rules" ||
                   path == "route.rules" || path == "route.rule_set";
        case ExportTarget::Xray:
            return path == "outbounds" || path == "dns" || path == "dns.servers" || path == "routing.rules" ||
                   path == "routing.balancers";
        case ExportTarget::Mihomo:
            return path == "proxies" || path == "proxy-groups" || path == "rules" || path == "dns" ||
                   path == "dns.nameserver" || path == "dns.fallback";
    }
    return false;
}

bool isTemplatePolicyActionSupportedForPath(ExportTarget target, const std::string& path, TemplatePolicyAction action) {
    if (!isTemplatePolicyPathSupported(target, path)) {
        return false;
    }
    if (action == TemplatePolicyAction::Replace || action == TemplatePolicyAction::Reject) {
        return true;
    }
    if (action == TemplatePolicyAction::Append) {
        switch (target) {
            case ExportTarget::SingBox:
                return path == "outbounds" || path == "dns.servers" || path == "dns.rules" ||
                       path == "route.rules" || path == "route.rule_set";
            case ExportTarget::Xray:
                return path == "outbounds" || path == "dns.servers" || path == "routing.rules" ||
                       path == "routing.balancers";
            case ExportTarget::Mihomo:
                return path == "proxies" || path == "proxy-groups" || path == "rules" ||
                       path == "dns.nameserver" || path == "dns.fallback";
        }
        return false;
    }
    switch (target) {
        case ExportTarget::SingBox:
            return path == "outbounds" || path == "dns" || path == "dns.servers" || path == "route.rule_set";
        case ExportTarget::Xray:
            return path == "outbounds" || path == "routing.balancers";
        case ExportTarget::Mihomo:
            return path == "proxies" || path == "proxy-groups" || path == "dns";
    }
    return false;
}

TemplatePolicyAction defaultTemplatePolicyAction(ExportTarget target, const std::string& path) {
    switch (target) {
        case ExportTarget::SingBox:
            if (path == "outbounds" || path == "dns.servers" || path == "route.rule_set") {
                return TemplatePolicyAction::Merge;
            }
            return TemplatePolicyAction::Replace;
        case ExportTarget::Xray:
            if (path == "outbounds" || path == "routing.balancers") {
                return TemplatePolicyAction::Merge;
            }
            return TemplatePolicyAction::Replace;
        case ExportTarget::Mihomo:
            if (path == "proxies" || path == "proxy-groups" || path == "dns") {
                return TemplatePolicyAction::Merge;
            }
            return TemplatePolicyAction::Replace;
    }
    return TemplatePolicyAction::Replace;
}

TemplatePolicyAction resolveTemplatePolicyAction(ExportTarget target, const ResolvedProfile* profile, const std::string& path) {
    TemplatePolicyAction resolved = defaultTemplatePolicyAction(target, path);
    if (profile == nullptr) {
        return resolved;
    }

    const std::string targetKey = templatePolicyTargetKey(target);

    const auto targetIt = profile->templatePolicy.targets.find(targetKey);
    if (targetIt == profile->templatePolicy.targets.end()) {
        return resolved;
    }
    const auto actionIt = targetIt->second.pathActions.find(path);
    if (actionIt == targetIt->second.pathActions.end()) {
        return resolved;
    }

    TemplatePolicyAction parsed = resolved;
    if (parseTemplatePolicyAction(actionIt->second, parsed) && isTemplatePolicyActionSupportedForPath(target, path, parsed)) {
        return parsed;
    }
    return resolved;
}

bool getExplicitTemplatePolicyAction(ExportTarget target, const ResolvedProfile* profile, const std::string& path, TemplatePolicyAction& action) {
    if (profile == nullptr) {
        return false;
    }

    const std::string targetKey = templatePolicyTargetKey(target);

    const auto targetIt = profile->templatePolicy.targets.find(targetKey);
    if (targetIt == profile->templatePolicy.targets.end()) {
        return false;
    }
    const auto actionIt = targetIt->second.pathActions.find(path);
    if (actionIt == targetIt->second.pathActions.end()) {
        return false;
    }
    TemplatePolicyAction parsed;
    if (!parseTemplatePolicyAction(actionIt->second, parsed)) {
        return false;
    }
    if (!isTemplatePolicyActionSupportedForPath(target, path, parsed)) {
        return false;
    }
    action = parsed;
    return true;
}

void addTemplatePolicyRejectWarning(std::vector<DiagnosticMessage>& warnings, const std::string& path) {
    warnings.push_back(
        {"template_policy_reject_preserved", "template_policy rejected generated field '" + path + "'; preserved template content"}
    );
}

void applyTemplatePolicyJsonField(
    nlohmann::json& parent,
    const std::string& key,
    const nlohmann::json* templateValue,
    bool templateHad,
    const nlohmann::json& generatedValue,
    TemplatePolicyAction action,
    const std::string& path,
    const std::string& mergeKey,
    std::vector<DiagnosticMessage>& warnings
) {
    if (action == TemplatePolicyAction::Reject) {
        if (templateHad) {
            parent[key] = *templateValue;
            addTemplatePolicyRejectWarning(warnings, path);
        } else {
            parent.erase(key);
        }
        return;
    }

    if (action == TemplatePolicyAction::Replace) {
        parent[key] = generatedValue;
        return;
    }

    if (action == TemplatePolicyAction::Append) {
        if (!templateHad || templateValue == nullptr) {
            parent[key] = generatedValue;
            return;
        }
        if (!templateValue->is_array() || !generatedValue.is_array()) {
            parent[key] = generatedValue;
            return;
        }
        nlohmann::json merged = *templateValue;
        for (const auto& item : generatedValue) {
            merged.push_back(item);
        }
        parent[key] = merged;
        return;
    }

    if (action == TemplatePolicyAction::Merge) {
        if (!templateHad || templateValue == nullptr) {
            parent[key] = generatedValue;
            return;
        }
        if (templateValue->is_object() && generatedValue.is_object()) {
            nlohmann::json merged = *templateValue;
            for (auto it = generatedValue.begin(); it != generatedValue.end(); ++it) {
                merged[it.key()] = it.value();
            }
            parent[key] = merged;
            return;
        }
        if (templateValue->is_array() && generatedValue.is_array()) {
            if (mergeKey.empty()) {
                nlohmann::json merged = *templateValue;
                for (const auto& item : generatedValue) {
                    merged.push_back(item);
                }
                parent[key] = merged;
                return;
            }
            nlohmann::json merged = nlohmann::json::array();
            std::map<std::string, size_t> indexByKey;
            for (const auto& item : *templateValue) {
                if (item.is_object() && item.contains(mergeKey) && item[mergeKey].is_string()) {
                    indexByKey[item[mergeKey].get<std::string>()] = merged.size();
                }
                merged.push_back(item);
            }
            for (const auto& item : generatedValue) {
                if (item.is_object() && item.contains(mergeKey) && item[mergeKey].is_string()) {
                    const std::string keyValue = item[mergeKey].get<std::string>();
                    const auto existing = indexByKey.find(keyValue);
                    if (existing != indexByKey.end()) {
                        merged[existing->second] = item;
                    } else {
                        indexByKey[keyValue] = merged.size();
                        merged.push_back(item);
                    }
                } else {
                    merged.push_back(item);
                }
            }
            parent[key] = merged;
            return;
        }
        parent[key] = generatedValue;
        return;
    }
}

void applyMihomoStructuredProtocolFields(const Node& node, YAML::Node& p) {
    if (const auto* options = std::get_if<ShadowsocksOptions>(&node.options)) {
        if (!options->method.empty()) {
            p["cipher"] = options->method;
        }
        if (!options->password.empty()) {
            p["password"] = options->password;
        }
        if (options->udpOverTcp) {
            p["udp-over-tcp"] = true;
            if (options->udpOverTcpVersion > 0) {
                p["udp-over-tcp-version"] = options->udpOverTcpVersion;
            }
        }
        if (!options->plugin.empty()) {
            p["plugin"] = options->plugin;
        }
        for (const auto& item : options->pluginOptions) {
            p["plugin-opts"][item.first] = item.second;
        }
        return;
    }
    if (const auto* options = std::get_if<VMessOptions>(&node.options)) {
        if (!options->uuid.empty()) {
            p["uuid"] = options->uuid;
        }
        p["alterId"] = options->alterId;
        if (!options->security.empty()) {
            p["cipher"] = options->security;
        }
        if (!options->packetEncoding.empty()) {
            p["packet-encoding"] = options->packetEncoding;
        }
        if (options->globalPadding) {
            p["global-padding"] = true;
        }
        if (options->authenticatedLength) {
            p["authenticated-length"] = true;
        }
        return;
    }
    if (const auto* options = std::get_if<VLESSOptions>(&node.options)) {
        if (!options->uuid.empty()) {
            p["uuid"] = options->uuid;
        }
        if (!options->flow.empty()) {
            p["flow"] = options->flow;
        }
        if (!options->encryption.empty()) {
            p["encryption"] = options->encryption;
        }
        if (!options->packetEncoding.empty()) {
            p["packet-encoding"] = options->packetEncoding;
        }
        return;
    }
    if (const auto* options = std::get_if<TrojanOptions>(&node.options)) {
        if (!options->password.empty()) {
            p["password"] = options->password;
        }
        for (const auto& item : options->shadowsocksOptions) {
            p["ss-opts"][item.first] = item.second;
        }
        return;
    }
    if (const auto* options = std::get_if<WireGuardOptions>(&node.options)) {
        if (!options->privateKey.empty()) {
            p["private-key"] = options->privateKey;
        }
        if (!options->localAddress.empty()) {
            p["ip"] = options->localAddress.front();
            if (options->localAddress.size() > 1) {
                p["ipv6"] = options->localAddress[1];
            }
        }
        if (!options->peers.empty()) {
            const auto& peer = options->peers.front();
            if (!peer.publicKey.empty()) {
                p["public-key"] = peer.publicKey;
            }
            if (!peer.preSharedKey.empty()) {
                p["pre-shared-key"] = peer.preSharedKey;
            }
            if (!peer.allowedIps.empty()) {
                for (const auto& allowed : peer.allowedIps) {
                    p["allowed-ips"].push_back(allowed);
                }
            }
            if (!peer.reserved.empty()) {
                for (const auto reserved : peer.reserved) {
                    p["reserved"].push_back(reserved);
                }
            }
        }
        if (options->mtu > 0) {
            p["mtu"] = options->mtu;
        }
        return;
    }
    if (const auto* options = std::get_if<Hysteria2Options>(&node.options)) {
        if (!options->password.empty()) {
            p["password"] = options->password;
        }
        if (!options->ports.empty()) {
            p["ports"] = options->ports;
        }
        if (!options->hopInterval.empty()) {
            p["hop-interval"] = options->hopInterval;
        }
        if (options->upMbps > 0) {
            p["up"] = std::to_string(options->upMbps) + " Mbps";
        }
        if (options->downMbps > 0) {
            p["down"] = std::to_string(options->downMbps) + " Mbps";
        }
        if (!options->obfsType.empty()) {
            p["obfs"] = options->obfsType;
        }
        if (!options->obfsPassword.empty()) {
            p["obfs-password"] = options->obfsPassword;
        }
        return;
    }
    if (const auto* options = std::get_if<TUICOptions>(&node.options)) {
        if (!options->token.empty()) {
            p["token"] = options->token;
        }
        if (!options->uuid.empty()) {
            p["uuid"] = options->uuid;
        }
        if (!options->password.empty()) {
            p["password"] = options->password;
        }
        if (!options->congestionControl.empty()) {
            p["congestion-controller"] = options->congestionControl;
        }
        if (!options->udpRelayMode.empty()) {
            p["udp-relay-mode"] = options->udpRelayMode;
        }
        if (options->zeroRttHandshake) {
            p["reduce-rtt"] = true;
        }
        if (!options->heartbeat.empty()) {
            p["heartbeat-interval"] = options->heartbeat;
        }
    }
}

YAML::Node makeMihomoProxy(const ProxyNode& n) {
    YAML::Node p;
    const auto type = canonicalProtocolName(n.type);
    const Node structured = legacyToStructuredNode(n);
    p["name"] = n.name;
    p["type"] = targetProtocolName(ExportTarget::Mihomo, n.type);
    p["server"] = n.server;
    p["port"] = n.port;
    applyMihomoStructuredProtocolFields(structured, p);
    if (!n.protocol.uuid.empty() && !p["uuid"]) {
        p["uuid"] = n.protocol.uuid;
    }
    if (!n.protocol.password.empty() && !p["password"]) {
        p["password"] = n.protocol.password;
    }
    if (!n.protocol.cipher.empty() && !p["cipher"]) {
        p["cipher"] = n.protocol.cipher;
    }
    for (const auto& item : n.protocol.values) {
        constexpr const char* prefix = "raw_mihomo.";
        if (item.first.rfind(prefix, 0) == 0 && !item.second.empty()) {
            p[item.first.substr(std::string(prefix).size())] = item.second;
        }
    }
    if (n.tlsConfig.enabled || n.tlsConfig.reality.enabled) {
        p["tls"] = true;
    }
    if (n.tlsConfig.allowInsecure) {
        p["skip-cert-verify"] = true;
    }
    if (!n.tlsConfig.sni.empty()) {
        p["servername"] = n.tlsConfig.sni;
    }
    if (!n.tlsConfig.fingerprint.empty()) {
        p["client-fingerprint"] = n.tlsConfig.fingerprint;
    }
    if (n.tlsConfig.reality.enabled && !n.tlsConfig.reality.publicKey.empty()) {
        p["reality-opts"]["public-key"] = n.tlsConfig.reality.publicKey;
        if (!n.tlsConfig.reality.shortId.empty()) {
            p["reality-opts"]["short-id"] = n.tlsConfig.reality.shortId;
        }
    }
    if (!n.transport.network.empty()) {
        p["network"] = n.transport.network;
    }
    const auto network = toLower(n.transport.network);
    if (network == "ws") {
        if (!n.transport.path.empty()) {
            p["ws-opts"]["path"] = n.transport.path;
        }
        if (!n.transport.host.empty()) {
            p["ws-opts"]["headers"]["Host"] = n.transport.host;
        }
    }
    if (network == "http") {
        if (!n.transport.path.empty()) {
            p["http-opts"]["path"] = n.transport.path;
        }
        if (!n.transport.host.empty()) {
            for (const auto& host : splitCommaValues(n.transport.host)) {
                p["http-opts"]["headers"]["Host"].push_back(host);
            }
        }
    }
    if (network == "h2") {
        if (!n.transport.path.empty()) {
            p["h2-opts"]["path"] = n.transport.path;
        }
        if (!n.transport.host.empty()) {
            for (const auto& host : splitCommaValues(n.transport.host)) {
                p["h2-opts"]["host"].push_back(host);
            }
        }
    }
    if (network == "grpc" && !n.transport.serviceName.empty()) {
        p["grpc-opts"]["grpc-service-name"] = n.transport.serviceName;
        if (!n.transport.authority.empty()) {
            p["grpc-opts"]["grpc-authority"] = n.transport.authority;
        }
    }
    return p;
}

void applySingBoxStructuredProtocolFields(const Node& node, nlohmann::json& o) {
    if (const auto* options = std::get_if<ShadowsocksOptions>(&node.options)) {
        if (!options->method.empty()) {
            o["method"] = options->method;
        }
        if (!options->password.empty()) {
            o["password"] = options->password;
        }
        return;
    }
    if (const auto* options = std::get_if<VMessOptions>(&node.options)) {
        if (!options->uuid.empty()) {
            o["uuid"] = options->uuid;
        }
        o["security"] = options->security.empty() ? "auto" : options->security;
        if (options->alterId != 0) {
            o["alter_id"] = options->alterId;
        }
        if (!options->packetEncoding.empty()) {
            o["packet_encoding"] = options->packetEncoding;
        }
        if (options->globalPadding) {
            o["global_padding"] = true;
        }
        if (options->authenticatedLength) {
            o["authenticated_length"] = true;
        }
        return;
    }
    if (const auto* options = std::get_if<VLESSOptions>(&node.options)) {
        if (!options->uuid.empty()) {
            o["uuid"] = options->uuid;
        }
        if (!options->flow.empty()) {
            o["flow"] = options->flow;
        }
        if (!options->packetEncoding.empty()) {
            o["packet_encoding"] = options->packetEncoding;
        }
        return;
    }
    if (const auto* options = std::get_if<TrojanOptions>(&node.options)) {
        if (!options->password.empty()) {
            o["password"] = options->password;
        }
        return;
    }
    if (const auto* options = std::get_if<Hysteria2Options>(&node.options)) {
        if (!options->password.empty()) {
            o["password"] = options->password;
        }
        if (!options->ports.empty()) {
            o["server_ports"] = splitCommaValues(options->ports);
        }
        if (!options->hopInterval.empty()) {
            o["hop_interval"] = options->hopInterval;
        }
        if (!options->hopIntervalMax.empty()) {
            o["hop_interval_max"] = options->hopIntervalMax;
        }
        if (options->upMbps > 0) {
            o["up_mbps"] = options->upMbps;
        }
        if (options->downMbps > 0) {
            o["down_mbps"] = options->downMbps;
        }
        if (!options->obfsType.empty()) {
            o["obfs"] = {{"type", options->obfsType}};
            if (!options->obfsPassword.empty()) {
                o["obfs"]["password"] = options->obfsPassword;
            }
        }
        return;
    }
    if (const auto* options = std::get_if<TUICOptions>(&node.options)) {
        if (!options->uuid.empty()) {
            o["uuid"] = options->uuid;
        }
        if (!options->password.empty()) {
            o["password"] = options->password;
        }
        if (!options->congestionControl.empty()) {
            o["congestion_control"] = options->congestionControl;
        }
        if (!options->udpRelayMode.empty()) {
            o["udp_relay_mode"] = options->udpRelayMode;
        }
        if (options->zeroRttHandshake) {
            o["zero_rtt_handshake"] = true;
        }
        if (!options->heartbeat.empty()) {
            o["heartbeat"] = options->heartbeat;
        }
    }
}

nlohmann::json makeSingBoxWireGuardEndpoint(const ProxyNode& n) {
    const Node node = legacyToStructuredNode(n);
    nlohmann::json endpoint = { {"type", "wireguard"}, {"tag", n.name} };
    const auto* options = std::get_if<WireGuardOptions>(&node.options);
    if (!options) {
        return endpoint;
    }
    if (!options->privateKey.empty()) {
        endpoint["private_key"] = options->privateKey;
    }
    if (!options->localAddress.empty()) {
        endpoint["address"] = options->localAddress;
    }
    if (options->mtu > 0) {
        endpoint["mtu"] = options->mtu;
    }
    if (options->workers > 0) {
        endpoint["workers"] = options->workers;
    }
    endpoint["peers"] = nlohmann::json::array();
    for (const auto& peer : options->peers) {
        nlohmann::json item;
        if (!peer.endpoint.host.empty()) {
            item["address"] = peer.endpoint.host;
        }
        if (peer.endpoint.port > 0) {
            item["port"] = peer.endpoint.port;
        }
        if (!peer.publicKey.empty()) {
            item["public_key"] = peer.publicKey;
        }
        if (!peer.preSharedKey.empty()) {
            item["pre_shared_key"] = peer.preSharedKey;
        }
        if (!peer.allowedIps.empty()) {
            item["allowed_ips"] = peer.allowedIps;
        }
        if (!peer.reserved.empty()) {
            item["reserved"] = peer.reserved;
        }
        endpoint["peers"].push_back(item);
    }
    return endpoint;
}

void applyXrayStructuredProtocolFields(const Node& node, nlohmann::json& o) {
    if (const auto* options = std::get_if<VMessOptions>(&node.options)) {
        o["settings"]["vnext"] = nlohmann::json::array(
            {{{"address", node.server.host}, {"port", node.server.port}, {"users", nlohmann::json::array()}}}
        );
        nlohmann::json user;
        if (!options->uuid.empty()) {
            user["id"] = options->uuid;
        }
        user["security"] = options->security.empty() ? "auto" : options->security;
        if (options->alterId != 0) {
            user["alterId"] = options->alterId;
        }
        if (options->authenticatedLength) {
            user["experiments"] = "AuthenticatedLength";
        }
        o["settings"]["vnext"][0]["users"].push_back(user);
        return;
    }
    if (const auto* options = std::get_if<VLESSOptions>(&node.options)) {
        o["settings"]["vnext"] = nlohmann::json::array(
            {{{"address", node.server.host}, {"port", node.server.port}, {"users", nlohmann::json::array()}}}
        );
        nlohmann::json user;
        if (!options->uuid.empty()) {
            user["id"] = options->uuid;
        }
        user["encryption"] = options->encryption.empty() ? "none" : options->encryption;
        if (!options->flow.empty()) {
            user["flow"] = options->flow;
        }
        o["settings"]["vnext"][0]["users"].push_back(user);
        return;
    }
    if (const auto* options = std::get_if<TrojanOptions>(&node.options)) {
        o["settings"]["servers"] = nlohmann::json::array(
            {{{"address", node.server.host}, {"port", node.server.port}, {"password", options->password}}}
        );
        return;
    }
    if (const auto* options = std::get_if<ShadowsocksOptions>(&node.options)) {
        o["settings"]["servers"] = nlohmann::json::array(
            {{{"address", node.server.host}, {"port", node.server.port}, {"method", options->method}, {"password", options->password}}}
        );
        if (options->udpOverTcp) {
            o["settings"]["servers"][0]["uot"] = true;
            if (options->udpOverTcpVersion > 0) {
                o["settings"]["servers"][0]["UoTVersion"] = options->udpOverTcpVersion;
            }
        }
        return;
    }
    if (const auto* options = std::get_if<WireGuardOptions>(&node.options)) {
        if (!options->privateKey.empty()) {
            o["settings"]["secretKey"] = options->privateKey;
        }
        if (!options->localAddress.empty()) {
            o["settings"]["address"] = options->localAddress;
        }
        if (options->mtu > 0) {
            o["settings"]["mtu"] = options->mtu;
        }
        if (options->workers > 0) {
            o["settings"]["workers"] = options->workers;
        }
        if (!options->domainStrategy.empty()) {
            o["settings"]["domainStrategy"] = options->domainStrategy;
        }
        o["settings"]["peers"] = nlohmann::json::array();
        for (const auto& peer : options->peers) {
            nlohmann::json item;
            if (!peer.endpoint.host.empty() && peer.endpoint.port > 0) {
                item["endpoint"] = peer.endpoint.host + ":" + std::to_string(peer.endpoint.port);
            }
            if (!peer.publicKey.empty()) {
                item["publicKey"] = peer.publicKey;
            }
            if (!peer.preSharedKey.empty()) {
                item["preSharedKey"] = peer.preSharedKey;
            }
            if (!peer.allowedIps.empty()) {
                item["allowedIPs"] = peer.allowedIps;
            }
            o["settings"]["peers"].push_back(item);
        }
        return;
    }
    if (const auto* options = std::get_if<HysteriaOptions>(&node.options)) {
        o["settings"]["version"] = options->version;
        o["settings"]["address"] = node.server.host;
        o["settings"]["port"] = node.server.port;
        return;
    }
    if (node.protocol == "freedom" || node.protocol == "blackhole" || node.protocol == "dns" || node.protocol == "loopback") {
        return;
    }
    if (node.protocol == "http" || node.protocol == "socks") {
        o["settings"]["servers"] = nlohmann::json::array({{{"address", node.server.host}, {"port", node.server.port}}});
    }
}

nlohmann::json makeSingBoxOutbound(const ProxyNode& n) {
    nlohmann::json o;
    const auto type = canonicalProtocolName(n.type);
    const Node structured = legacyToStructuredNode(n);
    o["tag"] = n.name;
    o["type"] = targetProtocolName(ExportTarget::SingBox, n.type);
    o["server"] = n.server;
    o["server_port"] = n.port;
    for (const auto& item : n.protocol.values) {
        constexpr const char* prefix = "raw_singbox.";
        if (item.first.rfind(prefix, 0) == 0 && !item.second.empty()) {
            setJsonScalar(o, item.first.substr(std::string(prefix).size()), item.second);
        }
    }
    if (!n.protocol.uuid.empty() && !o.contains("uuid")) {
        o["uuid"] = n.protocol.uuid;
    }
    if (!n.protocol.password.empty() && !o.contains("password")) {
        o["password"] = n.protocol.password;
    }
    if (!n.protocol.cipher.empty() && !o.contains("method")) {
        o["method"] = n.protocol.cipher;
    }
    if (type == "hysteria2") {
        applySingBoxStructuredProtocolFields(structured, o);
        if (n.tlsConfig.enabled || n.tlsConfig.reality.enabled) {
            o["tls"] = nlohmann::json::object();
            o["tls"]["enabled"] = true;
            if (!n.tlsConfig.sni.empty()) {
                o["tls"]["server_name"] = n.tlsConfig.sni;
            }
            if (n.tlsConfig.allowInsecure) {
                o["tls"]["insecure"] = true;
            }
            if (!n.tlsConfig.alpn.empty()) {
                o["tls"]["alpn"] = n.tlsConfig.alpn;
            }
        }
        return o;
    }
    if (type == "tuic") {
        applySingBoxStructuredProtocolFields(structured, o);
        if (n.tlsConfig.enabled || n.tlsConfig.reality.enabled) {
            o["tls"] = nlohmann::json::object();
            o["tls"]["enabled"] = true;
            if (!n.tlsConfig.sni.empty()) {
                o["tls"]["server_name"] = n.tlsConfig.sni;
            }
            if (n.tlsConfig.allowInsecure) {
                o["tls"]["insecure"] = true;
            }
            if (!n.tlsConfig.alpn.empty()) {
                o["tls"]["alpn"] = n.tlsConfig.alpn;
            }
        }
        return o;
    }
    if (type == "wireguard") {
        return o;
    }
    applySingBoxStructuredProtocolFields(structured, o);
    if (n.tlsConfig.enabled || n.tlsConfig.reality.enabled) {
        o["tls"] = nlohmann::json::object();
        o["tls"]["enabled"] = true;
        if (!n.tlsConfig.sni.empty()) {
            o["tls"]["server_name"] = n.tlsConfig.sni;
        }
        if (!n.tlsConfig.alpn.empty()) {
            o["tls"]["alpn"] = n.tlsConfig.alpn;
        }
        if (n.tlsConfig.allowInsecure) {
            o["tls"]["insecure"] = true;
        }
        if (!n.tlsConfig.fingerprint.empty()) {
            o["tls"]["utls"] = {{"enabled", true}, {"fingerprint", n.tlsConfig.fingerprint}};
        }
        if (n.tlsConfig.reality.enabled && !n.tlsConfig.reality.publicKey.empty()) {
            o["tls"]["reality"] = {{"enabled", true}, {"public_key", n.tlsConfig.reality.publicKey}};
            if (!n.tlsConfig.reality.shortId.empty()) {
                o["tls"]["reality"]["short_id"] = n.tlsConfig.reality.shortId;
            }
        }
    }
    const auto network = toLower(n.transport.network);
    if (network == "ws") {
        o["transport"] = {{"type", "ws"}, {"path", n.transport.path.empty() ? "/" : n.transport.path}};
        if (!n.transport.host.empty()) {
            o["transport"]["headers"] = {{"Host", n.transport.host}};
        }
    } else if (network == "grpc") {
        o["transport"] = {{"type", "grpc"}, {"service_name", n.transport.serviceName}};
        if (!n.transport.authority.empty()) {
            o["transport"]["authority"] = n.transport.authority;
        }
    } else if (network == "http" || network == "h2" || network == "httpupgrade") {
        o["transport"] = {{"type", network}, {"path", n.transport.path}};
        if (!n.transport.host.empty()) {
            o["transport"]["host"] = splitCommaValues(n.transport.host);
        }
    }
    if (type == "vless" && !n.protocol.flow.empty()) {
        o["flow"] = n.protocol.flow;
    }
    return o;
}

nlohmann::json makeXrayOutbound(const ProxyNode& n) {
    nlohmann::json o;
    const auto proto = canonicalProtocolName(n.type);
    const Node structured = legacyToStructuredNode(n);
    o["tag"] = n.name;
    o["protocol"] = targetProtocolName(ExportTarget::Xray, n.type);
    o["settings"] = nlohmann::json::object();
    applyXrayStructuredProtocolFields(structured, o);
    const std::string security = n.tlsConfig.reality.enabled ? "reality" : (n.tlsConfig.enabled ? "tls" : "");
    if (!security.empty()) {
        o["streamSettings"]["security"] = security;
    }
    if (security == "tls") {
        if (!n.tlsConfig.sni.empty()) {
            o["streamSettings"]["tlsSettings"]["serverName"] = n.tlsConfig.sni;
        }
        if (!n.tlsConfig.alpn.empty()) {
            o["streamSettings"]["tlsSettings"]["alpn"] = n.tlsConfig.alpn;
        }
        if (n.tlsConfig.allowInsecure) {
            o["streamSettings"]["tlsSettings"]["allowInsecure"] = true;
        }
        if (!n.tlsConfig.fingerprint.empty()) {
            o["streamSettings"]["tlsSettings"]["fingerprint"] = n.tlsConfig.fingerprint;
        }
    }
    if (security == "reality") {
        if (!n.tlsConfig.sni.empty()) {
            o["streamSettings"]["realitySettings"]["serverName"] = n.tlsConfig.sni;
        }
        if (!n.tlsConfig.reality.publicKey.empty()) {
            o["streamSettings"]["realitySettings"]["publicKey"] = n.tlsConfig.reality.publicKey;
        }
        if (!n.tlsConfig.reality.shortId.empty()) {
            o["streamSettings"]["realitySettings"]["shortId"] = n.tlsConfig.reality.shortId;
        }
        if (!n.tlsConfig.reality.spiderX.empty()) {
            o["streamSettings"]["realitySettings"]["spiderX"] = n.tlsConfig.reality.spiderX;
        }
        if (!n.tlsConfig.fingerprint.empty()) {
            o["streamSettings"]["realitySettings"]["fingerprint"] = n.tlsConfig.fingerprint;
        }
    }
    const auto network = toLower(n.transport.network);
    if (!network.empty()) {
        o["streamSettings"]["network"] = network;
        if (network == "ws") {
            if (!n.transport.path.empty()) {
                o["streamSettings"]["wsSettings"]["path"] = n.transport.path;
            }
            if (!n.transport.host.empty()) {
                o["streamSettings"]["wsSettings"]["headers"]["Host"] = n.transport.host;
            }
        } else if (network == "grpc") {
            if (!n.transport.serviceName.empty()) {
                o["streamSettings"]["grpcSettings"]["serviceName"] = n.transport.serviceName;
            }
            if (!n.transport.authority.empty()) {
                o["streamSettings"]["grpcSettings"]["authority"] = n.transport.authority;
            }
        } else if (network == "http" || network == "h2") {
            if (!n.transport.path.empty()) {
                o["streamSettings"]["httpSettings"]["path"] = n.transport.path;
            }
            if (!n.transport.host.empty()) {
                o["streamSettings"]["httpSettings"]["host"] = splitCommaValues(n.transport.host);
            }
        } else if (network == "httpupgrade") {
            if (!n.transport.path.empty()) {
                o["streamSettings"]["httpupgradeSettings"]["path"] = n.transport.path;
            }
            if (!n.transport.host.empty()) {
                o["streamSettings"]["httpupgradeSettings"]["host"] = n.transport.host;
            }
        }
    }
    return o;
}

} // namespace subcli
