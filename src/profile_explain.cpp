#include "subcli/profile_explain.hpp"

#include <algorithm>
#include <filesystem>
#include <set>
#include <sstream>

#include "exporter_internal.hpp"
#include "subcli/capability_matrix.hpp"
#include "subcli/util.hpp"

namespace subcli {
namespace {

std::vector<CapabilityFinding> assessCapabilitiesForExplain(
    ExportTarget target,
    const ResolvedProfile& profile,
    const ProfileExplainOptions& options
) {
    if (options.config != nullptr) {
        return assessProfileCapabilities(target, profile, *options.config);
    }
    return assessProfileCapabilities(target, profile);
}

std::string targetName(ExportTarget target) {
    return templatePolicyTargetKey(target);
}

std::vector<std::string> sortedUnique(std::vector<std::string> values) {
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
    return values;
}

nlohmann::json groupToJson(const ProfileGroup& group) {
    return {
        {"tag", group.tag},
        {"type", group.type},
        {"members", group.members},
        {"default", group.defaultMember},
        {"url", group.url},
        {"interval", group.interval},
        {"strategy", group.strategy},
    };
}

nlohmann::json groupsToJson(const std::vector<ProfileGroup>& groups) {
    nlohmann::json out = nlohmann::json::array();
    for (const auto& group : groups) {
        out.push_back(groupToJson(group));
    }
    return out;
}

nlohmann::json ruleToJson(const ProfileRule& rule) {
    return {
        {"type", rule.type},
        {"value", rule.value},
        {"outbound", rule.outbound},
        {"domains", rule.domains},
        {"ip_cidrs", rule.ipCidrs},
        {"ports", rule.ports},
        {"networks", rule.networks},
    };
}

nlohmann::json rulesToJson(const std::vector<ProfileRule>& rules) {
    nlohmann::json out = nlohmann::json::array();
    for (const auto& rule : rules) {
        out.push_back(ruleToJson(rule));
    }
    return out;
}

nlohmann::json templatePolicyToJson(const ProfileTemplatePolicy& policy) {
    nlohmann::json out = nlohmann::json::object();
    for (const auto& targetEntry : policy.targets) {
        for (const auto& pathEntry : targetEntry.second.pathActions) {
            out[targetEntry.first][pathEntry.first] = pathEntry.second;
        }
    }
    return out;
}

std::vector<std::string> profileOverrideSummary(const ResolvedProfile& profile) {
    std::vector<std::string> overrides;
    if (profile.extends.empty()) {
        return overrides;
    }

    ResolvedProfile base;
    std::string error;
    const auto basePath = (std::filesystem::path(SUBCLI_SOURCE_DIR) / "profiles" / (profile.extends + ".json")).string();
    if (!loadProfile(basePath, base, error)) {
        overrides.push_back("unable to load base profile for override diff");
        return overrides;
    }

    if (profile.description != base.description) {
        overrides.push_back("description");
    }
    if (profile.defaultOutbound != base.defaultOutbound) {
        overrides.push_back("default_outbound");
    }
    if (profile.dns.mode != base.dns.mode) {
        overrides.push_back("dns.mode");
    }
    if (profile.dns.strategy != base.dns.strategy) {
        overrides.push_back("dns.strategy");
    }
    if (profile.dns.directServers != base.dns.directServers) {
        overrides.push_back("dns.direct_servers");
    }
    if (profile.dns.remoteServers != base.dns.remoteServers) {
        overrides.push_back("dns.remote_servers");
    }
    if (groupsToJson(profile.groups) != groupsToJson(base.groups)) {
        overrides.push_back("groups");
    }
    if (rulesToJson(profile.rules) != rulesToJson(base.rules)) {
        overrides.push_back("rules");
    }
    if (templatePolicyToJson(profile.templatePolicy) != templatePolicyToJson(base.templatePolicy)) {
        overrides.push_back("template_policy");
    }
    return sortedUnique(overrides);
}

std::vector<std::string> requiredAssetsForProfileTarget(const ResolvedProfile& profile, ExportTarget target) {
    std::set<std::string> required;
    for (const auto& rule : profile.rules) {
        const std::string type = toLower(rule.type);
        const std::string value = toLower(rule.value);
        if (target == ExportTarget::Mihomo) {
            if (type == "geosite") {
                required.insert("mihomo.geosite");
            }
            if (type == "geoip") {
                required.insert("mihomo.geoip");
            }
        }
        if (target == ExportTarget::SingBox) {
            if (type == "geosite" && value == "cn") {
                required.insert("sing-box.geosite-cn");
            }
            if (type == "geoip" && value == "cn") {
                required.insert("sing-box.geoip-cn");
            }
        }
        if (target == ExportTarget::Xray) {
            if (type == "geosite") {
                required.insert("xray.geosite");
            }
            if (type == "geoip") {
                required.insert("xray.geoip");
            }
        }
    }
    return std::vector<std::string>(required.begin(), required.end());
}

std::vector<std::string> targetNotesForProfile(const ResolvedProfile& profile, ExportTarget target) {
    std::vector<std::string> notes;
    if (target == ExportTarget::SingBox) {
        bool hasFallback = false;
        bool hasLoadBalance = false;
        for (const auto& group : profile.groups) {
            const std::string type = toLower(group.type);
            hasFallback = hasFallback || type == "fallback";
            hasLoadBalance = hasLoadBalance || type == "load-balance" || type == "loadbalance";
        }
        if (hasFallback) {
            notes.push_back("fallback groups render as urltest");
        }
        if (hasLoadBalance) {
            notes.push_back("load-balance groups render as selector");
        }
    }
    if (target == ExportTarget::Xray) {
        notes.push_back("profile groups render as balancers");
        notes.push_back("xray generated node outbounds use managed SUBCLI_* tags");
    }
    return notes;
}

std::vector<std::string> supportedPathsForTarget(ExportTarget target) {
    if (target == ExportTarget::SingBox) {
        return {"outbounds", "dns", "dns.servers", "dns.rules", "route.rules", "route.rule_set"};
    }
    if (target == ExportTarget::Xray) {
        return {"outbounds", "dns", "dns.servers", "routing.rules", "routing.balancers"};
    }
    return {"proxies", "proxy-groups", "rules", "dns", "dns.nameserver", "dns.fallback"};
}

std::string renderRuleSummary(const ProfileRule& rule) {
    const std::string type = toLower(rule.type);
    if (type == "final" || type == "match") {
        return type + " -> " + (rule.outbound.empty() ? "-" : rule.outbound);
    }
    std::string value = rule.value;
    if (value.empty()) {
        if (!rule.domains.empty()) {
            value = "[domains:" + std::to_string(rule.domains.size()) + "]";
        } else if (!rule.ipCidrs.empty()) {
            value = "[ip_cidrs:" + std::to_string(rule.ipCidrs.size()) + "]";
        } else if (!rule.ports.empty()) {
            value = "[ports:" + std::to_string(rule.ports.size()) + "]";
        } else if (!rule.networks.empty()) {
            value = "[networks:" + std::to_string(rule.networks.size()) + "]";
        } else {
            value = "-";
        }
    }
    return type + " " + value + " -> " + (rule.outbound.empty() ? "-" : rule.outbound);
}

} // namespace

std::string explainProfileText(const ResolvedProfile& profile, const ProfileExplainOptions& options) {
    std::ostringstream out;
    out << "profile: " << profile.name << "\n";
    out << "extends: " << (profile.extends.empty() ? "-" : profile.extends) << "\n";
    out << "description: " << (profile.description.empty() ? "-" : profile.description) << "\n";
    out << "default_outbound: " << profile.defaultOutbound << "\n\n";

    out << "dns:\n";
    out << "  mode: " << (profile.dns.mode.empty() ? "-" : profile.dns.mode) << "\n";
    out << "  strategy: " << (profile.dns.strategy.empty() ? "-" : profile.dns.strategy) << "\n";
    out << "  direct_servers:\n";
    if (profile.dns.directServers.empty()) {
        out << "    - -\n";
    } else {
        for (const auto& server : profile.dns.directServers) {
            out << "    - " << server << "\n";
        }
    }
    out << "  remote_servers:\n";
    if (profile.dns.remoteServers.empty()) {
        out << "    - -\n";
    } else {
        for (const auto& server : profile.dns.remoteServers) {
            out << "    - " << server << "\n";
        }
    }

    out << "\ngroups:\n";
    if (profile.groups.empty()) {
        out << "  none\n";
    } else {
        for (const auto& group : profile.groups) {
            out << "  " << group.tag << " " << group.type << " -> ";
            if (group.members.empty()) {
                out << "-";
            } else {
                for (size_t i = 0; i < group.members.size(); ++i) {
                    if (i) {
                        out << ", ";
                    }
                    out << group.members[i];
                }
            }
            out << "\n";
        }
    }
    out << "\nmember_selectors:\n";
    out << "  REGION:* => expand to all generated regions\n";
    out << "  REGION:<name> => expand one generated region when present\n";
    out << "  NODE:* => expand to all generated node names\n";
    out << "  SOURCE:* => expand to all generated node names\n";
    out << "  SOURCE:<id> => expand nodes from one subscription source id\n";
    out << "  TAG:<tag> => expand nodes from subscriptions carrying that tag\n";
    out << "  PROTOCOL:<name> => expand nodes matching canonical protocol\n";

    out << "\nrules:\n";
    if (profile.rules.empty()) {
        out << "  none\n";
    } else {
        for (const auto& rule : profile.rules) {
            out << "  " << renderRuleSummary(rule) << "\n";
        }
    }

    out << "\ntemplate_policy:\n";
    if (profile.templatePolicy.targets.empty()) {
        out << "  none\n";
    } else {
        std::vector<std::string> entries;
        for (const auto& targetEntry : profile.templatePolicy.targets) {
            for (const auto& pathEntry : targetEntry.second.pathActions) {
                entries.push_back(targetEntry.first + "." + pathEntry.first + " = " + pathEntry.second);
            }
        }
        entries = sortedUnique(entries);
        for (const auto& entry : entries) {
            out << "  " << entry << "\n";
        }
    }

    if (!profile.extends.empty()) {
        const auto overrides = profileOverrideSummary(profile);
        out << "\noverrides:\n";
        if (overrides.empty()) {
            out << "  none\n";
        } else {
            for (const auto& item : overrides) {
                out << "  " << item << "\n";
            }
        }
    }

    auto renderTargetSection = [&](ExportTarget target) {
        out << "\ntarget: " << targetName(target) << "\n";
        const auto assets = requiredAssetsForProfileTarget(profile, target);
        out << "\nrequired_assets:\n";
        if (assets.empty()) {
            out << "  none\n";
        } else {
            for (const auto& asset : assets) {
                out << "  " << asset << "\n";
            }
        }
        const auto notes = targetNotesForProfile(profile, target);
        out << "\ntarget_notes:\n";
        if (notes.empty()) {
            out << "  none\n";
        } else {
            for (const auto& note : notes) {
                out << "  " << note << "\n";
            }
        }
        const auto capabilities = assessCapabilitiesForExplain(target, profile, options);
        out << "\ncapabilities:\n";
        if (capabilities.empty()) {
            out << "  none\n";
        } else {
            for (const auto& item : capabilities) {
                std::string level = "native";
                if (item.level == CapabilityLevel::Degraded) {
                    level = "degraded";
                } else if (item.level == CapabilityLevel::Unsupported) {
                    level = "unsupported";
                } else if (item.level == CapabilityLevel::RequiresAsset) {
                    level = "requires_asset";
                }
                out << "  " << item.subject << ": " << level << "\n";
            }
        }

        out << "\ntemplate_policy_effective:\n";
        const std::string key = templatePolicyTargetKey(target);
        const auto targetIt = profile.templatePolicy.targets.find(key);
        const auto paths = supportedPathsForTarget(target);
        for (const auto& path : paths) {
            std::string action = "default exporter behavior";
            if (targetIt != profile.templatePolicy.targets.end()) {
                const auto actionIt = targetIt->second.pathActions.find(path);
                if (actionIt != targetIt->second.pathActions.end()) {
                    action = actionIt->second;
                }
            }
            out << "  " << path << ": " << action << "\n";
        }
    };

    if (options.allTargets) {
        renderTargetSection(ExportTarget::Mihomo);
        renderTargetSection(ExportTarget::SingBox);
        renderTargetSection(ExportTarget::Xray);
    } else if (options.hasTarget) {
        renderTargetSection(options.target);
    }

    return out.str();
}

nlohmann::json explainProfileJson(const ResolvedProfile& profile, const ProfileExplainOptions& options) {
    nlohmann::json result;
    result["profile"] = {
        {"name", profile.name},
        {"extends", profile.extends},
        {"description", profile.description},
        {"default_outbound", profile.defaultOutbound},
    };
    result["overrides"] = profileOverrideSummary(profile);
    result["dns"] = {
        {"mode", profile.dns.mode},
        {"strategy", profile.dns.strategy},
        {"direct_servers", profile.dns.directServers},
        {"remote_servers", profile.dns.remoteServers},
    };

    result["groups"] = nlohmann::json::array();
    for (const auto& group : profile.groups) {
        result["groups"].push_back(groupToJson(group));
    }

    result["rules"] = nlohmann::json::array();
    for (const auto& rule : profile.rules) {
        result["rules"].push_back(ruleToJson(rule));
    }

    result["template_policy"] = nlohmann::json::array();
    for (const auto& targetEntry : profile.templatePolicy.targets) {
        for (const auto& pathEntry : targetEntry.second.pathActions) {
            result["template_policy"].push_back(
                {
                    {"target", targetEntry.first},
                    {"path", pathEntry.first},
                    {"action", pathEntry.second},
                }
            );
        }
    }

    if (!options.hasTarget && !options.allTargets) {
        result["target"] = nullptr;
        return result;
    }

    if (options.allTargets) {
        result["targets"] = nlohmann::json::array();
        for (const auto targetValue : {ExportTarget::Mihomo, ExportTarget::SingBox, ExportTarget::Xray}) {
            nlohmann::json target;
            target["name"] = targetName(targetValue);
            target["required_assets"] = requiredAssetsForProfileTarget(profile, targetValue);
            target["notes"] = targetNotesForProfile(profile, targetValue);
            nlohmann::json capabilities = nlohmann::json::array();
            for (const auto& item : assessCapabilitiesForExplain(targetValue, profile, options)) {
                std::string level = "native";
                if (item.level == CapabilityLevel::Degraded) {
                    level = "degraded";
                } else if (item.level == CapabilityLevel::Unsupported) {
                    level = "unsupported";
                } else if (item.level == CapabilityLevel::RequiresAsset) {
                    level = "requires_asset";
                }
                capabilities.push_back({{"subject", item.subject}, {"code", item.code}, {"level", level}});
            }
            target["capabilities"] = capabilities;
            result["targets"].push_back(target);
        }
        result["target"] = nullptr;
        return result;
    }

    nlohmann::json target;
    target["name"] = targetName(options.target);
    target["required_assets"] = requiredAssetsForProfileTarget(profile, options.target);
    target["notes"] = targetNotesForProfile(profile, options.target);

    target["effective_policy"] = nlohmann::json::array();
    const std::string key = templatePolicyTargetKey(options.target);
    const auto targetIt = profile.templatePolicy.targets.find(key);
    const auto paths = supportedPathsForTarget(options.target);
    for (const auto& path : paths) {
        std::string action = "default exporter behavior";
        bool explicitAction = false;
        if (targetIt != profile.templatePolicy.targets.end()) {
            const auto actionIt = targetIt->second.pathActions.find(path);
            if (actionIt != targetIt->second.pathActions.end()) {
                action = actionIt->second;
                explicitAction = true;
            }
        }
        target["effective_policy"].push_back({{"path", path}, {"action", action}, {"explicit", explicitAction}});
    }
    result["target"] = target;
    return result;
}

} // namespace subcli
