#include "subcli/platform.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

namespace subcli {
namespace {

using Clock = std::chrono::steady_clock;

std::vector<char*> makeArgv(const std::string& binaryPath, const std::vector<std::string>& args) {
    std::vector<char*> argv;
    argv.reserve(args.size() + 2);
    argv.push_back(const_cast<char*>(binaryPath.c_str()));
    for (const auto& arg : args) {
        argv.push_back(const_cast<char*>(arg.c_str()));
    }
    argv.push_back(nullptr);
    return argv;
}

int decodeWaitStatus(int status) {
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }
    return -1;
}

void closeFd(int& fd) {
    if (fd >= 0) {
        close(fd);
        fd = -1;
    }
}

void closePipe(int fds[2]) {
    closeFd(fds[0]);
    closeFd(fds[1]);
}

bool setCloseOnExec(int fd, std::string& error) {
    const int flags = fcntl(fd, F_GETFD, 0);
    if (flags < 0) {
        error = std::string("failed to read fd flags: ") + std::strerror(errno);
        return false;
    }
    if (fcntl(fd, F_SETFD, flags | FD_CLOEXEC) < 0) {
        error = std::string("failed to set close-on-exec: ") + std::strerror(errno);
        return false;
    }
    return true;
}

bool setNonBlocking(int fd, std::string& error) {
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        error = std::string("failed to read fd status flags: ") + std::strerror(errno);
        return false;
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        error = std::string("failed to set non-blocking pipe: ") + std::strerror(errno);
        return false;
    }
    return true;
}

bool makePipe(int fds[2], std::string& error) {
    fds[0] = -1;
    fds[1] = -1;
    if (pipe(fds) != 0) {
        error = std::string("failed to create pipe: ") + std::strerror(errno);
        return false;
    }
    return true;
}

void writeExecErrorAndExit(int execFd, int errorCode) {
    if (execFd >= 0) {
        const int savedErrno = errorCode;
        const char* data = reinterpret_cast<const char*>(&savedErrno);
        size_t written = 0;
        while (written < sizeof(savedErrno)) {
            const ssize_t n = write(execFd, data + written, sizeof(savedErrno) - written);
            if (n > 0) {
                written += static_cast<size_t>(n);
                continue;
            }
            if (n < 0 && errno == EINTR) {
                continue;
            }
            break;
        }
    }
    _exit(127);
}

int duplicateFdAtLeastOrExit(int fd, int minFd, int execFd) {
    const int duplicated = fcntl(fd, F_DUPFD, minFd);
    if (duplicated < 0) {
        writeExecErrorAndExit(execFd, errno);
    }
    return duplicated;
}

void setCloseOnExecOrExit(int fd, int execFd) {
    const int flags = fcntl(fd, F_GETFD, 0);
    if (flags < 0) {
        writeExecErrorAndExit(execFd, errno);
    }
    if (fcntl(fd, F_SETFD, flags | FD_CLOEXEC) < 0) {
        writeExecErrorAndExit(execFd, errno);
    }
}

void execChild(
    const std::string& binaryPath,
    const std::vector<std::string>& args,
    int stdoutFd,
    int stderrFd,
    int execFd
) {
    if (stdoutFd >= 0 && dup2(stdoutFd, STDOUT_FILENO) < 0) {
        writeExecErrorAndExit(execFd, errno);
    }
    if (stderrFd >= 0 && dup2(stderrFd, STDERR_FILENO) < 0) {
        writeExecErrorAndExit(execFd, errno);
    }
    if (stdoutFd > STDERR_FILENO) {
        close(stdoutFd);
    }
    if (stderrFd > STDERR_FILENO && stderrFd != stdoutFd) {
        close(stderrFd);
    }

    auto argv = makeArgv(binaryPath, args);
    execv(binaryPath.c_str(), argv.data());
    writeExecErrorAndExit(execFd, errno);
}

void readTextPipe(int& fd, std::string& output) {
    if (fd < 0) {
        return;
    }

    char buffer[4096];
    while (true) {
        const ssize_t n = read(fd, buffer, sizeof(buffer));
        if (n > 0) {
            output.append(buffer, static_cast<size_t>(n));
            continue;
        }
        if (n == 0) {
            closeFd(fd);
            return;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return;
        }
        closeFd(fd);
        return;
    }
}

