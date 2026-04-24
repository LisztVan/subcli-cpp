#pragma once

#include <string>

namespace subcli {

enum class DiagnosticSeverity {
    Info,
    Warning,
    Error,
};

struct Diagnostic {
    DiagnosticSeverity severity = DiagnosticSeverity::Warning;
    std::string code;
    std::string target;
    std::string protocol;
    std::string node;
    std::string field;
    std::string message;
};

} // namespace subcli
