#include "platform_test_support.hpp"

#include <filesystem>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace subcli {
namespace {

#ifdef _WIN32
std::string system32Binary(const std::string& name) {
    char buffer[MAX_PATH] = {};
    const UINT len = GetSystemDirectoryA(buffer, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        return name;
    }
    return (std::filesystem::path(buffer) / name).string();
}

std::mutex& socketRegistryMutex() {
    static std::mutex mutex;
    return mutex;
}

std::map<int, SOCKET>& socketRegistry() {
    static std::map<int, SOCKET> sockets;
    return sockets;
}

int registerClientSocket(SOCKET socket) {
    static int nextId = 1;
    std::lock_guard<std::mutex> lock(socketRegistryMutex());
    const int id = nextId++;
    socketRegistry()[id] = socket;
    return id;
}

SOCKET lookupClientSocket(int id) {
    std::lock_guard<std::mutex> lock(socketRegistryMutex());
    const auto it = socketRegistry().find(id);
    return it == socketRegistry().end() ? INVALID_SOCKET : it->second;
}

SOCKET takeClientSocket(int id) {
    std::lock_guard<std::mutex> lock(socketRegistryMutex());
    const auto it = socketRegistry().find(id);
    if (it == socketRegistry().end()) {
        return INVALID_SOCKET;
    }
    const SOCKET socket = it->second;
    socketRegistry().erase(it);
    return socket;
}
#endif

} // namespace

ProcessCommand testSupportSuccessCommand() {
#ifdef _WIN32
    return {system32Binary("cmd.exe"), {"/C", "exit", "0"}};
#else
    return {"/bin/sh", {"-c", "exit 0"}};
#endif
}

ProcessCommand testSupportFailureCommand() {
#ifdef _WIN32
    return {system32Binary("cmd.exe"), {"/C", "exit", "7"}};
#else
    return {"/bin/sh", {"-c", "exit 7"}};
#endif
}

ProcessCommand testSupportLongRunningCommand(int seconds) {
#ifdef _WIN32
    return {system32Binary("WindowsPowerShell\\v1.0\\powershell.exe"), {"-NoProfile", "-Command", "Start-Sleep -Seconds " + std::to_string(seconds)}};
#else
    return {"/bin/sleep", {std::to_string(seconds)}};
#endif
}

ProcessCommand testSupportShellCommand(const std::string& script) {
#ifdef _WIN32
    return {system32Binary("WindowsPowerShell\\v1.0\\powershell.exe"), {"-NoProfile", "-Command", script}};
#else
    return {"/bin/sh", {"-c", script}};
#endif
}

std::string testSupportMissingExecutablePath() {
#ifdef _WIN32
    return "C:\\definitely\\missing\\subcli-binary.exe";
#else
    return "/definitely/missing/subcli-binary";
#endif
}

void testSupportEnsureSocketsReady() {
#ifdef _WIN32
    static bool initialized = false;
    if (!initialized) {
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
            throw std::runtime_error("WSAStartup failed");
        }
        initialized = true;
    }
#endif
}

TestTcpServerHandle testSupportCreateLoopbackServer() {
    testSupportEnsureSocketsReady();
#ifdef _WIN32
    SOCKET server = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server == INVALID_SOCKET) {
        throw std::runtime_error("test server socket should create");
    }
#else
    int server = ::socket(AF_INET, SOCK_STREAM, 0);
    if (server < 0) {
        throw std::runtime_error("test server socket should create");
    }
#endif

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (::bind(server, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
#ifdef _WIN32
        closesocket(server);
#else
        close(server);
#endif
        throw std::runtime_error("test server bind should succeed");
    }
    if (::listen(server, 1) != 0) {
#ifdef _WIN32
        closesocket(server);
#else
        close(server);
#endif
        throw std::runtime_error("test server listen should succeed");
    }

    sockaddr_in bound{};
#ifdef _WIN32
    int boundLen = sizeof(bound);
#else
    socklen_t boundLen = sizeof(bound);
#endif
    if (::getsockname(server, reinterpret_cast<sockaddr*>(&bound), &boundLen) != 0) {
#ifdef _WIN32
        closesocket(server);
#else
        close(server);
#endif
        throw std::runtime_error("test server should report bound port");
    }

    TestTcpServerHandle out;
#ifdef _WIN32
    out.socket = static_cast<uintptr_t>(server);
#else
    out.socket = server;
#endif
    out.port = ntohs(bound.sin_port);
    return out;
}

int testSupportConnectLoopback(int port) {
    testSupportEnsureSocketsReady();
#ifdef _WIN32
    SOCKET client = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (client == INVALID_SOCKET) {
        return -1;
    }
#else
    int client = ::socket(AF_INET, SOCK_STREAM, 0);
    if (client < 0) {
        return -1;
    }
#endif

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(static_cast<unsigned short>(port));
    if (::connect(client, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
#ifdef _WIN32
        closesocket(client);
#else
        close(client);
#endif
        return -1;
    }

#ifdef _WIN32
    return registerClientSocket(client);
#else
    return client;
#endif
}

int testSupportAccept(TestTcpServerHandle& server) {
#ifdef _WIN32
    const SOCKET client = ::accept(static_cast<SOCKET>(server.socket), nullptr, nullptr);
    if (client == INVALID_SOCKET) {
        return -1;
    }
    return registerClientSocket(client);
#else
    return ::accept(server.socket, nullptr, nullptr);
#endif
}

int testSupportRecv(int client, char* buffer, int length) {
#ifdef _WIN32
    const SOCKET socket = lookupClientSocket(client);
    if (socket == INVALID_SOCKET) {
        return -1;
    }
    return ::recv(socket, buffer, length, 0);
#else
    return static_cast<int>(::recv(client, buffer, static_cast<size_t>(length), 0));
#endif
}

int testSupportSend(int client, const char* buffer, int length) {
#ifdef _WIN32
    const SOCKET socket = lookupClientSocket(client);
    if (socket == INVALID_SOCKET) {
        return -1;
    }
    return ::send(socket, buffer, length, 0);
#else
    return static_cast<int>(::send(client, buffer, static_cast<size_t>(length), 0));
#endif
}

void testSupportCloseClient(int client) {
#ifdef _WIN32
    const SOCKET socket = takeClientSocket(client);
    if (socket != INVALID_SOCKET) {
        closesocket(socket);
    }
#else
    close(client);
#endif
}

void testSupportCloseServer(TestTcpServerHandle& server) {
#ifdef _WIN32
    if (server.socket != 0) {
        closesocket(static_cast<SOCKET>(server.socket));
        server.socket = 0;
    }
#else
    if (server.socket >= 0) {
        close(server.socket);
        server.socket = -1;
    }
#endif
}

} // namespace subcli