void readExecPipe(
    int& fd,
    std::array<char, sizeof(int)>& buffer,
    size_t& bytesRead,
    bool& execFailed,
    int& execErrno
) {
    if (fd < 0 || execFailed) {
        return;
    }

    while (bytesRead < buffer.size()) {
        const ssize_t n = read(fd, buffer.data() + bytesRead, buffer.size() - bytesRead);
        if (n > 0) {
            bytesRead += static_cast<size_t>(n);
            if (bytesRead == buffer.size()) {
                std::memcpy(&execErrno, buffer.data(), sizeof(execErrno));
                execFailed = true;
                closeFd(fd);
            }
            continue;
        }
        if (n == 0) {
            closeFd(fd);
            return;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return;
        }
        execErrno = errno;
        execFailed = true;
        closeFd(fd);
        return;
    }
}

void pollOpenFds(int stdoutFd, int stderrFd, int execFd, int timeoutMs) {
    std::vector<pollfd> fds;
    if (stdoutFd >= 0) {
        fds.push_back({stdoutFd, POLLIN, 0});
    }
    if (stderrFd >= 0) {
        fds.push_back({stderrFd, POLLIN, 0});
    }
    if (execFd >= 0) {
        fds.push_back({execFd, POLLIN, 0});
    }

    if (fds.empty()) {
        if (timeoutMs > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(timeoutMs));
        }
        return;
    }

    while (poll(fds.data(), fds.size(), timeoutMs) < 0) {
        if (errno == EINTR) {
            continue;
        }
        break;
    }
}

bool reapProcess(pid_t pid, int& status, bool& processExited, std::string& error) {
    if (processExited) {
        return true;
    }

    while (true) {
        const pid_t done = waitpid(pid, &status, WNOHANG);
        if (done == pid) {
            processExited = true;
            return true;
        }
        if (done == 0) {
            return true;
        }
        if (errno == EINTR) {
            continue;
        }
        error = std::string("failed to wait process: ") + std::strerror(errno);
        return false;
    }
}

void reapProcessBlocking(pid_t pid, int& status, bool& processExited) {
    if (processExited) {
        return;
    }

    while (true) {
        const pid_t done = waitpid(pid, &status, 0);
        if (done == pid) {
            processExited = true;
            return;
        }
        if (done < 0 && errno == EINTR) {
            continue;
        }
        processExited = true;
        return;
    }
}

int millisecondsUntil(const Clock::time_point& deadline, int maximumMs) {
    const auto now = Clock::now();
    if (now >= deadline) {
        return 0;
    }
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
    return static_cast<int>(std::min<long long>(maximumMs, std::max<long long>(1, remaining)));
}

void closeCaptureFds(int& stdoutFd, int& stderrFd, int& execFd) {
    closeFd(stdoutFd);
    closeFd(stderrFd);
    closeFd(execFd);
}

bool readExecErrorBlocking(int fd, int& execErrno, std::string& error) {
    std::array<char, sizeof(int)> buffer{};
    size_t bytesRead = 0;
    while (bytesRead < buffer.size()) {
        const ssize_t n = read(fd, buffer.data() + bytesRead, buffer.size() - bytesRead);
        if (n > 0) {
            bytesRead += static_cast<size_t>(n);
            continue;
        }
        if (n == 0) {
            return false;
        }
        if (errno == EINTR) {
            continue;
        }
        error = std::string("failed to read exec status: ") + std::strerror(errno);
        return true;
    }
    std::memcpy(&execErrno, buffer.data(), sizeof(execErrno));
    return true;
}

} // namespace

std::string currentExecutablePath(const std::string& argv0) {
    std::error_code ec;
#if defined(__APPLE__)
    uint32_t size = 0;
    (void)_NSGetExecutablePath(nullptr, &size);
    if (size > 0) {
        std::vector<char> buffer(size + 1, '\0');
        if (_NSGetExecutablePath(buffer.data(), &size) == 0) {
            std::filesystem::path resolved(buffer.data());
            auto absolute = std::filesystem::absolute(resolved, ec);
            if (!ec) {
                return absolute.lexically_normal().string();
            }
            return resolved.lexically_normal().string();
        }
    }
#else
    const auto procSelf = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (!ec && !procSelf.empty()) {
        auto absolute = std::filesystem::absolute(procSelf, ec);
        if (!ec) {
            return absolute.lexically_normal().string();
        }
        return procSelf.lexically_normal().string();
    }
#endif
    if (!argv0.empty()) {
        ec.clear();
        auto absolute = std::filesystem::absolute(std::filesystem::path(argv0), ec);
        if (!ec) {
            return absolute.lexically_normal().string();
        }
        return argv0;
    }
    return "";
}

