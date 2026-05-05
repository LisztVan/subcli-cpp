#include "subcli/capability_matrix.hpp"

#include "subcli/capabilities.hpp"
#include "subcli/registry.hpp"
#include "subcli/util.hpp"

#include <set>

namespace subcli {

namespace {

bool isPortableRuleType(const std::string& type) {
    return type == "geosite" || type == "geoip" || type == "domain" || type == "domain_suffix" || type == "domain_keyword" ||
           type == "ip_cidr" || type == "port" || type == "network" || type == "final" || type == "match";
}

std::vector<std::string> requiredAssetKeysForRule(ExportTarget target, const ProfileRule& rule) {
    std::vector<std::string> keys;
    const std::string type = toLower(rule.type);
    const std::string value = toLower(rule.value);
    if (target == ExportTarget::Mihomo) {
        if (type == "geosite") {
            keys.push_back("mihomo.geosite");
        }
        if (type == "geoip") {
            keys.push_back("mihomo.geoip");
        }
    } else if (target == ExportTarget::SingBox) {
        if (type == "geosite" && value == "cn") {
            keys.push_back("sing-box.geosite-cn");
        }
        if (type == "geoip" && value == "cn") {
            keys.push_back("sing-box.geoip-cn");
        }
    } else if (target == ExportTarget::Xray) {
        if (type == "geosite") {
            keys.push_back("xray.geosite");
        }
        if (type == "geoip") {
            keys.push_back("xray.geoip");
        }
    }
    return keys;
}

bool isFakeIpMode(const std::string& mode) {
    const std::string normalized = toLower(mode);
    return normalized == "fake-ip" || normalized == "fakeip";
}

CapabilityLevel profileGroupLevelForTarget(ExportTarget target, const std::string& type) {
    const std::string normalized = toLower(type);
    if (target == ExportTarget::Mihomo) {
        return CapabilityLevel::Native;
    }
    if (target == ExportTarget::SingBox) {
        if (normalized == "fallback" || normalized == "load-balance" || normalized == "loadbalance") {
            return CapabilityLevel::Degraded;
        }
        return CapabilityLevel::Native;
    }
    return CapabilityLevel::Degraded;
}

} // namespace

std::vector<CapabilityFinding> assessProfileCapabilities(ExportTarget target, const ResolvedProfile& profile) {
    std::vector<CapabilityFinding> findings;
    for (const auto& group : profile.groups) {
        if (group.tag.empty()) {
            continue;
        }
        const auto level = profileGroupLevelForTarget(target, group.type);
        findings.push_back(
            {target,
             level,
             "profile_group_type",
             group.tag,
             level == CapabilityLevel::Native ? "group type is natively supported" : "group type requires target-specific degradation"}
        );
    }
    return findings;
}

std::vector<CapabilityFinding> assessProfileCapabilities(ExportTarget target, const ResolvedProfile& profile, const AppConfig& config) {
    auto findings = assessProfileCapabilities(target, profile);
    std::set<std::string> seenRequiredAssets;

    for (const auto& rule : profile.rules) {
        const std::string normalizedType = toLower(rule.type);
        const std::string normalizedValue = toLower(rule.value);
        if (!isPortableRuleType(normalizedType)) {
            findings.push_back(
                {
                    target,
                    CapabilityLevel::Unsupported,
                    "route_rule_type",
                    rule.type,
                    "route rule type is not in portable set",
                }
            );
        }

        if (target == ExportTarget::SingBox && (normalizedType == "geosite" || normalizedType == "geoip") &&
            normalizedValue != "cn" && normalizedValue != "private") {
            findings.push_back(
                {
                    target,
                    CapabilityLevel::Unsupported,
                    "route_rule_type",
                    normalizedType + ":" + normalizedValue,
                    subcli::exportTargetId(target) + " v2.1 only supports cn/private for this geosite/geoip rule",
                }
            );
        }

        for (const auto& assetKey : requiredAssetKeysForRule(target, rule)) {
            const auto it = config.assetPaths.find(assetKey);
            if ((it == config.assetPaths.end() || it->second.empty()) && seenRequiredAssets.insert(assetKey).second) {
                findings.push_back(
                    {
                        target,
                        CapabilityLevel::RequiresAsset,
                        "requires_asset",
                        assetKey,
                        "required asset path is not configured",
                    }
                );
            }
        }
    }

    if (isFakeIpMode(profile.dns.mode)) {
        CapabilityLevel level = CapabilityLevel::Native;
        if (target == ExportTarget::SingBox || target == ExportTarget::Xray) {
            level = CapabilityLevel::Degraded;
        }
        findings.push_back(
            {
                target,
                level,
                "dns_mode",
                profile.dns.mode,
                level == CapabilityLevel::Native ? "dns mode is natively supported" : "dns mode requires target-specific degradation",
            }
        );
    }

    return findings;
}

std::vector<CapabilityFinding> assessNodeCapabilities(ExportTarget target, const std::vector<ProxyNode>& nodes) {
    std::vector<CapabilityFinding> findings;
    for (const auto& node : nodes) {
        std::string reason;
        if (!supportsNode(target, node, reason)) {
            findings.push_back({target, CapabilityLevel::Unsupported, "node_protocol", node.name, reason});
        }
    }
    return findings;
}

} // namespace subcli
