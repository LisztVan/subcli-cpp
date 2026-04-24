#include "subcli/capabilities.hpp"

#include <set>

#include "subcli/protocol_registry.hpp"
#include "subcli/util.hpp"

namespace subcli {
namespace {

bool isCoreGeneratedProtocol(const std::string& type) {
    const auto canonical = canonicalProtocolName(type);
    return canonical == "direct" || canonical == "dns" || canonical == "block" || canonical == "selector" ||
           canonical == "urltest" || canonical == "freedom" || canonical == "blackhole" || canonical == "loopback";
}

bool isTransportlessProtocol(const std::string& type) {
    const auto canonical = canonicalProtocolName(type);
    return canonical == "hysteria" || canonical == "hysteria2" || canonical == "tuic" || canonical == "wireguard" ||
           canonical == "direct" || canonical == "dns" || canonical == "block" || canonical == "selector" ||
           canonical == "urltest" || canonical == "freedom" || canonical == "blackhole" || canonical == "loopback";
}

bool hasAny(const std::set<std::string>& values, const std::string& value) {
    return values.count(value) > 0;
}

std::set<std::string> supportedTransports(ExportTarget target, const std::string& type) {
    const auto protocol = canonicalProtocolName(type);
    if (isTransportlessProtocol(protocol)) {
        return {"", "tcp"};
    }
    switch (target) {
        case ExportTarget::Mihomo:
            if (protocol == "vless") {
                return {"", "tcp", "ws", "grpc", "http", "h2", "xhttp"};
            }
            if (protocol == "trojan") {
                return {"", "tcp", "ws", "grpc"};
            }
            if (protocol == "vmess") {
                return {"", "tcp", "ws", "grpc", "http", "h2"};
            }
            return {"", "tcp"};
        case ExportTarget::SingBox:
            if (protocol == "vmess" || protocol == "vless" || protocol == "trojan") {
                return {"", "tcp", "ws", "grpc", "http", "httpupgrade", "quic"};
            }
            return {"", "tcp"};
        case ExportTarget::Xray:
            if (protocol == "vmess" || protocol == "vless" || protocol == "trojan") {
                return {"", "tcp", "raw", "ws", "grpc", "http", "h2", "httpupgrade", "xhttp", "mkcp", "quic"};
            }
            if (protocol == "shadowsocks") {
                return {"", "tcp"};
            }
            return {"", "tcp"};
    }
    return {"", "tcp"};
}

} // namespace

bool supportsProtocol(ExportTarget target, const ProxyNode& node) {
    return isProtocolExportReady(target, node.type);
}

bool supportsTransport(ExportTarget target, const ProxyNode& node) {
    const auto network = toLower(node.network.empty() ? node.transport.network : node.network);
    return hasAny(supportedTransports(target, node.type), network);
}

bool supportsTlsMode(ExportTarget target, const ProxyNode& node) {
    const auto type = canonicalProtocolName(node.type);
    if (isCoreGeneratedProtocol(type)) {
        return true;
    }
    if (!node.reality && !node.tlsConfig.reality.enabled) {
        return true;
    }
    if (target == ExportTarget::SingBox && (type == "hysteria2" || type == "tuic" || type == "wireguard")) {
        return false;
    }
    return !node.publicKey.empty() || !node.tlsConfig.reality.publicKey.empty();
}

bool supportsNode(ExportTarget target, const ProxyNode& node, std::string& reason) {
    if (!isOfficialProtocolSupported(target, node.type)) {
        reason = "protocol '" + canonicalProtocolName(node.type) + "' is not supported by " + targetName(target);
        return false;
    }
    if (!supportsProtocol(target, node)) {
        reason = "protocol '" + canonicalProtocolName(node.type) + "' is registered for " + targetName(target) +
                 " but its exporter is not implemented yet";
        return false;
    }
    if (!supportsTransport(target, node)) {
        const auto network = toLower(node.network.empty() ? node.transport.network : node.network);
        reason = "transport '" + network + "' is not supported for protocol '" + canonicalProtocolName(node.type) +
                 "' by " + targetName(target);
        return false;
    }
    if (!supportsTlsMode(target, node)) {
        reason = "tls/reality mode is not supported for protocol '" + canonicalProtocolName(node.type) + "' by " +
                 targetName(target);
        return false;
    }
    reason.clear();
    return true;
}

} // namespace subcli
