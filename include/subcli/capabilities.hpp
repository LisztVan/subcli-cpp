#pragma once

#include <string>

#include "subcli/exporter.hpp"
#include "subcli/models.hpp"

namespace subcli {

bool supportsProtocol(ExportTarget target, const ProxyNode& node);
bool supportsTransport(ExportTarget target, const ProxyNode& node);
bool supportsTlsMode(ExportTarget target, const ProxyNode& node);
bool supportsNode(ExportTarget target, const ProxyNode& node, std::string& reason);

} // namespace subcli
