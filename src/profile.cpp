#include "subcli/profile.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

#include "exporter_internal.hpp"

namespace subcli {
namespace {

using json = nlohmann::json;

bool isBuiltInProfileName(const std::string& name) {
    return name == "bypass-cn" || name == "global" || name == "direct";
}

std::string resolveBuiltInProfilePath(const std::filesystem::path& profilePath, const std::string& profileName) {
    const auto sourceBuiltIn = std::filesystem::path(SUBCLI_SOURCE_DIR) / "profiles" / (profileName + ".json");
    std::error_code ec;
    if (std::filesystem::exists(sourceBuiltIn, ec) && !ec) {
        return sourceBuiltIn.string();
    }
    return (profilePath.parent_path() / "profiles" / (profileName + ".json")).string();
}

bool templatePolicyPathsConflict(const std::string& a, const std::string& b) {
    if (a == b) {
        return true;
    }
    if (a.size() < b.size() && b.compare(0, a.size(), a) == 0 && b[a.size()] == '.') {
        return true;
    }
    if (b.size() < a.size() && a.compare(0, b.size(), b) == 0 && a[b.size()] == '.') {
        return true;
    }
    return false;
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

    if (root.contains("extends")) {
        if (!root["extends"].is_string()) {
            error = "profile extends must be a string";
            return false;
        }
        const std::string extendsName = root["extends"].get<std::string>();
        if (!isBuiltInProfileName(extendsName)) {
            error = "unsupported profile extends: " + extendsName;
            return false;
        }
        std::string extendsError;
        const auto extendsPath = resolveBuiltInProfilePath(std::filesystem::path(path), extendsName);
        if (!loadProfile(extendsPath, parsed, extendsError)) {
            error = "failed to load extended profile " + extendsName + ": " + extendsError;
            return false;
        }
        parsed.extends = extendsName;
    }

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
    if (root.contains("description") && root["description"].is_string()) {
        parsed.description = root["description"].get<std::string>();
    }
    if (root.contains("default_outbound") && root["default_outbound"].is_string()) {
        parsed.defaultOutbound = root["default_outbound"].get<std::string>();
    }

    const auto dnsIt = root.find("dns");
    if (dnsIt != root.end()) {
        if (!dnsIt->is_object()) {
            error = "profile dns must be an object";
            return false;
        }
        if (dnsIt->contains("mode") && (*dnsIt)["mode"].is_string()) {
            parsed.dns.mode = (*dnsIt)["mode"].get<std::string>();
        }
        if (dnsIt->contains("strategy") && (*dnsIt)["strategy"].is_string()) {
            parsed.dns.strategy = (*dnsIt)["strategy"].get<std::string>();
        }
        if (dnsIt->contains("direct_servers")) {
            parsed.dns.directServers = readStringArray(*dnsIt, "direct_servers");
        }
        if (dnsIt->contains("remote_servers")) {
            parsed.dns.remoteServers = readStringArray(*dnsIt, "remote_servers");
        }
    }

    const auto groupsIt = root.find("groups");
    if (groupsIt != root.end()) {
        if (!groupsIt->is_array()) {
            error = "profile groups must be an array";
            return false;
        }
        parsed.groups.clear();
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
        parsed.rules.clear();
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

    const auto templatePolicyIt = root.find("template_policy");
    if (templatePolicyIt != root.end()) {
        if (!templatePolicyIt->is_object()) {
            error = "profile template_policy must be an object";
            return false;
        }
        const auto targetsIt = templatePolicyIt->find("targets");
        if (targetsIt != templatePolicyIt->end()) {
            if (!targetsIt->is_object()) {
                error = "profile template_policy.targets must be an object";
                return false;
            }
            for (auto targetIt = targetsIt->begin(); targetIt != targetsIt->end(); ++targetIt) {
                const std::string target = targetIt.key();
                ExportTarget parsedTarget;
                if (!parseTemplatePolicyTarget(target, parsedTarget)) {
                    error = "unsupported template_policy target: " + target;
                    return false;
                }
                if (!targetIt.value().is_object()) {
                    error = "profile template_policy.targets." + target + " must be an object";
                    return false;
                }
                const auto pathsIt = targetIt.value().find("paths");
                if (pathsIt == targetIt.value().end()) {
                    continue;
                }
                if (!pathsIt->is_object()) {
                    error = "profile template_policy.targets." + target + ".paths must be an object";
                    return false;
                }
                std::vector<std::string> seenPaths;
                for (auto pathIt = pathsIt->begin(); pathIt != pathsIt->end(); ++pathIt) {
                    const std::string path = pathIt.key();
                    for (const auto& seenPath : seenPaths) {
                        if (templatePolicyPathsConflict(path, seenPath)) {
                            error = "conflicting template_policy paths for " + target + ": " + seenPath + " and " + path;
                            return false;
                        }
                    }
                    if (!isTemplatePolicyPathSupported(parsedTarget, path)) {
                        error = "unsupported template_policy path for " + target + ": " + path;
                        return false;
                    }
                    if (!pathIt.value().is_string()) {
                        error = "template_policy action must be a string for path: " + path;
                        return false;
                    }
                    const std::string action = pathIt.value().get<std::string>();
                    TemplatePolicyAction parsedAction;
                    if (!parseTemplatePolicyAction(action, parsedAction)) {
                        error = "unsupported template_policy action: " + action;
                        return false;
                    }
                    if (!isTemplatePolicyActionSupportedForPath(parsedTarget, path, parsedAction)) {
                        error = "unsupported template_policy action for " + target + " path " + path + ": " + action;
                        return false;
                    }
                    parsed.templatePolicy.targets[target].pathActions[path] = action;
                    seenPaths.push_back(path);
                }
            }
        }
    }

    profile = parsed;
    return true;
}

} // namespace subcli
