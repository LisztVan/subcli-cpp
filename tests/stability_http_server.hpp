#pragma once

#include <atomic>
#include <filesystem>
#include <string>
#include <thread>

#include "platform_test_support.hpp"

namespace subcli {

class StabilityHttpServer {
public:
    explicit StabilityHttpServer(std::filesystem::path fixtureDir);
    ~StabilityHttpServer();

    StabilityHttpServer(const StabilityHttpServer&) = delete;
    StabilityHttpServer& operator=(const StabilityHttpServer&) = delete;

    void start();
    void stop();
    int port() const;
    std::string url(const std::string& path) const;

private:
    void serveLoop();
    void handleClient(int client);
    std::string responseForPath(const std::string& path);
    std::string readFixture(const std::string& name) const;

    std::filesystem::path fixtureDir_;
    TestTcpServerHandle server_{};
    std::thread thread_;
    std::atomic<bool> running_{false};
};

} // namespace subcli
