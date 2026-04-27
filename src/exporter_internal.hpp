#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>
#include <yaml-cpp/yaml.h>

#include "subcli/models.hpp"
#include "subcli/profile.hpp"

namespace subcli {

struct GroupData {
    std::vector<std::string> regionOrder;
    std::map<std::string, std::vector<std::string>> groups;
};

std::string joinTemplatePath(const std::string& dir, const std::string& filename);
std::string resolveTemplatePath(const AppConfig& config, const std::string& configuredPath, const std::string& fallbackName);
std::vector<std::string> splitCommaValues(const std::string& input);
std::vector<ProxyNode> preprocessNodes(const std::vector<ProxyNode>& nodes, const AppConfig& config, std::vector<DiagnosticMessage>& warnings);
std::vector<ProxyNode> makeExportNodes(const std::vector<ProxyNode>& nodes);
GroupData buildGroups(const std::vector<ProxyNode>& nodes, const AppConfig& config);
std::vector<std::string> expandProfileMembers(
    const std::vector<std::string>& rawMembers,
    const GroupData& groups,
    const std::vector<ProxyNode>& exportNodes
);
std::set<std::string> generatedSingBoxTags(const std::vector<ProxyNode>& nodes, const GroupData& groups);
std::set<std::string> generatedXrayTags(const std::vector<ProxyNode>& nodes);
void removeJsonArrayObjectsByTag(nlohmann::json& array, const std::set<std::string>& tags);
bool hasRouteRuleForOutbound(const nlohmann::json& rules, const std::string& outbound);
bool hasXrayCatchAllRule(const nlohmann::json& rules, const std::string& target);
bool hasSingBoxDnsDirectRule(const nlohmann::json& rules);
bool hasMihomoRule(const YAML::Node& rules, const std::string& value);

YAML::Node makeMihomoProxy(const ProxyNode& n);
nlohmann::json makeSingBoxOutbound(const ProxyNode& n);
nlohmann::json makeSingBoxWireGuardEndpoint(const ProxyNode& n);
nlohmann::json makeXrayOutbound(const ProxyNode& n);

ExportResult exportMihomoImpl(const std::vector<ProxyNode>& nodes, const AppConfig& config, bool tun, const ResolvedProfile* profile, const std::string& outPath, std::string& error);
ExportResult exportSingBoxImpl(const std::vector<ProxyNode>& nodes, const AppConfig& config, bool tun, const ResolvedProfile* profile, const std::string& outPath, std::string& error);
ExportResult exportXrayImpl(const std::vector<ProxyNode>& nodes, const AppConfig& config, bool tun, const ResolvedProfile* profile, const std::string& outPath, std::string& error);

} // namespace subcli
