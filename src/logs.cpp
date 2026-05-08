#include "subcli/logs.hpp"

#include <chrono>
#include <deque>
#include <fstream>
#include <iostream>
#include <thread>

namespace subcli {

std::vector<std::string> readLogTailLines(const std::filesystem::path& path, int tailLines, std::string& error) {
    error.clear();
    if (tailLines < 0) {
        error = "tail lines must not be negative";
        return {};
    }

    std::ifstream in(path);
    if (!in) {
        error = "log file does not exist or cannot be read: " + path.string();
        return {};
    }

    if (tailLines == 0) {
        std::vector<std::string> lines;
        std::string line;
        while (std::getline(in, line)) {
            lines.push_back(line);
        }
        return lines;
    }

    std::deque<std::string> tail;
    std::string line;
    while (std::getline(in, line)) {
        tail.push_back(line);
        if (static_cast<int>(tail.size()) > tailLines) {
            tail.pop_front();
        }
    }

    return {tail.begin(), tail.end()};
}

bool printLogTail(const std::filesystem::path& path, int tailLines, std::string& error) {
    const auto lines = readLogTailLines(path, tailLines, error);
    if (!error.empty()) {
        return false;
    }

    for (const auto& line : lines) {
        std::cout << line << '\n';
    }
    return true;
}

bool followLog(const std::filesystem::path& path, std::string& error) {
    error.clear();
    std::ifstream in(path);
    if (!in) {
        error = "log file does not exist or cannot be read: " + path.string();
        return false;
    }

    in.seekg(0, std::ios::end);
    while (true) {
        std::string line;
        while (std::getline(in, line)) {
            std::cout << line << '\n';
        }
        std::cout.flush();
        in.clear();
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
}

} // namespace subcli
