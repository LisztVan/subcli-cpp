#include "subcli/diagnostic_service.hpp"

#include <sstream>

#include <nlohmann/json.hpp>

#include "subcli/registry.hpp"

namespace subcli {

namespace {

const char* severityName(DiagnosticSeverity severity) {
    switch (severity) {
    case DiagnosticSeverity::Info:
        return "info";
    case DiagnosticSeverity::Warning:
        return "warning";
    case DiagnosticSeverity::Error:
        return "error";
    }
    return "unknown";
}

} // namespace

DiagnosticReport buildDiagnosticReport(
    const AppConfig& config,
    const std::vector<Subscription>& subscriptions,
    const std::string& workspaceRoot
) {
    DiagnosticReport report;

    report.findings.push_back({"workspace.resolved", DiagnosticSeverity::Info, "workspace", "workspace path resolved", workspaceRoot});

    for (const auto& key : configKeyRegistry()) {
        report.findings.push_back({"config.key.registered", DiagnosticSeverity::Info, key.key, "registered config key", ""});
    }

    for (const auto& target : exportTargetRegistry()) {
        report.findings.push_back({"export.target.registered", DiagnosticSeverity::Info, target.id, "registered export target", ""});
    }

    if (!config.profilePath.empty() || !config.profile.empty()) {
        report.findings.push_back({"profile.configured", DiagnosticSeverity::Info, "profile", "profile is configured", config.profilePath});
    } else {
        report.findings.push_back({"profile.missing", DiagnosticSeverity::Warning, "profile", "no profile configured", ""});
    }

    for (const auto& kv : config.templateNormal) {
        if (!kv.second.empty()) {
            report.findings.push_back({"template.configured", DiagnosticSeverity::Info, kv.first + ".normal", "template path configured", kv.second});
        }
    }
    for (const auto& kv : config.templateTun) {
        if (!kv.second.empty()) {
            report.findings.push_back({"template.configured", DiagnosticSeverity::Info, kv.first + ".tun", "template path configured", kv.second});
        }
    }
    for (const auto& kv : config.assetPaths) {
        if (!kv.second.empty()) {
            report.findings.push_back({"asset.path.configured", DiagnosticSeverity::Info, kv.first, "asset path configured", kv.second});
        }
    }
    for (const auto& kv : config.assetUrls) {
        if (!kv.second.empty()) {
            report.findings.push_back({"asset.url.configured", DiagnosticSeverity::Info, kv.first, "asset url configured", kv.second});
        }
    }

    for (const auto& sub : subscriptions) {
        report.findings.push_back({
            sub.enabled ? "subscription.enabled" : "subscription.disabled",
            DiagnosticSeverity::Info,
            sub.id.empty() ? sub.name : sub.id,
            sub.enabled ? "subscription enabled" : "subscription disabled",
            ""
        });
        if (!sub.lastError.empty()) {
            report.findings.push_back({
                "subscription.last_error",
                DiagnosticSeverity::Warning,
                sub.id.empty() ? sub.name : sub.id,
                sub.lastError,
                ""
            });
        }
    }

    for (const auto& finding : report.findings) {
        if (finding.severity == DiagnosticSeverity::Error) {
            report.hasError = true;
            break;
        }
    }
    return report;
}

nlohmann::json diagnosticReportToJson(const DiagnosticReport& report) {
    nlohmann::json findings = nlohmann::json::array();
    for (const auto& finding : report.findings) {
        findings.push_back({
            {"code", finding.code},
            {"severity", severityName(finding.severity)},
            {"subject", finding.subject},
            {"message", finding.message},
            {"path", finding.path},
        });
    }
    return {
        {"ok", !report.hasError},
        {"findings", findings},
    };
}

std::string diagnosticReportToText(const DiagnosticReport& report) {
    std::ostringstream out;
    for (const auto& finding : report.findings) {
        out << "[" << severityName(finding.severity) << "] " << finding.code;
        if (!finding.subject.empty()) {
            out << " subject=" << finding.subject;
        }
        if (!finding.path.empty()) {
            out << " path=" << finding.path;
        }
        if (!finding.message.empty()) {
            out << " message=" << finding.message;
        }
        out << "\n";
    }
    out << "doctor summary: failed=" << (report.hasError ? 1 : 0) << "\n";
    return out.str();
}

} // namespace subcli
