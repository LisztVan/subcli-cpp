#pragma once

#include <string>
#include <vector>

#include "subcli/exporter.hpp"
#include "subcli/models.hpp"
#include "subcli/profile.hpp"

namespace subcli {

enum class CapabilityLevel {
    Native,
    Degraded,
    Unsupported,
    RequiresAsset,
};

struct CapabilityFinding {
    ExportTarget target;
    CapabilityLevel level;
    std::string code;
    std::string subject;
    std::string message;
};

std::vector<CapabilityFinding> assessProfileCapabilities(ExportTarget target, const ResolvedProfile& profile);
std::vector<CapabilityFinding> assessProfileCapabilities(ExportTarget target, const ResolvedProfile& profile, const AppConfig& config);
std::vector<CapabilityFinding> assessNodeCapabilities(ExportTarget target, const std::vector<ProxyNode>& nodes);

} // namespace subcli
