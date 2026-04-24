#pragma once

#include <string>
#include <vector>

#include "subcli/models.hpp"

namespace subcli {

std::vector<Subscription> loadSubscriptions(const std::string& path);
void saveSubscriptions(const std::string& path, const std::vector<Subscription>& subs);

AppConfig loadConfig(const std::string& path);
void saveConfig(const std::string& path, const AppConfig& config);

} // namespace subcli
