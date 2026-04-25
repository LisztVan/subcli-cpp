#include "subcli/cli_output.hpp"

#include <iostream>

namespace subcli {

nlohmann::json makeStatusJson(const std::string& status, const std::string& message) {
    return {{"status", status}, {"message", message}};
}

nlohmann::json diagnosticMessagesToJson(const std::vector<DiagnosticMessage>& messages) {
    nlohmann::json out = nlohmann::json::array();
    for (const auto& message : messages) {
        out.push_back({{"code", message.code}, {"message", message.message}});
    }
    return out;
}

void printJsonLine(const nlohmann::json& value) {
    std::cout << value.dump() << "\n";
}

} // namespace subcli
