#pragma once

#include <map>
#include <string>
#include <variant>
#include <vector>

#include "subcli/diagnostic.hpp"
#include "subcli/models.hpp"

namespace subcli {

struct Endpoint {
    std::string host;
    int port = 0;
};

struct CommonOptions {
    bool udp = false;
    bool tfo = false;
    bool mptcp = false;
    std::string interfaceName;
    std::string routingMark;
    std::string dialerProxy;
};

struct ShadowsocksOptions {
    std::string method;
    std::string password;
    bool udpOverTcp = false;
    int udpOverTcpVersion = 0;
    std::string plugin;
    std::map<std::string, std::string> pluginOptions;
};

struct VMessOptions {
    std::string uuid;
    std::string security = "auto";
    int alterId = 0;
    std::string packetEncoding;
    bool globalPadding = false;
    bool authenticatedLength = false;
};

struct VLESSOptions {
    std::string uuid;
    std::string flow;
    std::string encryption = "none";
    std::string packetEncoding;
};

struct TrojanOptions {
    std::string password;
    std::map<std::string, std::string> shadowsocksOptions;
};

struct Hysteria2Options {
    std::string password;
    std::string ports;
    std::string hopInterval;
    std::string hopIntervalMax;
    int upMbps = 0;
    int downMbps = 0;
    std::string obfsType;
    std::string obfsPassword;
};

struct HysteriaOptions {
    int version = 2;
};

struct TUICOptions {
    std::string token;
    std::string uuid;
    std::string password;
    std::string congestionControl;
    std::string udpRelayMode;
    bool zeroRttHandshake = false;
    std::string heartbeat;
};

struct WireGuardPeer {
    Endpoint endpoint;
    std::string publicKey;
    std::string preSharedKey;
    std::vector<std::string> allowedIps;
    std::vector<int> reserved;
};

struct WireGuardOptions {
    std::string privateKey;
    std::vector<std::string> localAddress;
    std::vector<WireGuardPeer> peers;
    int mtu = 0;
    int workers = 0;
    std::string domainStrategy;
};

struct GenericOptions {
    std::map<std::string, std::string> values;
};

using NodeProtocolOptions = std::variant<
    GenericOptions,
    ShadowsocksOptions,
    VMessOptions,
    VLESSOptions,
    TrojanOptions,
    HysteriaOptions,
    Hysteria2Options,
    TUICOptions,
    WireGuardOptions>;

struct Node {
    std::string protocol;
    std::string sourceId;
    std::string name;
    Endpoint server;
    CommonOptions common;
    TlsConfig tls;
    TransportConfig transport;
    NodeProtocolOptions options;
    std::vector<Diagnostic> diagnostics;
};

Node legacyToStructuredNode(const ProxyNode& legacy);

} // namespace subcli
