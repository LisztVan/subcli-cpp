#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace subcli {

std::vector<std::string> readLogTailLines(const std::filesystem::path& path, int tailLines, std::string& error);
bool printLogTail(const std::filesystem::path& path, int tailLines, std::string& error);
bool followLog(const std::filesystem::path& path, std::string& error);

} // namespace subcli
