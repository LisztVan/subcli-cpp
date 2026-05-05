#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "subcli/diagnostic.hpp"
#include "subcli/models.hpp"

namespace subcli {

struct DiagnosticFinding {
    std::string code;
    DiagnosticSeverity severity = DiagnosticSeverity::Info;
    std::string subject;
    std::string message;
    std::string path;
};

struct DiagnosticReport {
    std::vector<DiagnosticFinding> findings;
    bool hasError = false;
};

DiagnosticReport buildDiagnosticReport(
    const AppConfig& config,
    const std::vector<Subscription>& subscriptions,
    const std::string& workspaceRoot
);
nlohmann::json diagnosticReportToJson(const DiagnosticReport& report);
std::string diagnosticReportToText(const DiagnosticReport& report);

} // namespace subcli
