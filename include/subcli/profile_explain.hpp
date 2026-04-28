#pragma once

#include <string>

#include <nlohmann/json.hpp>

#include "subcli/exporter.hpp"
#include "subcli/models.hpp"
#include "subcli/profile.hpp"

namespace subcli {

struct ProfileExplainOptions {
    bool hasTarget = false;
    bool allTargets = false;
    ExportTarget target = ExportTarget::Mihomo;
    const AppConfig* config = nullptr;
};

std::string explainProfileText(const ResolvedProfile& profile, const ProfileExplainOptions& options);
nlohmann::json explainProfileJson(const ResolvedProfile& profile, const ProfileExplainOptions& options);

} // namespace subcli
