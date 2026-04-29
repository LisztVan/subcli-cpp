#include "subcli/core_check.hpp"

#include <string>

#ifdef _WIN32

namespace subcli {

CoreCheckResult runMihomoConfigCheck(const std::string&, const std::string&, int) {
    return {false, -1, "core config check is not supported on Windows yet"};
}

CoreCheckResult runSingBoxConfigCheck(const std::string&, const std::string&, int) {
    return {false, -1, "core config check is not supported on Windows yet"};
}

CoreCheckResult runXrayConfigCheck(const std::string&, const std::string&, int) {
    return {false, -1, "core config check is not supported on Windows yet"};
}

} // namespace subcli

#else

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <chrono>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace subcli {

namespace {

bool isExecutable(const std::string& path) {
    if (path.empty()) {
        return false;
    }
    return access(path.c_str(), X_OK) == 0;
}

CoreCheckResult runProcess(const std::string& binaryPath, const std::vector<std::string>& args, int timeoutSec) {
    CoreCheckResult result;
    if (binaryPath.empty()) {
        result.ok = false;
        result.message = "binary path is empty";
        return result;
    }

    int pipeFd[2] = {-1, -1};
    if (pipe(pipeFd) != 0) {
        result.message = std::string("failed to create pipe: ") + std::strerror(errno);
        return result;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(pipeFd[0]);
        close(pipeFd[1]);
        result.message = std::string("failed to fork: ") + std::strerror(errno);
        return result;
    }

    if (pid == 0) {
        close(pipeFd[0]);
        dup2(pipeFd[1], STDOUT_FILENO);
        dup2(pipeFd[1], STDERR_FILENO);
        close(pipeFd[1]);

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

    close(pipeFd[1]);
    fcntl(pipeFd[0], F_SETFL, fcntl(pipeFd[0], F_GETFL, 0) | O_NONBLOCK);
    std::string output;
    char buf[1024];
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(std::max(1, timeoutSec));
    int status = 0;
    while (true) {
        while (true) {
            const ssize_t n = read(pipeFd[0], buf, sizeof(buf));
            if (n > 0) {
                output.append(buf, static_cast<size_t>(n));
                continue;
            }
            break;
        }

        const pid_t done = waitpid(pid, &status, WNOHANG);
        if (done == pid) {
            break;
        }
        if (done < 0) {
            close(pipeFd[0]);
            result.message = std::string("failed to wait process: ") + std::strerror(errno);
            return result;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            kill(pid, SIGTERM);
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            if (waitpid(pid, &status, WNOHANG) == 0) {
                kill(pid, SIGKILL);
                waitpid(pid, &status, 0);
            }
            close(pipeFd[0]);
            result.exitCode = 124;
            result.ok = false;
            result.message = "command timed out after " + std::to_string(std::max(1, timeoutSec)) + " seconds";
            return result;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    close(pipeFd[0]);

    if (WIFEXITED(status)) {
        result.exitCode = WEXITSTATUS(status);
        result.ok = (result.exitCode == 0);
    } else if (WIFSIGNALED(status)) {
        result.exitCode = 128 + WTERMSIG(status);
        result.ok = false;
    } else {
        result.exitCode = -1;
        result.ok = false;
    }

    if (!result.ok) {
        if (!output.empty()) {
            result.message = output;
        } else {
            result.message = "command failed with exit code " + std::to_string(result.exitCode);
        }
    }
    return result;
}

} // namespace

CoreCheckResult runMihomoConfigCheck(const std::string& binaryPath, const std::string& configPath, int timeoutSec) {
    if (binaryPath.empty()) {
        return {false, -1, "mihomo binary not found; set core_paths.mihomo or install it in PATH"};
    }
    if (!isExecutable(binaryPath)) {
        return {false, -1, "mihomo binary is not executable: " + binaryPath};
    }
    return runProcess(binaryPath, {"-t", "-f", configPath}, timeoutSec);
}

CoreCheckResult runSingBoxConfigCheck(const std::string& binaryPath, const std::string& configPath, int timeoutSec) {
    if (binaryPath.empty()) {
        return {false, -1, "sing-box binary not found; set core_paths.sing_box or install it in PATH"};
    }
    if (!isExecutable(binaryPath)) {
        return {false, -1, "sing-box binary is not executable: " + binaryPath};
    }
    return runProcess(binaryPath, {"check", "-c", configPath}, timeoutSec);
}

CoreCheckResult runXrayConfigCheck(const std::string& binaryPath, const std::string& configPath, int timeoutSec) {
    if (binaryPath.empty()) {
        return {false, -1, "xray binary not found; set core_paths.xray or install it in PATH"};
    }
    if (!isExecutable(binaryPath)) {
        return {false, -1, "xray binary is not executable: " + binaryPath};
    }
    return runProcess(binaryPath, {"run", "-test", "-config", configPath}, timeoutSec);
}

} // namespace subcli

#endif
