#pragma once

#include <set>
#include <string>
#include <vector>

#include "subcli/exporter.hpp"

namespace subcli {

struct ProtocolTargetInfo {
    bool official = false;
    bool exportReady = false;
    std::string outputName;
};

struct ProtocolInfo {
    std::string canonical;
    std::set<std::string> aliases;
    ProtocolTargetInfo mihomo;
    ProtocolTargetInfo singBox;
    ProtocolTargetInfo xray;
};

std::string targetName(ExportTarget target);
std::string canonicalProtocolName(const std::string& type);
const ProtocolInfo* protocolInfo(const std::string& type);
ProtocolTargetInfo protocolTargetInfo(ExportTarget target, const std::string& type);
bool isOfficialProtocolSupported(ExportTarget target, const std::string& type);
bool isProtocolExportReady(ExportTarget target, const std::string& type);
std::string targetProtocolName(ExportTarget target, const std::string& type);
std::vector<std::string> officialProtocolsForTarget(ExportTarget target);

} // namespace subcli
