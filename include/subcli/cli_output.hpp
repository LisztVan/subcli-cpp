#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "subcli/models.hpp"

namespace subcli {

nlohmann::json makeStatusJson(const std::string& status, const std::string& message);
nlohmann::json diagnosticMessagesToJson(const std::vector<DiagnosticMessage>& messages);
void printJsonLine(const nlohmann::json& value);

} // namespace subcli
