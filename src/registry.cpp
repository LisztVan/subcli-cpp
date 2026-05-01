#include "subcli/registry.hpp"

#include <algorithm>

namespace subcli {

namespace {

const std::vector<CommandDescriptor> kCommands = {
    {"init", "Create config/data/cache/state/output directories.", {}},
    {"doctor", "Check dirs, templates, assets, and core paths.", {"--json"}},
    {"sub", "List/add/edit/remove/enable/disable/update/validate subscriptions; import/export/check/prune lifecycle.", {"--json", "--strict-network"}},
    {"config", "List/get/set/remove application settings.", {"--json"}},
    {"profile", "List/get/validate/explain export profiles.", {"--json", "--target"}},
    {"template", "List/get/set/reset/validate export templates.", {"--json"}},
    {"asset", "List/status/validate/update geo and rule assets.", {"--json"}},
    {"export", "Render all/mihomo/sing-box/xray configs.", {"--tun", "--check", "--check-timeout", "--output-dir", "--profile", "--sub", "--tag", "--strict-network", "--strict-capabilities", "--download-assets", "--explain-policy", "--json"}},
    {"workspace", "Init/status/use/unset/migrate/doctor workspace roots.", {"--json"}},
    {"check", "Validate exported config with installed core.", {"--file", "--output-dir", "--timeout"}},
    {"run", "Run one core with an exported config.", {"--file", "--output-dir"}},
    {"daemon", "Once/run/start/stop/status periodic helper.", {"--json"}},
    {"status", "Show helper process status.", {"--json"}},
    {"stop", "Stop a helper process.", {}},
    {"restart", "Restart a helper process.", {}},
    {"completion", "Generate shell completion scripts.", {"--shell"}},
};

const std::vector<ConfigKeyDescriptor> kConfigKeys = {
    {"profile", ConfigValueType::String, "Default profile name."},
    {"profile_path", ConfigValueType::Path, "Profile path override."},
    {"template_dir", ConfigValueType::Path, "Template directory root."},
    {"output_dir", ConfigValueType::Path, "Export output directory."},
    {"asset_dir", ConfigValueType::Path, "Asset directory root."},
    {"tun", ConfigValueType::Boolean, "Enable tun template mode."},
    {"log_level", ConfigValueType::String, "Log verbosity."},
    {"parallelism", ConfigValueType::Integer, "Parallel fetch workers."},
    {"timeout", ConfigValueType::Integer, "Default request timeout seconds."},
    {"retry", ConfigValueType::Integer, "Default request retry count."},
    {"fetch_max_bytes", ConfigValueType::Integer, "Maximum bytes fetched per source."},
    {"core_paths.mihomo", ConfigValueType::Path, "Mihomo core binary path."},
    {"core_paths.sing_box", ConfigValueType::Path, "sing-box core binary path."},
    {"core_paths.xray", ConfigValueType::Path, "xray core binary path."},
    {"node_management.dedupe", ConfigValueType::Boolean, "Drop duplicate nodes before export."},
    {"node_management.rename_template", ConfigValueType::String, "Node rename template."},
    {"node_management.include_regex", ConfigValueType::String, "Node include filter regex."},
    {"node_management.exclude_regex", ConfigValueType::String, "Node exclude filter regex."},
    {"node_management.sort_by", ConfigValueType::String, "Node sort order expression."},
    {"templates.", ConfigValueType::Prefix, "Template path override namespace."},
    {"templates.mihomo.normal", ConfigValueType::Path, "Mihomo normal template path."},
    {"templates.mihomo.tun", ConfigValueType::Path, "Mihomo tun template path."},
    {"templates.sing-box.normal", ConfigValueType::Path, "sing-box normal template path."},
    {"templates.sing-box.tun", ConfigValueType::Path, "sing-box tun template path."},
    {"templates.xray.normal", ConfigValueType::Path, "xray normal template path."},
    {"templates.xray.tun", ConfigValueType::Path, "xray tun template path."},
    {"grouping.region_rules.", ConfigValueType::Prefix, "Region matcher namespace."},
    {"assets.paths.", ConfigValueType::Prefix, "Asset file path namespace."},
    {"assets.urls.", ConfigValueType::Prefix, "Asset URL namespace."},
};

const std::vector<ExportTargetDescriptor> kExportTargets = {
    {ExportTarget::Mihomo, "mihomo", "Mihomo YAML config.", "mihomo.yaml", "core_paths.mihomo", "templates.mihomo.normal", "templates.mihomo.tun"},
    {ExportTarget::SingBox, "sing-box", "sing-box JSON config.", "sing-box.json", "core_paths.sing_box", "templates.sing-box.normal", "templates.sing-box.tun"},
    {ExportTarget::Xray, "xray", "xray JSON config.", "xray.json", "core_paths.xray", "templates.xray.normal", "templates.xray.tun"},
};

} // namespace

const std::vector<CommandDescriptor>& commandRegistry() {
    return kCommands;
}

const std::vector<ConfigKeyDescriptor>& configKeyRegistry() {
    return kConfigKeys;
}

const std::vector<ExportTargetDescriptor>& exportTargetRegistry() {
    return kExportTargets;
}

std::vector<std::string> allCommandNames() {
    std::vector<std::string> names;
    names.reserve(kCommands.size());
    for (const auto& entry : kCommands) {
        names.push_back(entry.name);
    }
    return names;
}

std::vector<std::string> allConfigKeyNames() {
    std::vector<std::string> names;
    names.reserve(kConfigKeys.size());
    for (const auto& entry : kConfigKeys) {
        names.push_back(entry.key);
    }
    return names;
}

std::vector<std::string> allExportTargetIds() {
    std::vector<std::string> ids;
    ids.reserve(kExportTargets.size());
    for (const auto& entry : kExportTargets) {
        ids.push_back(entry.id);
    }
    return ids;
}

const CommandDescriptor* findCommandDescriptor(const std::string& name) {
    const auto it = std::find_if(kCommands.begin(), kCommands.end(), [&name](const CommandDescriptor& entry) {
        return entry.name == name;
    });
    if (it == kCommands.end()) {
        return nullptr;
    }
    return &(*it);
}

const ConfigKeyDescriptor* findConfigKeyDescriptor(const std::string& key) {
    for (const auto& entry : kConfigKeys) {
        if (entry.valueType == ConfigValueType::Prefix) {
            if (key.rfind(entry.key, 0) == 0 && key.size() > entry.key.size()) {
                return &entry;
            }
            continue;
        }
        if (entry.key == key) {
            return &entry;
        }
    }
    return nullptr;
}

const ExportTargetDescriptor* findExportTargetDescriptor(const std::string& id) {
    const auto it = std::find_if(kExportTargets.begin(), kExportTargets.end(), [&id](const ExportTargetDescriptor& entry) {
        return entry.id == id;
    });
    if (it == kExportTargets.end()) {
        return nullptr;
    }
    return &(*it);
}

const ExportTargetDescriptor* findExportTargetDescriptor(ExportTarget target) {
    const auto it = std::find_if(kExportTargets.begin(), kExportTargets.end(), [target](const ExportTargetDescriptor& entry) {
        return entry.target == target;
    });
    if (it == kExportTargets.end()) {
        return nullptr;
    }
    return &(*it);
}

bool resolveExportTarget(const std::string& id, ExportTarget& target, const ExportTargetDescriptor*& descriptor) {
    descriptor = findExportTargetDescriptor(id);
    if (descriptor == nullptr) {
        return false;
    }
    target = descriptor->target;
    return true;
}

std::string exportTargetId(ExportTarget target) {
    const auto* descriptor = findExportTargetDescriptor(target);
    if (descriptor == nullptr) {
        return "";
    }
    return descriptor->id;
}

bool exportTargetOutputPath(const std::string& outputDir, ExportTarget target, std::string& outputPath) {
    const auto* descriptor = findExportTargetDescriptor(target);
    if (descriptor == nullptr || descriptor->outputFile.empty()) {
        return false;
    }
    outputPath = outputDir + "/" + descriptor->outputFile;
    return true;
}

} // namespace subcli
