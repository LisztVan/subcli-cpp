#pragma once

#include <string>
#include <vector>

#include "subcli/models.hpp"

namespace subcli {

ParseResult parseSubscription(
    const std::string& content,
    const std::string& sourceId,
    const AppConfig& config
);

ParseResult parseSubscription(
    const std::string& content,
    const std::string& sourceId,
    const std::string& formatHint,
    const AppConfig& config
);

std::vector<ProxyNode> parseSubscriptionContent(
    const std::string& content,
    const std::string& sourceId,
    const AppConfig& config
);

} // namespace subcli
