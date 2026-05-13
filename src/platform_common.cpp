#include "subcli/platform.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <sstream>

namespace subcli {

std::string platformPathListSeparator() {
#ifdef _WIN32
    return ";";
#else
    return ":";
#endif
}

std::vector<std::string> executableNameCandidates(const std::string& name) {
#ifdef _WIN32
    std::vector<std::string> out;
    if (name.empty()) {
        return out;
    }

    const std::filesystem::path input(name);
    if (input.has_extension()) {
        out.push_back(name);
        return out;
    }

    std::vector<std::string> extensions = {".exe", ".cmd", ".bat", ".com"};
    const char* pathextRaw = std::getenv("PATHEXT");
    if (pathextRaw != nullptr && *pathextRaw != '\0') {
        extensions.clear();
        std::stringstream ss(pathextRaw);
        std::string item;
        while (std::getline(ss, item, ';')) {
            if (item.empty()) {
                continue;
            }
            std::transform(item.begin(), item.end(), item.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
            extensions.push_back(item);
        }
    }

    out.push_back(name);
    for (const auto& ext : extensions) {
        out.push_back(name + ext);
    }
    return out;
#else
    return name.empty() ? std::vector<std::string>{} : std::vector<std::string>{name};
#endif
}

std::string findExecutableOnPath(const std::string& name) {
    if (name.empty()) {
        return "";
    }

    const auto candidates = executableNameCandidates(name);
    const bool hasPathSeparator = name.find('/') != std::string::npos || name.find('\\') != std::string::npos;
    if (hasPathSeparator) {
        for (const auto& candidateName : candidates) {
            if (isExecutablePath(candidateName)) {
                return candidateName;
            }
        }
        return "";
    }

    const char* raw = std::getenv("PATH");
    if (raw == nullptr || *raw == '\0') {
        return "";
    }

    std::stringstream ss(raw);
    std::string dir;
    const char separator = platformPathListSeparator()[0];
    while (std::getline(ss, dir, separator)) {
        if (dir.empty()) {
            dir = ".";
        }
        for (const auto& candidateName : candidates) {
            const auto candidate = std::filesystem::path(dir) / candidateName;
            if (isExecutablePath(candidate.string())) {
                return candidate.string();
            }
        }
    }
    return "";
}

} // namespace subcli