bool isExecutablePath(const std::string& path) {
    if (path.empty()) {
        return false;
    }

    std::error_code ec;
    const bool regular = std::filesystem::is_regular_file(std::filesystem::path(path), ec);
    if (ec || !regular) {
        return false;
    }
    return access(path.c_str(), X_OK) == 0;
}

ProcessRunResult runProcessCapture(const std::string& binaryPath, const std::vector<std::string>& args, int timeoutSec) {
    ProcessRunResult result;
    if (!isExecutablePath(binaryPath)) {
        result.error = "binary path is not executable: " + binaryPath;
        return result;
    }

    int stdoutPipe[2] = {-1, -1};
    int stderrPipe[2] = {-1, -1};
    int execPipe[2] = {-1, -1};
    std::string setupError;
    if (!makePipe(stdoutPipe, setupError) || !makePipe(stderrPipe, setupError) || !makePipe(execPipe, setupError) ||
        !setCloseOnExec(execPipe[1], setupError)) {
        closePipe(stdoutPipe);
        closePipe(stderrPipe);
        closePipe(execPipe);
        result.error = setupError;
        return result;
    }

    pid_t pid = fork();
    if (pid < 0) {
        closePipe(stdoutPipe);
        closePipe(stderrPipe);
        closePipe(execPipe);
        result.error = std::string("failed to fork: ") + std::strerror(errno);
        return result;
    }

    if (pid == 0) {
        closeFd(stdoutPipe[0]);
        closeFd(stderrPipe[0]);
        closeFd(execPipe[0]);
        execChild(binaryPath, args, stdoutPipe[1], stderrPipe[1], execPipe[1]);
    }

    closeFd(stdoutPipe[1]);
    closeFd(stderrPipe[1]);
    closeFd(execPipe[1]);

    if (!setNonBlocking(stdoutPipe[0], setupError) || !setNonBlocking(stderrPipe[0], setupError) ||
        !setNonBlocking(execPipe[0], setupError)) {
        kill(pid, SIGKILL);
        int status = 0;
        (void)waitpid(pid, &status, 0);
        closeCaptureFds(stdoutPipe[0], stderrPipe[0], execPipe[0]);
        result.error = setupError;
        return result;
    }

    int status = 0;
    bool processExited = false;
    bool execFailed = false;
    int execErrno = 0;
    std::array<char, sizeof(int)> execBuffer{};
    size_t execBytesRead = 0;
    const bool hasTimeout = timeoutSec > 0;
    const int effectiveTimeoutSec = std::max(1, timeoutSec);
    const auto deadline = Clock::now() + std::chrono::seconds(effectiveTimeoutSec);
    Clock::time_point exitedAt{};

    while (true) {
        readTextPipe(stdoutPipe[0], result.output);
        readTextPipe(stderrPipe[0], result.error);
        readExecPipe(execPipe[0], execBuffer, execBytesRead, execFailed, execErrno);

        std::string waitError;
        if (!reapProcess(pid, status, processExited, waitError)) {
            closeCaptureFds(stdoutPipe[0], stderrPipe[0], execPipe[0]);
            result.error = waitError;
            return result;
        }
        if (processExited && exitedAt == Clock::time_point{}) {
            exitedAt = Clock::now();
        }

        if (!processExited && hasTimeout && Clock::now() >= deadline) {
            result.timedOut = true;
            kill(pid, SIGTERM);
            const auto graceDeadline = Clock::now() + std::chrono::milliseconds(200);
            while (!processExited && Clock::now() < graceDeadline) {
                readTextPipe(stdoutPipe[0], result.output);
                readTextPipe(stderrPipe[0], result.error);
                readExecPipe(execPipe[0], execBuffer, execBytesRead, execFailed, execErrno);
                std::string ignored;
                if (!reapProcess(pid, status, processExited, ignored)) {
                    break;
                }
                if (!processExited) {
                    pollOpenFds(stdoutPipe[0], stderrPipe[0], execPipe[0], millisecondsUntil(graceDeadline, 20));
                }
            }
            if (!processExited) {
                kill(pid, SIGKILL);
                reapProcessBlocking(pid, status, processExited);
            }
            readTextPipe(stdoutPipe[0], result.output);
            readTextPipe(stderrPipe[0], result.error);
            readExecPipe(execPipe[0], execBuffer, execBytesRead, execFailed, execErrno);
            closeCaptureFds(stdoutPipe[0], stderrPipe[0], execPipe[0]);
            result.started = !execFailed;
            result.exitCode = 124;
            result.error = "command timed out after " + std::to_string(effectiveTimeoutSec) + " seconds";
            return result;
        }

        if (processExited && stdoutPipe[0] < 0 && stderrPipe[0] < 0 && execPipe[0] < 0) {
            break;
        }

        if (processExited && exitedAt != Clock::time_point{} && Clock::now() - exitedAt > std::chrono::milliseconds(500)) {
            closeCaptureFds(stdoutPipe[0], stderrPipe[0], execPipe[0]);
            break;
        }

        const int pollTimeout = hasTimeout && !processExited ? millisecondsUntil(deadline, 20) : 20;
        pollOpenFds(stdoutPipe[0], stderrPipe[0], execPipe[0], pollTimeout);
    }

    closeCaptureFds(stdoutPipe[0], stderrPipe[0], execPipe[0]);
    result.exitCode = decodeWaitStatus(status);
    if (execFailed) {
        result.started = false;
        result.error = std::string("failed to exec process: ") + std::strerror(execErrno);
        return result;
    }

    result.started = true;
    if (result.exitCode != 0 && result.error.empty()) {
        result.error = "command failed with exit code " + std::to_string(result.exitCode);
    }
    return result;
}

