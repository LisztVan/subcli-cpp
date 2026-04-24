#include "subcli/exporter.hpp"

#include "exporter_internal.hpp"

namespace subcli {

ExportResult exportForTarget(
    ExportTarget target,
    const std::vector<ProxyNode>& nodes,
    const AppConfig& config,
    bool tun,
    const std::string& outPath,
    std::string& error
) {
    switch (target) {
        case ExportTarget::Mihomo:
            return exportMihomoImpl(nodes, config, tun, outPath, error);
        case ExportTarget::SingBox:
            return exportSingBoxImpl(nodes, config, tun, outPath, error);
        case ExportTarget::Xray:
            return exportXrayImpl(nodes, config, tun, outPath, error);
    }
    return {};
}

bool exportMihomo(
    const std::vector<ProxyNode>& nodes,
    const AppConfig& config,
    bool tun,
    const std::string& outPath,
    std::string& error
) {
    return exportForTarget(ExportTarget::Mihomo, nodes, config, tun, outPath, error).ok;
}

bool exportSingBox(
    const std::vector<ProxyNode>& nodes,
    const AppConfig& config,
    bool tun,
    const std::string& outPath,
    std::string& error
) {
    return exportForTarget(ExportTarget::SingBox, nodes, config, tun, outPath, error).ok;
}

bool exportXray(
    const std::vector<ProxyNode>& nodes,
    const AppConfig& config,
    bool tun,
    const std::string& outPath,
    std::string& error
) {
    return exportForTarget(ExportTarget::Xray, nodes, config, tun, outPath, error).ok;
}

} // namespace subcli
