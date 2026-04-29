#include "subcli/core_discovery.hpp"

#include <cstdlib>
#include <cctype>
#include <filesystem>
#include <sstream>

#ifdef _WIN32
#include <algorithm>
#else
#include <unistd.h>
#endif

namespace subcli {

std::string findExecutableInPath(const std::string& name) {
    const char* raw = std::getenv("PATH");
    if (!raw || !*raw) {
        return "";
    }
    std::stringstream ss(raw);
    std::string dir;
    const char pathSeparator =
#ifdef _WIN32
        ';';
#else
        ':';
#endif
    while (std::getline(ss, dir, pathSeparator)) {
        if (dir.empty()) {
            continue;
        }
        std::filesystem::path candidate = std::filesystem::path(dir) / name;
        if (isExecutableFile(candidate)) {
            return candidate.string();
        }
    }
    return "";
}

bool isExecutableFile(const std::filesystem::path& path) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec) {
        return false;
    }
    if (!std::filesystem::is_regular_file(path, ec) || ec) {
        return false;
    }
#ifdef _WIN32
    const std::string ext = path.extension().string();
    std::string lowerExt = ext;
    std::transform(lowerExt.begin(), lowerExt.end(), lowerExt.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return lowerExt == ".exe" || lowerExt == ".bat" || lowerExt == ".cmd";
#else
    return access(path.c_str(), X_OK) == 0;
#endif
}

CorePaths discoverCorePaths(const AppConfig& cfg) {
    CorePaths out;
    out.mihomo = cfg.mihomoPath.empty() ? findExecutableInPath("mihomo") : cfg.mihomoPath;
    out.singBox = cfg.singBoxPath.empty() ? findExecutableInPath("sing-box") : cfg.singBoxPath;
    out.xray = cfg.xrayPath.empty() ? findExecutableInPath("xray") : cfg.xrayPath;
    return out;
}

} // namespace subcli
