#pragma once

#include <string>
#include <vector>

namespace subcli {

enum class ConfigValueType {
    String,
    Integer,
    Boolean,
    Path,
    Prefix,
};

struct CommandDescriptor {
    std::string name;
    std::string summary;
    std::vector<std::string> options;
};

struct ConfigKeyDescriptor {
    std::string key;
    ConfigValueType valueType = ConfigValueType::String;
    std::string summary;
};

struct ExportTargetDescriptor {
    std::string id;
    std::string summary;
    std::string outputFile;
    std::string coreConfigKey;
    std::string normalTemplateKey;
    std::string tunTemplateKey;
};

const std::vector<CommandDescriptor>& commandRegistry();
const std::vector<ConfigKeyDescriptor>& configKeyRegistry();
const std::vector<ExportTargetDescriptor>& exportTargetRegistry();

std::vector<std::string> allCommandNames();
std::vector<std::string> allConfigKeyNames();
std::vector<std::string> allExportTargetIds();

const CommandDescriptor* findCommandDescriptor(const std::string& name);
const ConfigKeyDescriptor* findConfigKeyDescriptor(const std::string& key);
const ExportTargetDescriptor* findExportTargetDescriptor(const std::string& id);

} // namespace subcli
