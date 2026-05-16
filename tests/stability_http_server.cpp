#include "stability_http_server.hpp"

#include <chrono>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

namespace subcli {
namespace {

std::string httpResponse(int status, const std::string& reason, const std::string& body, const std::string& contentType = "text/plain; charset=utf-8") {
    std::ostringstream out;
    out << "HTTP/1.1 " << status << " " << reason << "\r\n";
    out << "Content-Type: " << contentType << "\r\n";
    out << "Content-Length: " << body.size() << "\r\n";
    out << "Connection: close\r\n";
    out << "\r\n";
    out << body;
    return out.str();
}

std::string parsePathFromRequest(const std::string& request) {
    const auto firstSpace = request.find(' ');
    if (firstSpace == std::string::npos) {
        return "/";
    }
    const auto secondSpace = request.find(' ', firstSpace + 1);
    if (secondSpace == std::string::npos) {
        return "/";
    }
    return request.substr(firstSpace + 1, secondSpace - firstSpace - 1);
}

std::string largeSubscriptionBody() {
    std::string body;
    body.reserve(1024 * 1024 + 4096);
    const std::string line = "ss://YWVzLTI1Ni1nY206cGFzc0AxMjcuMC4wLjE6ODM4OA#Large%20Node\n";
    while (body.size() < 1024 * 1024 + 4096) {
        body += line;
    }
    return body;
}

} // namespace

StabilityHttpServer::StabilityHttpServer(std::filesystem::path fixtureDir) : fixtureDir_(std::move(fixtureDir)) {}

StabilityHttpServer::~StabilityHttpServer() {
    stop();
}

void StabilityHttpServer::start() {
    if (running_) {
        return;
    }
    server_ = testSupportCreateLoopbackServer();
    running_ = true;
    thread_ = std::thread([this]() { serveLoop(); });
}

void StabilityHttpServer::stop() {
    if (!running_) {
        return;
    }
    const int shutdownPort = server_.port;
    if (shutdownPort > 0) {
        const int client = testSupportConnectLoopback(shutdownPort);
        if (client >= 0) {
            const std::string request = "GET /shutdown HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n";
            (void)testSupportSend(client, request.data(), static_cast<int>(request.size()));
            char buffer[256] = {};
            (void)testSupportRecv(client, buffer, static_cast<int>(sizeof(buffer)));
            testSupportCloseClient(client);
        }
    }
    running_ = false;
    if (thread_.joinable()) {
        thread_.join();
    }
    testSupportCloseServer(server_);
}

int StabilityHttpServer::port() const {
    return server_.port;
}

std::string StabilityHttpServer::url(const std::string& path) const {
    return "http://127.0.0.1:" + std::to_string(port()) + path;
}

void StabilityHttpServer::serveLoop() {
    while (running_) {
        int client = testSupportAccept(server_);
        if (client < 0) {
            if (running_) {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            continue;
        }
        handleClient(client);
        testSupportCloseClient(client);
    }
}

void StabilityHttpServer::handleClient(int client) {
    char buffer[4096] = {};
    const int received = testSupportRecv(client, buffer, static_cast<int>(sizeof(buffer) - 1));
    if (received <= 0) {
        return;
    }
    const std::string request(buffer, static_cast<size_t>(received));
    const std::string path = parsePathFromRequest(request);
    const std::string response = responseForPath(path);
    (void)testSupportSend(client, response.data(), static_cast<int>(response.size()));
}

std::string StabilityHttpServer::responseForPath(const std::string& path) {
    if (path == "/shutdown") {
        running_ = false;
        return httpResponse(200, "OK", "shutdown\n");
    }
    if (path == "/sub/plain") {
        return httpResponse(200, "OK", readFixture("plain.txt"));
    }
    if (path == "/sub/base64") {
        return httpResponse(200, "OK", readFixture("base64.txt"));
    }
    if (path == "/sub/malformed") {
        return httpResponse(200, "OK", readFixture("malformed.txt"));
    }
    if (path == "/sub/empty") {
        return httpResponse(200, "OK", readFixture("empty.txt"));
    }
    if (path == "/sub/unicode") {
        return httpResponse(200, "OK", readFixture("unicode.txt"));
    }
    if (path == "/sub/large") {
        return httpResponse(200, "OK", largeSubscriptionBody());
    }
    if (path == "/sub/slow") {
        std::this_thread::sleep_for(std::chrono::seconds(3));
        return httpResponse(200, "OK", readFixture("plain.txt"));
    }
    if (path == "/sub/500") {
        return httpResponse(500, "Internal Server Error", "server error\n");
    }
    return httpResponse(404, "Not Found", "not found\n");
}

std::string StabilityHttpServer::readFixture(const std::string& name) const {
    std::ifstream in(fixtureDir_ / name, std::ios::binary);
    if (!in) {
        throw std::runtime_error("missing stability HTTP fixture: " + (fixtureDir_ / name).string());
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

} // namespace subcli