int runProcessForeground(const std::string& binaryPath, const std::vector<std::string>& args, std::string& error) {
    error.clear();
    if (!isExecutablePath(binaryPath)) {
        error = "binary path is not executable: " + binaryPath;
        return 1;
    }

    int execPipe[2] = {-1, -1};
    if (!makePipe(execPipe, error) || !setCloseOnExec(execPipe[1], error)) {
        closePipe(execPipe);
        return 1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        closePipe(execPipe);
        error = std::string("failed to fork process: ") + std::strerror(errno);
        return 1;
    }

    if (pid == 0) {
        closeFd(execPipe[0]);
        execChild(binaryPath, args, -1, -1, execPipe[1]);
    }

    closeFd(execPipe[1]);
    int execErrno = 0;
    std::string execReadError;
    const bool execFailed = readExecErrorBlocking(execPipe[0], execErrno, execReadError);
    closeFd(execPipe[0]);

    int status = 0;
    bool processExited = false;
    reapProcessBlocking(pid, status, processExited);

    if (!execReadError.empty()) {
        error = execReadError;
        return 1;
    }
    if (execFailed) {
        error = std::string("failed to exec process: ") + std::strerror(execErrno);
        return 127;
    }
    return decodeWaitStatus(status);
}

BackgroundProcessResult startBackgroundProcess(
    const std::string& binaryPath,
    const std::vector<std::string>& args,
    const std::filesystem::path& logPath
) {
    BackgroundProcessResult result;
    if (!isExecutablePath(binaryPath)) {
        result.error = "binary path is not executable: " + binaryPath;
        return result;
    }

    std::error_code ec;
    if (!logPath.parent_path().empty()) {
        std::filesystem::create_directories(logPath.parent_path(), ec);
        if (ec) {
            result.error = "failed to create log directory: " + ec.message();
            return result;
        }
    }

    const int logFd = open(logPath.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (logFd < 0) {
        result.error = "failed to open log file: " + logPath.string() + ": " + std::strerror(errno);
        return result;
    }

    int execPipe[2] = {-1, -1};
    std::string setupError;
    if (!makePipe(execPipe, setupError) || !setCloseOnExec(execPipe[1], setupError)) {
        close(logFd);
        closePipe(execPipe);
        result.error = setupError;
        return result;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(logFd);
        closePipe(execPipe);
        result.error = std::string("failed to fork background process: ") + std::strerror(errno);
        return result;
    }

    if (pid == 0) {
        closeFd(execPipe[0]);

        int execStatusFd = execPipe[1];
        if (execStatusFd <= STDERR_FILENO) {
            const int originalExecStatusFd = execStatusFd;
            execStatusFd = duplicateFdAtLeastOrExit(execStatusFd, STDERR_FILENO + 1, originalExecStatusFd);
            setCloseOnExecOrExit(execStatusFd, execStatusFd);
            close(originalExecStatusFd);
        }

        if (setsid() < 0) {
            writeExecErrorAndExit(execStatusFd, errno);
        }

        int safeLogFd = logFd;
        if (safeLogFd <= STDERR_FILENO) {
            safeLogFd = duplicateFdAtLeastOrExit(logFd, STDERR_FILENO + 1, execStatusFd);
            close(logFd);
        }
        if (dup2(safeLogFd, STDOUT_FILENO) < 0 || dup2(safeLogFd, STDERR_FILENO) < 0) {
            writeExecErrorAndExit(execStatusFd, errno);
        }
        if (safeLogFd > STDERR_FILENO) {
            close(safeLogFd);
        }

        const int stdinFd = open("/dev/null", O_RDONLY);
        if (stdinFd >= 0) {
            if (dup2(stdinFd, STDIN_FILENO) < 0) {
                writeExecErrorAndExit(execStatusFd, errno);
            }
            if (stdinFd > STDERR_FILENO) {
                close(stdinFd);
            }
        }
        execChild(binaryPath, args, -1, -1, execStatusFd);
    }

    close(logFd);
    closeFd(execPipe[1]);

    int execErrno = 0;
    std::string execReadError;
    const bool execFailed = readExecErrorBlocking(execPipe[0], execErrno, execReadError);
    closeFd(execPipe[0]);

    if (!execReadError.empty()) {
        int status = 0;
        (void)waitpid(pid, &status, 0);
        result.error = execReadError;
        return result;
    }
    if (execFailed) {
        int status = 0;
        (void)waitpid(pid, &status, 0);
        result.error = std::string("failed to exec background process: ") + std::strerror(execErrno);
        return result;
    }

    result.started = true;
    result.pid = static_cast<int>(pid);
    return result;
}

bool isProcessRunning(int pid) {
    if (pid <= 0) {
        return false;
    }
    if (kill(static_cast<pid_t>(pid), 0) == 0) {
        return true;
    }
    return errno != ESRCH;
}

bool terminateProcess(int pid, int timeoutSec, std::string& error) {
    error.clear();
    if (pid <= 0) {
        return true;
    }

    const pid_t posixPid = static_cast<pid_t>(pid);
    int status = 0;
    while (waitpid(posixPid, &status, WNOHANG) < 0 && errno == EINTR) {
    }
    if (!isProcessRunning(pid)) {
        return true;
    }

    if (kill(posixPid, SIGTERM) != 0 && errno != ESRCH) {
        error = "failed to send SIGTERM: " + std::string(std::strerror(errno));
        return false;
    }

    const auto deadline = Clock::now() + std::chrono::seconds(std::max(1, timeoutSec));
    while (Clock::now() < deadline) {
        const pid_t done = waitpid(posixPid, &status, WNOHANG);
        if (done == posixPid) {
            return true;
        }
        if (done < 0 && errno != EINTR && errno != ECHILD) {
            break;
        }
        if (done < 0 && errno == ECHILD && !isProcessRunning(pid)) {
            return true;
        }
        if (!isProcessRunning(pid)) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    if (!isProcessRunning(pid)) {
        return true;
    }

    if (kill(posixPid, SIGKILL) != 0 && errno != ESRCH) {
        error = "failed to send SIGKILL: " + std::string(std::strerror(errno));
        return false;
    }

    const auto killDeadline = Clock::now() + std::chrono::seconds(1);
    while (Clock::now() < killDeadline) {
        const pid_t done = waitpid(posixPid, &status, WNOHANG);
        if (done == posixPid) {
            return true;
        }
        if (done < 0 && errno != EINTR && errno != ECHILD) {
            break;
        }
        if (done < 0 && errno == ECHILD && !isProcessRunning(pid)) {
            return true;
        }
        if (!isProcessRunning(pid)) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    if (isProcessRunning(pid)) {
        error = "process did not terminate after SIGKILL";
        return false;
    }
    return true;
}

} // namespace subcli
