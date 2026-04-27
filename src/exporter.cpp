#include "subcli/exporter.hpp"

#include "exporter_internal.hpp"

namespace subcli {

ExportResult exportForTarget(
    ExportTarget target,
    const std::vector<ProxyNode>& nodes,
    const AppConfig& config,
    bool tun,
    const ResolvedProfile* profile,
    const std::string& outPath,
    std::string& error
) {
    switch (target) {
        case ExportTarget::Mihomo:
            return exportMihomoImpl(nodes, config, tun, profile, outPath, error);
        case ExportTarget::SingBox:
            return exportSingBoxImpl(nodes, config, tun, profile, outPath, error);
        case ExportTarget::Xray:
            return exportXrayImpl(nodes, config, tun, profile, outPath, error);
    }
    return {};
}

ExportResult exportForTarget(
    ExportTarget target,
    const std::vector<ProxyNode>& nodes,
    const AppConfig& config,
    bool tun,
    const std::string& outPath,
    std::string& error
) {
    return exportForTarget(target, nodes, config, tun, nullptr, outPath, error);
}

bool exportMihomo(
    const std::vector<ProxyNode>& nodes,
    const AppConfig& config,
    bool tun,
    const ResolvedProfile* profile,
    const std::string& outPath,
    std::string& error
) {
    return exportForTarget(ExportTarget::Mihomo, nodes, config, tun, profile, outPath, error).ok;
}

bool exportMihomo(
    const std::vector<ProxyNode>& nodes,
    const AppConfig& config,
    bool tun,
    const std::string& outPath,
    std::string& error
) {
    return exportMihomo(nodes, config, tun, nullptr, outPath, error);
}

bool exportSingBox(
    const std::vector<ProxyNode>& nodes,
    const AppConfig& config,
    bool tun,
    const ResolvedProfile* profile,
    const std::string& outPath,
    std::string& error
) {
    return exportForTarget(ExportTarget::SingBox, nodes, config, tun, profile, outPath, error).ok;
}

bool exportSingBox(
    const std::vector<ProxyNode>& nodes,
    const AppConfig& config,
    bool tun,
    const std::string& outPath,
    std::string& error
) {
    return exportSingBox(nodes, config, tun, nullptr, outPath, error);
}

bool exportXray(
    const std::vector<ProxyNode>& nodes,
    const AppConfig& config,
    bool tun,
    const ResolvedProfile* profile,
    const std::string& outPath,
    std::string& error
) {
    return exportForTarget(ExportTarget::Xray, nodes, config, tun, profile, outPath, error).ok;
}

bool exportXray(
    const std::vector<ProxyNode>& nodes,
    const AppConfig& config,
    bool tun,
    const std::string& outPath,
    std::string& error
) {
    return exportXray(nodes, config, tun, nullptr, outPath, error);
}

} // namespace subcli
