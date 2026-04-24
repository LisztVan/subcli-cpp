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

} // namespace

ExportResult exportXrayImpl(
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
        if (supportsNode(ExportTarget::Xray, node, reason)) {
            supported.push_back(node);
        } else {
            ++result.skipped;
            result.warnings.push_back({"unsupported_node", node.name + ": " + reason});
        }
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
        if (!hasXrayCatchAllRule(root["routing"]["rules"], "PROXY")) {
            root["routing"]["rules"].push_back(
                {{"type", "field"}, {"network", "tcp,udp"}, {"balancerTag", "PROXY"}}
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
