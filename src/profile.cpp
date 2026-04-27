#include "subcli/profile.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

namespace subcli {
namespace {

using json = nlohmann::json;

bool isBuiltInProfileName(const std::string& name) {
    return name == "bypass-cn" || name == "global" || name == "direct";
}

std::string readString(const json& object, const char* key, const std::string& fallback = "") {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_string()) {
        return fallback;
    }
    return it->get<std::string>();
}

std::vector<std::string> readStringArray(const json& object, const char* key) {
    std::vector<std::string> values;
    const auto it = object.find(key);
    if (it == object.end() || !it->is_array()) {
        return values;
    }
    for (const auto& item : *it) {
        if (item.is_string()) {
            values.push_back(item.get<std::string>());
        } else if (item.is_number_integer()) {
            values.push_back(std::to_string(item.get<int>()));
        }
    }
    return values;
}

} // namespace

bool resolveExportProfilePath(const AppConfig& config, const std::string& profilesDir, const std::string& requestedProfile, std::string& path) {
    path.clear();
    if (!config.profilePath.empty() && requestedProfile.empty()) {
        path = config.profilePath;
        return true;
    }

    const std::string profileName = requestedProfile.empty() ? config.profile : requestedProfile;
    if (isBuiltInProfileName(profileName)) {
        path = (std::filesystem::path(profilesDir) / (profileName + ".json")).string();
        return true;
    }
    if (!requestedProfile.empty()) {
        path = requestedProfile;
        return true;
    }
    return false;
}

bool loadExportProfile(const AppConfig& config, const std::string& profilesDir, const std::string& requestedProfile, ResolvedProfile& profile, bool& loaded, std::string& error) {
    loaded = false;
    error.clear();

    std::string path;
    if (!resolveExportProfilePath(config, profilesDir, requestedProfile, path)) {
        return true;
    }

    std::string loadError;
    if (!loadProfile(path, profile, loadError)) {
        error = "failed to load export profile " + path + ": " + loadError;
        return false;
    }

    loaded = true;
    return true;
}

bool loadExportProfile(const AppConfig& config, const std::string& profilesDir, ResolvedProfile& profile, bool& loaded, std::string& error) {
    return loadExportProfile(config, profilesDir, "", profile, loaded, error);
}

bool loadExportProfile(const AppConfig& config, const std::string& profilesDir, ResolvedProfile& profile, std::string& error) {
    bool loaded = false;
    return loadExportProfile(config, profilesDir, profile, loaded, error);
}

bool loadProfile(const std::string& path, ResolvedProfile& profile, std::string& error) {
    error.clear();

    std::ifstream in(path);
    if (!in) {
        error = "failed to open profile: " + path;
        return false;
    }

    json root;
    try {
        in >> root;
    } catch (const json::parse_error& ex) {
        error = std::string("invalid profile JSON: ") + ex.what();
        return false;
    }

    if (!root.is_object()) {
        error = "profile JSON must be an object";
        return false;
    }

    ResolvedProfile parsed;

    if (root.contains("version")) {
        if (!root["version"].is_number_integer()) {
            error = "profile version must be 1";
            return false;
        }
        parsed.version = root["version"].get<int>();
        if (parsed.version != 1) {
            error = "unsupported profile version: " + std::to_string(parsed.version);
            return false;
        }
    }

    parsed.name = readString(root, "name");
    if (parsed.name.empty()) {
        error = "profile name is required";
        return false;
    }
    parsed.description = readString(root, "description");
    parsed.defaultOutbound = readString(root, "default_outbound", parsed.defaultOutbound);

    const auto dnsIt = root.find("dns");
    if (dnsIt != root.end()) {
        if (!dnsIt->is_object()) {
            error = "profile dns must be an object";
            return false;
        }
        parsed.dns.mode = readString(*dnsIt, "mode");
        parsed.dns.strategy = readString(*dnsIt, "strategy");
        parsed.dns.directServers = readStringArray(*dnsIt, "direct_servers");
        parsed.dns.remoteServers = readStringArray(*dnsIt, "remote_servers");
    }

    const auto groupsIt = root.find("groups");
    if (groupsIt != root.end()) {
        if (!groupsIt->is_array()) {
            error = "profile groups must be an array";
            return false;
        }
        for (const auto& item : *groupsIt) {
            if (!item.is_object()) {
                error = "profile group must be an object";
                return false;
            }
            ProfileGroup group;
            group.tag = readString(item, "tag");
            group.type = readString(item, "type");
            if (group.tag.empty() || group.type.empty()) {
                error = "profile group requires tag and type";
                return false;
            }
            group.members = readStringArray(item, "members");
            group.defaultMember = readString(item, "default");
            group.url = readString(item, "url");
            if (item.contains("interval") && item["interval"].is_number_integer()) {
                group.interval = item["interval"].get<int>();
            }
            group.strategy = readString(item, "strategy");
            parsed.groups.push_back(group);
        }
    }

    const auto rulesIt = root.find("rules");
    if (rulesIt != root.end()) {
        if (!rulesIt->is_array()) {
            error = "profile rules must be an array";
            return false;
        }
        for (const auto& item : *rulesIt) {
            if (!item.is_object()) {
                error = "profile rule must be an object";
                return false;
            }
            ProfileRule rule;
            rule.type = readString(item, "type");
            rule.value = readString(item, "value");
            rule.outbound = readString(item, "outbound");
            rule.domains = readStringArray(item, "domains");
            rule.ipCidrs = readStringArray(item, "ip_cidrs");
            rule.ports = readStringArray(item, "ports");
            rule.networks = readStringArray(item, "networks");
            if (rule.type.empty() || (rule.type != "final" && rule.outbound.empty())) {
                error = "profile rule requires type and outbound";
                return false;
            }
            parsed.rules.push_back(rule);
        }
    }

    profile = parsed;
    return true;
}

} // namespace subcli
