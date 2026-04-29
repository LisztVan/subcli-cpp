#include "subcli/core_runtime.hpp"

#include <string>
#include <vector>

#ifdef _WIN32

namespace subcli {

bool startCoreRuntime(
    const std::filesystem::path&,
    const std::string&,
    const std::string&,
    const std::vector<std::string>&,
    const std::string&,
    std::string& error
) {
    error = "runtime management is not supported on Windows yet";
    return false;
}

RuntimeStatus inspectCoreRuntime(const std::filesystem::path&, const std::string& target, std::string& error) {
    error.clear();
    RuntimeStatus status;
    status.target = target;
    return status;
}

bool stopCoreRuntime(const std::filesystem::path&, const std::string&, int, std::string& error) {
    error = "runtime management is not supported on Windows yet";
    return false;
}

} // namespace subcli

#else

#include <chrono>
#include <csignal>
#include <fcntl.h>
#include <nlohmann/json.hpp>
#include <thread>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "subcli/util.hpp"

namespace subcli {

namespace {

std::string normalizeTarget(std::string target) {
    for (char& ch : target) {
        if (ch == '-') {
            ch = '_';
        }
    }
    return target;
}

std::filesystem::path runtimeStatePath(const std::filesystem::path& stateDir, const std::string& target) {
    return stateDir / "runtime" / (normalizeTarget(target) + ".json");
}

bool isPidRunning(pid_t pid) {
    if (pid <= 0) {
        return false;
    }
    if (kill(pid, 0) == 0) {
        return true;
    }
    return errno != ESRCH;
}

bool writeRuntimeState(const std::filesystem::path& statePath, const RuntimeStatus& status, std::string& error) {
    nlohmann::json state = {
        {"pid", status.pid},
        {"target", status.target},
        {"binary_path", status.binaryPath},
        {"config_path", status.configPath},
    };
    return writeFile(statePath.string(), state.dump(2), error);
}

} // namespace

RuntimeStatus inspectCoreRuntime(const std::filesystem::path& stateDir, const std::string& target, std::string& error) {
    error.clear();
    RuntimeStatus status;
    status.target = target;

    const auto statePath = runtimeStatePath(stateDir, target);
    if (!fileExists(statePath.string())) {
        return status;
    }

    const auto parsed = nlohmann::json::parse(readFile(statePath.string()), nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) {
        error = "invalid runtime state file: " + statePath.string();
        return status;
    }

    status.hasState = true;
    status.pid = parsed.value("pid", 0);
    status.binaryPath = parsed.value("binary_path", "");
    status.configPath = parsed.value("config_path", "");
    if (status.target.empty()) {
        status.target = parsed.value("target", target);
    }
    status.running = isPidRunning(static_cast<pid_t>(status.pid));
    return status;
}

bool startCoreRuntime(
    const std::filesystem::path& stateDir,
    const std::string& target,
    const std::string& binaryPath,
    const std::vector<std::string>& args,
    const std::string& configPath,
    std::string& error
) {
    error.clear();
    if (target.empty()) {
        error = "runtime target is empty";
        return false;
    }
    if (binaryPath.empty()) {
        error = "runtime binary path is empty";
        return false;
    }

    const auto current = inspectCoreRuntime(stateDir, target, error);
    if (!error.empty()) {
        return false;
    }
    if (current.hasState && current.running) {
        error = "runtime already running for " + target;
        return false;
    }

    const auto statePath = runtimeStatePath(stateDir, target);
    std::error_code ec;
    std::filesystem::create_directories(statePath.parent_path(), ec);
    if (ec) {
        error = "failed to create runtime state directory: " + ec.message();
        return false;
    }

    pid_t pid = fork();
    if (pid < 0) {
        error = "failed to fork runtime process";
        return false;
    }

    if (pid == 0) {
        setsid();
        const int devNull = open("/dev/null", O_RDWR);
        if (devNull >= 0) {
            dup2(devNull, STDIN_FILENO);
            dup2(devNull, STDOUT_FILENO);
            dup2(devNull, STDERR_FILENO);
            close(devNull);
        }

        std::vector<char*> argv;
        argv.reserve(args.size() + 2);
        argv.push_back(const_cast<char*>(binaryPath.c_str()));
        for (const auto& arg : args) {
            argv.push_back(const_cast<char*>(arg.c_str()));
        }
        argv.push_back(nullptr);
        execv(binaryPath.c_str(), argv.data());
        _exit(127);
    }

    RuntimeStatus state;
    state.hasState = true;
    state.running = true;
    state.pid = static_cast<int>(pid);
    state.target = target;
    state.binaryPath = binaryPath;
    state.configPath = configPath;
    if (!writeRuntimeState(statePath, state, error)) {
        kill(pid, SIGTERM);
        return false;
    }
    return true;
}

bool stopCoreRuntime(const std::filesystem::path& stateDir, const std::string& target, int timeoutSec, std::string& error) {
    error.clear();
    auto status = inspectCoreRuntime(stateDir, target, error);
    if (!error.empty()) {
        return false;
    }
    if (!status.hasState) {
        return true;
    }

    const auto statePath = runtimeStatePath(stateDir, target);
    if (!status.running || status.pid <= 0) {
        std::error_code ec;
        std::filesystem::remove(statePath, ec);
        return true;
    }

    const pid_t pid = static_cast<pid_t>(status.pid);
    if (kill(pid, SIGTERM) != 0 && errno != ESRCH) {
        error = "failed to send SIGTERM";
        return false;
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(std::max(1, timeoutSec));
    while (std::chrono::steady_clock::now() < deadline) {
        if (!isPidRunning(pid)) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    if (isPidRunning(pid)) {
        if (kill(pid, SIGKILL) != 0 && errno != ESRCH) {
            error = "failed to send SIGKILL";
            return false;
        }
    }

    int waitStatus = 0;
    (void)waitpid(pid, &waitStatus, WNOHANG);

    std::error_code ec;
    std::filesystem::remove(statePath, ec);
    return true;
}

} // namespace subcli

#endif
