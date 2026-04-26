#include "exporter_internal.hpp"

#include "subcli/capabilities.hpp"
#include "subcli/util.hpp"

namespace subcli {

namespace {

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

} // namespace

ExportResult exportMihomoImpl(
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

    YAML::Node proxyGroups(YAML::NodeType::Sequence);
    std::set<std::string> managedGroups = {"PROXY", "AUTO"};
    for (const auto& region : groups.regionOrder) {
        managedGroups.insert(region);
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

    addGroup("PROXY", groups.groups.at("PROXY"));
    addGroup("AUTO", groups.groups.at("AUTO"));
    for (const auto& region : groups.regionOrder) {
        addGroup(region, groups.groups.at(region));
    }
    root["proxy-groups"] = proxyGroups;

    if (config.profile == "bypass-cn") {
        ensureMihomoBypassCnProfile(root);
    } else {
        if (!root["rules"] || !root["rules"].IsSequence()) {
            root["rules"] = YAML::Node(YAML::NodeType::Sequence);
        }
        if (!hasMihomoRule(root["rules"], "MATCH,PROXY")) {
            root["rules"].push_back("MATCH,PROXY");
        }
    }

    YAML::Emitter emitter;
    emitter << root;
    result.ok = writeFile(outPath, emitter.c_str(), error);
    return result;
}

} // namespace subcli
