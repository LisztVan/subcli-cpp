#include "subcli/store.hpp"

#include <yaml-cpp/yaml.h>

#include <fstream>
#include <stdexcept>

#include "subcli/util.hpp"
#include "subcli/tag_utils.hpp"

namespace subcli {

namespace {

Subscription parseSubscription(const YAML::Node& n) {
    Subscription s;
    s.id = n["id"].as<std::string>("");
    s.name = n["name"].as<std::string>("");
    s.url = n["url"].as<std::string>("");
    s.enabled = n["enabled"].as<bool>(true);
    s.group = n["group"].as<std::string>("default");
    s.formatHint = n["format_hint"].as<std::string>("auto");
    s.userAgent = n["user_agent"].as<std::string>("");
    s.timeout = n["timeout"].as<int>(15);
    s.timeoutOverride = n["timeout_override"].as<bool>(s.timeout != 15);
    s.retry = n["retry"].as<int>(2);
    s.retryOverride = n["retry_override"].as<bool>(s.retry != 2);
    s.updateInterval = n["update_interval"].as<int>(3600);
    s.lastUpdated = n["last_updated"].as<std::string>("");
    s.lastSuccess = n["last_success"].as<std::string>("");
    s.lastError = n["last_error"].as<std::string>("");
    s.etag = n["etag"].as<std::string>("");
    s.lastModified = n["last_modified"].as<std::string>("");
    s.cachePath = n["cache_path"].as<std::string>("subscriptions/" + s.id + ".cache");
    s.priority = n["priority"].as<int>(100);

    if (n["tags"] && n["tags"].IsSequence()) {
        for (const auto& t : n["tags"]) {
            s.tags.push_back(t.as<std::string>());
        }
        s.tags = normalizeTags(s.tags);
    }
    if (n["headers"] && n["headers"].IsMap()) {
        for (const auto& kv : n["headers"]) {
            s.headers[kv.first.as<std::string>()] = kv.second.as<std::string>();
        }
    }
    return s;
}

YAML::Node toYaml(const Subscription& s) {
    YAML::Node n;
    n["id"] = s.id;
    n["name"] = s.name;
    n["url"] = s.url;
    n["enabled"] = s.enabled;
    n["group"] = s.group;
    n["tags"] = YAML::Node(YAML::NodeType::Sequence);
    for (const auto& t : s.tags) {
        n["tags"].push_back(t);
    }
    n["format_hint"] = s.formatHint;
    n["user_agent"] = s.userAgent;
    n["timeout"] = s.timeout;
    n["timeout_override"] = s.timeoutOverride;
    n["retry"] = s.retry;
    n["retry_override"] = s.retryOverride;
    n["headers"] = YAML::Node(YAML::NodeType::Map);
    for (const auto& kv : s.headers) {
        n["headers"][kv.first] = kv.second;
    }
    n["update_interval"] = s.updateInterval;
    n["last_updated"] = s.lastUpdated;
    n["last_success"] = s.lastSuccess;
    n["last_error"] = s.lastError;
    n["etag"] = s.etag;
    n["last_modified"] = s.lastModified;
    n["cache_path"] = s.cachePath;
    n["priority"] = s.priority;
    return n;
}

} // namespace

std::vector<Subscription> loadSubscriptions(const std::string& path) {
    if (!fileExists(path)) {
        return {};
    }
    YAML::Node root;
    try {
        root = YAML::LoadFile(path);
    } catch (const std::exception& ex) {
        throw std::runtime_error("failed to load subscriptions from " + path + ": " + ex.what());
    }
    std::vector<Subscription> out;
    if (root["subscriptions"] && root["subscriptions"].IsSequence()) {
        for (const auto& n : root["subscriptions"]) {
            out.push_back(parseSubscription(n));
        }
    }
    return out;
}

void saveSubscriptions(const std::string& path, const std::vector<Subscription>& subs) {
    YAML::Node root;
    root["version"] = 1;
    root["subscriptions"] = YAML::Node(YAML::NodeType::Sequence);
    for (const auto& s : subs) {
        root["subscriptions"].push_back(toYaml(s));
    }

    YAML::Emitter out;
    out << root;
    std::ofstream f(path);
    if (!f) {
        throw std::runtime_error("failed to write " + path);
    }
    f << out.c_str();
}

AppConfig loadConfig(const std::string& path) {
    AppConfig c;
    if (!fileExists(path)) {
        return c;
    }

    YAML::Node root;
    try {
        root = YAML::LoadFile(path);
    } catch (const std::exception& ex) {
        throw std::runtime_error("failed to load config from " + path + ": " + ex.what());
    }
    c.tun = root["tun"].as<bool>(false);
    c.logLevel = root["log_level"].as<std::string>("info");
    c.parallelism = root["parallelism"].as<int>(4);
    c.timeout = root["timeout"].as<int>(15);
    c.retry = root["retry"].as<int>(2);
    c.fetchMaxBytes = root["fetch_max_bytes"].as<long>(10 * 1024 * 1024);
    c.templateDir = root["template_dir"].as<std::string>("./templates");
    c.outputDir = root["output_dir"].as<std::string>("./outputs");
    c.profile = root["profile"].as<std::string>("bypass-cn");
    c.profilePath = root["profile_path"].as<std::string>("");
    if (root["routing"] && root["routing"]["rules"] && root["routing"]["rules"].IsSequence()) {
        for (const auto& n : root["routing"]["rules"]) {
            AppConfig::RoutingRule rule;
            rule.type = n["type"].as<std::string>("");
            rule.value = n["value"].as<std::string>("");
            rule.outbound = n["outbound"].as<std::string>("");
            if (!rule.type.empty() && !rule.outbound.empty()) {
                c.routingRules.push_back(rule);
            }
        }
    }
    c.assetDir = root["asset_dir"].as<std::string>("./assets");
    if (root["core_paths"] && root["core_paths"].IsMap()) {
        c.mihomoPath = root["core_paths"]["mihomo"].as<std::string>("");
        c.singBoxPath = root["core_paths"]["sing_box"].as<std::string>("");
        c.xrayPath = root["core_paths"]["xray"].as<std::string>("");
    }
    c.dedupeNodes = root["node_management"] && root["node_management"]["dedupe"].IsDefined()
                        ? root["node_management"]["dedupe"].as<bool>(true)
                        : true;
    c.renameTemplate = root["node_management"] && root["node_management"]["rename_template"]
                           ? root["node_management"]["rename_template"].as<std::string>("{name}")
                           : "{name}";
    c.includeRegex = root["node_management"] && root["node_management"]["include_regex"]
                         ? root["node_management"]["include_regex"].as<std::string>("")
                         : "";
    c.excludeRegex = root["node_management"] && root["node_management"]["exclude_regex"]
                         ? root["node_management"]["exclude_regex"].as<std::string>("")
                         : "";
    c.sortBy = root["node_management"] && root["node_management"]["sort_by"]
                   ? root["node_management"]["sort_by"].as<std::string>("region,name")
                   : "region,name";

    if (root["templates"]) {
        for (const std::string& key : {"mihomo", "sing-box", "xray"}) {
            if (root["templates"][key]) {
                c.templateNormal[key] = root["templates"][key]["normal"].as<std::string>("");
                c.templateTun[key] = root["templates"][key]["tun"].as<std::string>("");
            }
        }
    }

    if (root["grouping"] && root["grouping"]["region_rules"]) {
        auto rr = root["grouping"]["region_rules"];
        for (const auto& kv : rr) {
            c.regionRules[kv.first.as<std::string>()] = kv.second.as<std::string>();
        }
    }
    if (root["grouping"] && root["grouping"]["strategy_groups"] && root["grouping"]["strategy_groups"].IsSequence()) {
        for (const auto& node : root["grouping"]["strategy_groups"]) {
            AppConfig::StrategyGroup group;
            group.name = node["name"].as<std::string>("");
            group.type = node["type"].as<std::string>("select");
            group.url = node["url"].as<std::string>("");
            group.interval = node["interval"].as<int>(300);
            group.defaultMember = node["default"].as<std::string>("");
            if (node["members"] && node["members"].IsSequence()) {
                for (const auto& member : node["members"]) {
                    group.members.push_back(member.as<std::string>());
                }
            }
            if (!group.name.empty()) {
                c.strategyGroups.push_back(group);
            }
        }
    }
    if (root["assets"] && root["assets"].IsMap()) {
        if (root["assets"]["paths"] && root["assets"]["paths"].IsMap()) {
            for (const auto& kv : root["assets"]["paths"]) {
                c.assetPaths[kv.first.as<std::string>()] = kv.second.as<std::string>();
            }
        }
        if (root["assets"]["urls"] && root["assets"]["urls"].IsMap()) {
            for (const auto& kv : root["assets"]["urls"]) {
                c.assetUrls[kv.first.as<std::string>()] = kv.second.as<std::string>();
            }
        }
    }
    return c;
}

void saveConfig(const std::string& path, const AppConfig& c) {
    YAML::Node root;
    root["version"] = 1;
    root["tun"] = c.tun;
    root["log_level"] = c.logLevel;
    root["parallelism"] = c.parallelism;
    root["timeout"] = c.timeout;
    root["retry"] = c.retry;
    root["fetch_max_bytes"] = c.fetchMaxBytes;
    root["template_dir"] = c.templateDir;
    root["output_dir"] = c.outputDir;
    root["profile"] = c.profile;
    root["profile_path"] = c.profilePath;
    root["routing"]["rules"] = YAML::Node(YAML::NodeType::Sequence);
    for (const auto& rule : c.routingRules) {
        YAML::Node node;
        node["type"] = rule.type;
        node["value"] = rule.value;
        node["outbound"] = rule.outbound;
        root["routing"]["rules"].push_back(node);
    }
    root["asset_dir"] = c.assetDir;
    root["core_paths"]["mihomo"] = c.mihomoPath;
    root["core_paths"]["sing_box"] = c.singBoxPath;
    root["core_paths"]["xray"] = c.xrayPath;
    root["node_management"]["dedupe"] = c.dedupeNodes;
    root["node_management"]["rename_template"] = c.renameTemplate;
    root["node_management"]["include_regex"] = c.includeRegex;
    root["node_management"]["exclude_regex"] = c.excludeRegex;
    root["node_management"]["sort_by"] = c.sortBy;

    for (const auto& key : {"mihomo", "sing-box", "xray"}) {
        root["templates"][key]["normal"] = c.templateNormal.count(key) ? c.templateNormal.at(key) : "";
        root["templates"][key]["tun"] = c.templateTun.count(key) ? c.templateTun.at(key) : "";
    }

    root["grouping"]["region_rules"] = YAML::Node(YAML::NodeType::Map);
    for (const auto& kv : c.regionRules) {
        root["grouping"]["region_rules"][kv.first] = kv.second;
    }
    root["grouping"]["strategy_groups"] = YAML::Node(YAML::NodeType::Sequence);
    for (const auto& group : c.strategyGroups) {
        YAML::Node node;
        node["name"] = group.name;
        node["type"] = group.type;
        node["url"] = group.url;
        node["interval"] = group.interval;
        node["default"] = group.defaultMember;
        node["members"] = YAML::Node(YAML::NodeType::Sequence);
        for (const auto& member : group.members) {
            node["members"].push_back(member);
        }
        root["grouping"]["strategy_groups"].push_back(node);
    }
    root["assets"]["paths"] = YAML::Node(YAML::NodeType::Map);
    for (const auto& kv : c.assetPaths) {
        root["assets"]["paths"][kv.first] = kv.second;
    }
    root["assets"]["urls"] = YAML::Node(YAML::NodeType::Map);
    for (const auto& kv : c.assetUrls) {
        root["assets"]["urls"][kv.first] = kv.second;
    }

    YAML::Emitter out;
    out << root;
    std::ofstream f(path);
    if (!f) {
        throw std::runtime_error("failed to write " + path);
    }
    f << out.c_str();
}

} // namespace subcli
