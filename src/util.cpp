#include "subcli/util.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace subcli {

namespace {

std::time_t timegmPortable(std::tm* tm) {
#if defined(_WIN32)
    return _mkgmtime(tm);
#else
    return timegm(tm);
#endif
}

std::filesystem::path normalizeAbsolutePathImpl(const std::filesystem::path& path) {
    std::error_code ec;
    auto abs = std::filesystem::absolute(path, ec);
    if (ec) {
        return path;
    }
    return abs.lexically_normal();
}

} // namespace

std::string nowIso8601() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm = *std::gmtime(&t);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

std::string toLower(std::string v) {
    std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return v;
}

bool fileExists(const std::string& path) {
    return std::filesystem::exists(path);
}

std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool writeFile(const std::string& path, const std::string& content, std::string& error) {
    try {
        const std::filesystem::path target(path);
        const auto parent = target.parent_path();
        if (!parent.empty()) {
            std::filesystem::create_directories(parent);
        }
    } catch (const std::exception& ex) {
        error = std::string("failed to prepare directory for file: ") + path + " (" + ex.what() + ")";
        return false;
    }

    std::ofstream out(path, std::ios::binary);
    if (!out) {
        error = "failed to open file: " + path;
        return false;
    }
    out << content;
    if (!out.good()) {
        error = "failed to write file: " + path;
        return false;
    }
    return true;
}

std::string makeIdFromName(const std::string& name) {
    std::string id;
    id.reserve(name.size());
    for (char c : name) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            id.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        } else if (c == ' ' || c == '-' || c == '_') {
            id.push_back('-');
        }
    }
    if (id.empty()) {
        id = "sub";
    }
    return id;
}

bool parseIso8601(const std::string& value, std::time_t& out) {
    std::tm tm = {};
    std::istringstream iss(value);
    iss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    if (iss.fail()) {
        return false;
    }
    out = timegmPortable(&tm);
    return out != static_cast<std::time_t>(-1);
}

std::filesystem::path normalizeAbsolutePathForTest(const std::filesystem::path& path) {
    return normalizeAbsolutePathImpl(path);
}

std::string resolveAgainstBaseForTest(const std::string& baseDir, const std::string& path) {
    std::filesystem::path p(path);
    if (p.is_absolute()) {
        return normalizeAbsolutePathImpl(p).string();
    }
    return normalizeAbsolutePathImpl(std::filesystem::path(baseDir) / p).string();
}

} // namespace subcli
