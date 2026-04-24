#include "subcli/protocol_registry.hpp"

#include <algorithm>

#include "subcli/util.hpp"

namespace subcli {
namespace {

ProtocolTargetInfo unsupported() {
    return {};
}

ProtocolTargetInfo officialOnly(const std::string& outputName) {
    return {true, false, outputName};
}

ProtocolTargetInfo ready(const std::string& outputName) {
    return {true, true, outputName};
}

const std::vector<ProtocolInfo>& registry() {
    static const std::vector<ProtocolInfo> items = {
        {"direct", {"direct"}, officialOnly("direct"), officialOnly("direct"), unsupported()},
        {"dns", {"dns"}, officialOnly("dns"), officialOnly("dns"), ready("dns")},
        {"http", {"http"}, ready("http"), ready("http"), ready("http")},
        {"socks", {"socks", "socks5"}, ready("socks"), ready("socks"), ready("socks")},
        {"shadowsocks", {"ss", "shadowsocks"}, ready("ss"), ready("shadowsocks"), ready("shadowsocks")},
        {"shadowsocksr", {"ssr", "shadowsocksr"}, ready("ssr"), unsupported(), unsupported()},
        {"snell", {"snell"}, ready("snell"), unsupported(), unsupported()},
        {"vmess", {"vmess"}, ready("vmess"), ready("vmess"), ready("vmess")},
        {"vless", {"vless"}, ready("vless"), ready("vless"), ready("vless")},
        {"trojan", {"trojan"}, ready("trojan"), ready("trojan"), ready("trojan")},
        {"anytls", {"anytls", "any-tls"}, ready("anytls"), ready("anytls"), unsupported()},
        {"mieru", {"mieru"}, ready("mieru"), unsupported(), unsupported()},
        {"sudoku", {"sudoku"}, ready("sudoku"), unsupported(), unsupported()},
        {"hysteria", {"hysteria", "hy"}, ready("hysteria"), ready("hysteria"), ready("hysteria")},
        {"hysteria2", {"hysteria2", "hy2"}, ready("hysteria2"), ready("hysteria2"), unsupported()},
        {"tuic", {"tuic"}, ready("tuic"), ready("tuic"), unsupported()},
        {"wireguard", {"wireguard", "wg"}, ready("wireguard"), ready("wireguard"), ready("wireguard")},
        {"ssh", {"ssh"}, ready("ssh"), ready("ssh"), unsupported()},
        {"masque", {"masque"}, ready("masque"), unsupported(), unsupported()},
        {"trusttunnel", {"trusttunnel", "trust-tunnel"}, ready("trusttunnel"), unsupported(), unsupported()},
        {"block", {"block"}, unsupported(), officialOnly("block"), unsupported()},
        {"selector", {"selector", "select"}, unsupported(), officialOnly("selector"), unsupported()},
        {"urltest", {"urltest", "url-test"}, unsupported(), officialOnly("urltest"), unsupported()},
        {"naive", {"naive", "naiveproxy"}, unsupported(), ready("naive"), unsupported()},
        {"shadowtls", {"shadowtls", "shadow-tls"}, unsupported(), ready("shadowtls"), unsupported()},
        {"tor", {"tor"}, unsupported(), ready("tor"), unsupported()},
        {"blackhole", {"blackhole"}, unsupported(), unsupported(), ready("blackhole")},
        {"freedom", {"freedom"}, unsupported(), unsupported(), ready("freedom")},
        {"loopback", {"loopback"}, unsupported(), unsupported(), ready("loopback")},
    };
    return items;
}

const ProtocolTargetInfo& infoForTarget(const ProtocolInfo& info, ExportTarget target) {
    switch (target) {
        case ExportTarget::Mihomo:
            return info.mihomo;
        case ExportTarget::SingBox:
            return info.singBox;
        case ExportTarget::Xray:
            return info.xray;
    }
    return info.mihomo;
}

} // namespace

std::string targetName(ExportTarget target) {
    switch (target) {
        case ExportTarget::Mihomo:
            return "mihomo";
        case ExportTarget::SingBox:
            return "sing-box";
        case ExportTarget::Xray:
            return "xray";
    }
    return "unknown";
}

std::string canonicalProtocolName(const std::string& type) {
    const std::string low = toLower(type);
    for (const auto& item : registry()) {
        if (item.aliases.count(low)) {
            return item.canonical;
        }
    }
    return low;
}

const ProtocolInfo* protocolInfo(const std::string& type) {
    const std::string canonical = canonicalProtocolName(type);
    for (const auto& item : registry()) {
        if (item.canonical == canonical) {
            return &item;
        }
    }
    return nullptr;
}

ProtocolTargetInfo protocolTargetInfo(ExportTarget target, const std::string& type) {
    const auto* info = protocolInfo(type);
    if (!info) {
        return {};
    }
    return infoForTarget(*info, target);
}

bool isOfficialProtocolSupported(ExportTarget target, const std::string& type) {
    return protocolTargetInfo(target, type).official;
}

bool isProtocolExportReady(ExportTarget target, const std::string& type) {
    return protocolTargetInfo(target, type).exportReady;
}

std::string targetProtocolName(ExportTarget target, const std::string& type) {
    const auto info = protocolTargetInfo(target, type);
    return info.outputName.empty() ? canonicalProtocolName(type) : info.outputName;
}

std::vector<std::string> officialProtocolsForTarget(ExportTarget target) {
    std::vector<std::string> out;
    for (const auto& item : registry()) {
        if (infoForTarget(item, target).official) {
            out.push_back(item.canonical);
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace subcli
