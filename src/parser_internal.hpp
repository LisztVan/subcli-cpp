#pragma once

#include <map>
#include <string>

#include <nlohmann/json.hpp>
#include <yaml-cpp/yaml.h>

#include "subcli/models.hpp"

namespace subcli {

std::string b64Decode(const std::string& in);
std::string decodeUrlSafeB64(std::string s);
std::string trim(std::string v);
std::string urlDecode(const std::string& in);
std::map<std::string, std::string> parseQuery(const std::string& query);
bool parseBoolish(const std::string& value);
std::string joinJsonArray(const nlohmann::json& value);
bool parseHostPort(const std::string& hostPort, std::string& host, int& port);
std::string detectRegion(const std::string& name, const AppConfig& config);

void applyCommonUriFields(ProxyNode& p, const std::map<std::string, std::string>& query);
void applyTlsObject(ProxyNode& p, const nlohmann::json& tls);
void applyTransportObject(ProxyNode& p, const nlohmann::json& transport);
void applyXrayStreamSettings(ProxyNode& p, const nlohmann::json& streamSettings);

ProxyNode fromMihomoProxy(const YAML::Node& n, const std::string& sourceId, const AppConfig& config);
ProxyNode fromUri(const std::string& line, const std::string& sourceId, const AppConfig& config);

ParseResult parseMihomoSubscription(const std::string& content, const std::string& sourceId, const AppConfig& config);
ParseResult parseSingBoxSubscription(const std::string& content, const std::string& sourceId, const AppConfig& config);
ParseResult parseXraySubscription(const std::string& content, const std::string& sourceId, const AppConfig& config);
ParseResult parseUriSubscription(const std::string& content, const std::string& sourceId, const AppConfig& config);

} // namespace subcli
