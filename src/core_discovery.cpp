#include "subcli/core_discovery.hpp"
#include "subcli/platform.hpp"

namespace subcli {

std::string findExecutableInPath(const std::string& name) {
    return findExecutableOnPath(name);
}

bool isExecutableFile(const std::filesystem::path& path) {
    return isExecutablePath(path.string());
}

CorePaths discoverCorePaths(const AppConfig& cfg) {
    CorePaths out;
    out.mihomo = cfg.mihomoPath.empty() ? findExecutableInPath("mihomo") : cfg.mihomoPath;
    out.singBox = cfg.singBoxPath.empty() ? findExecutableInPath("sing-box") : cfg.singBoxPath;
    out.xray = cfg.xrayPath.empty() ? findExecutableInPath("xray") : cfg.xrayPath;
    return out;
}

} // namespace subcli
