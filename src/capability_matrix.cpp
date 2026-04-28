#include "subcli/capability_matrix.hpp"

#include "subcli/capabilities.hpp"
#include "subcli/util.hpp"

namespace subcli {

namespace {

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
