#pragma once

#include <string>
#include <vector>

#include "subcli/models.hpp"

namespace subcli {

struct ProfileDns {
    std::string mode;
    std::string strategy;
    std::vector<std::string> directServers;
    std::vector<std::string> remoteServers;
};

struct ProfileGroup {
    std::string tag;
    std::string type;
    std::vector<std::string> members;
    std::string defaultMember;
    std::string url;
    int interval = 300;
    std::string strategy;
};

struct ProfileRule {
    std::string type;
    std::string value;
    std::string outbound;
    std::vector<std::string> domains;
    std::vector<std::string> ipCidrs;
    std::vector<std::string> ports;
    std::vector<std::string> networks;
};

struct ResolvedProfile {
    int version = 1;
    std::string name;
    std::string description;
    std::string defaultOutbound = "PROXY";
    ProfileDns dns;
    std::vector<ProfileGroup> groups;
    std::vector<ProfileRule> rules;
};

bool loadProfile(const std::string& path, ResolvedProfile& profile, std::string& error);
bool resolveExportProfilePath(const AppConfig& config, const std::string& profilesDir, const std::string& requestedProfile, std::string& path);
bool loadExportProfile(const AppConfig& config, const std::string& profilesDir, const std::string& requestedProfile, ResolvedProfile& profile, bool& loaded, std::string& error);
bool loadExportProfile(const AppConfig& config, const std::string& profilesDir, ResolvedProfile& profile, bool& loaded, std::string& error);
bool loadExportProfile(const AppConfig& config, const std::string& profilesDir, ResolvedProfile& profile, std::string& error);

} // namespace subcli
