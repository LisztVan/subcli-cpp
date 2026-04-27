#include "exporter_internal.hpp"

#include <iomanip>
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
    (void)profile;
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
    if (!config.routingRules.empty()) {
        ensureXrayCustomRoutingRules(root, config);
    } else if (config.profile == "bypass-cn") {
        ensureXrayBypassCnProfile(root);
    } else if (config.profile == "direct") {
        ensureXrayProfileRoutingRoot(root);
    }
    if (!root["routing"].contains("balancers") || !root["routing"]["balancers"].is_array()) {
        root["routing"]["balancers"] = nlohmann::json::array();
    }
    const std::string strategy = detectXrayStrategy(root["routing"]["balancers"]);
    removeJsonArrayObjectsByTag(root["routing"]["balancers"], {"PROXY"});
    if (!root["routing"].contains("rules") || !root["routing"]["rules"].is_array()) {
        root["routing"]["rules"] = nlohmann::json::array();
    }
    if (!managed.empty()) {
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
    } else if (!hasXrayCatchAllRule(root["routing"]["rules"], "DIRECT")) {
        root["routing"]["rules"].push_back(
            {{"type", "field"}, {"network", "tcp,udp"}, {"outboundTag", "DIRECT"}}
        );
    }

    result.ok = writeFile(outPath, root.dump(2), error);
    return result;
}

} // namespace subcli
