#pragma once

#include <ctime>
#include <string>

namespace subcli {

std::string nowIso8601();
std::string toLower(std::string v);
bool fileExists(const std::string& path);
std::string readFile(const std::string& path);
bool writeFile(const std::string& path, const std::string& content, std::string& error);
std::string makeIdFromName(const std::string& name);
bool parseIso8601(const std::string& value, std::time_t& out);

} // namespace subcli
