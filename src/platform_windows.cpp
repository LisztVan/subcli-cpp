#include "subcli/platform.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

namespace subcli {
namespace {

class UniqueHandle {
public:
    UniqueHandle() = default;
    explicit UniqueHandle(HANDLE handle) : handle_(handle) {}
    ~UniqueHandle() { reset(); }

    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;

    UniqueHandle(UniqueHandle&& other) noexcept : handle_(other.release()) {}

    UniqueHandle& operator=(UniqueHandle&& other) noexcept {
        if (this != &other) {
            reset(other.release());
        }
        return *this;
    }

    bool valid() const { return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE; }
    HANDLE get() const { return handle_; }

    HANDLE release() {
        HANDLE released = handle_;
        handle_ = nullptr;
        return released;
    }

    void reset(HANDLE handle = nullptr) {
        if (valid()) {
            CloseHandle(handle_);
        }
        handle_ = handle;
    }

private:
    HANDLE handle_ = nullptr;
};

std::wstring widenBytes(const std::string& value) {
    std::wstring out;
    out.reserve(value.size());
    for (unsigned char ch : value) {
        out.push_back(static_cast<wchar_t>(ch));
    }
    return out;
}

std::string narrowWide(const std::wstring& value) {
    std::string out;
    out.reserve(value.size());
    for (wchar_t ch : value) {
        out.push_back(ch <= 0x7f ? static_cast<char>(ch) : '?');
    }
    return out;
}

std::wstring utf8ToWide(const std::string& value) {
    if (value.empty()) {
        return L"";
    }
    if (value.size() > static_cast<size_t>(std::numeric_limits<int>::max())) {
        return L"";
    }

    const int inputSize = static_cast<int>(value.size());
    int needed = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), inputSize, nullptr, 0);
    UINT codePage = CP_UTF8;
    DWORD flags = MB_ERR_INVALID_CHARS;
    if (needed <= 0) {
        codePage = CP_ACP;
        flags = 0;
        needed = MultiByteToWideChar(codePage, flags, value.data(), inputSize, nullptr, 0);
    }
    if (needed <= 0) {
        return widenBytes(value);
    }

    std::wstring out(static_cast<size_t>(needed), L'\0');
    const int written = MultiByteToWideChar(codePage, flags, value.data(), inputSize, out.data(), needed);
    if (written <= 0) {
        return widenBytes(value);
    }
    out.resize(static_cast<size_t>(written));
    return out;
}

std::string wideToUtf8(const std::wstring& value) {
    if (value.empty()) {
        return "";
    }
    if (value.size() > static_cast<size_t>(std::numeric_limits<int>::max())) {
        return "";
    }

    const int inputSize = static_cast<int>(value.size());
    const int needed = WideCharToMultiByte(CP_UTF8, 0, value.data(), inputSize, nullptr, 0, nullptr, nullptr);
    if (needed <= 0) {
        return narrowWide(value);
    }

    std::string out(static_cast<size_t>(needed), '\0');
    const int written = WideCharToMultiByte(CP_UTF8, 0, value.data(), inputSize, out.data(), needed, nullptr, nullptr);
    if (written <= 0) {
        return narrowWide(value);
    }
    out.resize(static_cast<size_t>(written));
    return out;
}

std::string lastWindowsError(DWORD errorCode) {
    wchar_t* rawMessage = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        errorCode,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&rawMessage),
        0,
        nullptr
    );

    std::wstring message;
    if (length > 0 && rawMessage != nullptr) {
        message.assign(rawMessage, rawMessage + length);
        LocalFree(rawMessage);
    }

    while (!message.empty() &&
           (message.back() == L'\r' || message.back() == L'\n' || message.back() == L' ' || message.back() == L'\t')) {
        message.pop_back();
    }

    if (message.empty()) {
        return "Windows error " + std::to_string(errorCode);
    }
    return wideToUtf8(message) + " (error " + std::to_string(errorCode) + ")";
}

std::string lastWindowsError(const std::string& action, DWORD errorCode) {
    return action + ": " + lastWindowsError(errorCode);
}

std::string lastWindowsError(const std::string& action) {
    return lastWindowsError(action, GetLastError());
}

bool needsWindowsQuoting(const std::wstring& arg) {
    if (arg.empty()) {
        return true;
    }
    return arg.find_first_of(L" \t\r\n\"") != std::wstring::npos;
}

std::wstring quoteWindowsArgument(const std::wstring& arg) {
    if (!needsWindowsQuoting(arg)) {
        return arg;
    }

    std::wstring quoted;
    quoted.push_back(L'\"');
    size_t backslashes = 0;
    for (wchar_t ch : arg) {
        if (ch == L'\\') {
            ++backslashes;
            continue;
        }
        if (ch == L'\"') {
            quoted.append(backslashes * 2 + 1, L'\\');
            quoted.push_back(ch);
            backslashes = 0;
            continue;
        }
        quoted.append(backslashes, L'\\');
        backslashes = 0;
        quoted.push_back(ch);
    }
    quoted.append(backslashes * 2, L'\\');
    quoted.push_back(L'\"');
    return quoted;
}

std::wstring buildCommandLine(const std::string& binaryPath, const std::vector<std::string>& args) {
    std::wstring commandLine = quoteWindowsArgument(utf8ToWide(binaryPath));
    for (const auto& arg : args) {
        commandLine.push_back(L' ');
        commandLine += quoteWindowsArgument(utf8ToWide(arg));
    }
    return commandLine;
}

DWORD timeoutMilliseconds(int timeoutSec) {
    const unsigned long long seconds = static_cast<unsigned long long>(std::max(1, timeoutSec));
    const unsigned long long millis = seconds * 1000ULL;
    if (millis >= static_cast<unsigned long long>(INFINITE)) {
        return INFINITE - 1;
    }
    return static_cast<DWORD>(millis);
}

int processExitCodeToInt(DWORD exitCode) {
    return static_cast<int>(exitCode);
}

bool createInheritablePipe(UniqueHandle& readHandle, UniqueHandle& writeHandle, std::string& error) {
    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;

    HANDLE readRaw = nullptr;
    HANDLE writeRaw = nullptr;
    if (!CreatePipe(&readRaw, &writeRaw, &security, 0)) {
        error = lastWindowsError("failed to create pipe");
        return false;
    }

    readHandle.reset(readRaw);
    writeHandle.reset(writeRaw);
    if (!SetHandleInformation(readHandle.get(), HANDLE_FLAG_INHERIT, 0)) {
        error = lastWindowsError("failed to make pipe read handle non-inheritable");
        return false;
    }
    return true;
}

UniqueHandle openNulForRead(std::string& error) {
    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;

    UniqueHandle handle(CreateFileW(
        L"NUL",
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        &security,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    ));
    if (!handle.valid()) {
        error = lastWindowsError("failed to open NUL for stdin");
    }
    return handle;
}

UniqueHandle openAppendLogFile(const std::filesystem::path& logPath, std::string& error) {
    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;

    const std::wstring nativePath = logPath.wstring();
    UniqueHandle handle(CreateFileW(
        nativePath.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        &security,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    ));
    if (!handle.valid()) {
        error = lastWindowsError("failed to open log file: " + logPath.string());
        return handle;
    }

    LARGE_INTEGER zero{};
    if (!SetFilePointerEx(handle.get(), zero, nullptr, FILE_END)) {
        error = lastWindowsError("failed to seek log file to end: " + logPath.string());
        handle.reset();
    }
    return handle;
}

bool createWindowsProcess(
    const std::string& binaryPath,
    const std::vector<std::string>& args,
    STARTUPINFOW& startupInfo,
    DWORD creationFlags,
    BOOL inheritHandles,
    PROCESS_INFORMATION& processInfo,
    std::string& error
) {
    const std::wstring applicationName = utf8ToWide(binaryPath);
    std::wstring commandLine = buildCommandLine(binaryPath, args);
    std::vector<wchar_t> mutableCommandLine(commandLine.begin(), commandLine.end());
    mutableCommandLine.push_back(L'\0');

    startupInfo.cb = sizeof(startupInfo);
    if (!CreateProcessW(
            applicationName.c_str(),
            mutableCommandLine.data(),
            nullptr,
            nullptr,
            inheritHandles,
            creationFlags,
            nullptr,
            nullptr,
            &startupInfo,
            &processInfo
        )) {
        error = lastWindowsError("failed to create process: " + binaryPath);
        return false;
    }
    return true;
}

void drainAvailablePipe(HANDLE pipe, std::string& output, bool& pipeOpen) {
    if (!pipeOpen) {
        return;
    }

    while (true) {
        DWORD available = 0;
        if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr)) {
            const DWORD errorCode = GetLastError();
            if (errorCode == ERROR_BROKEN_PIPE || errorCode == ERROR_HANDLE_EOF || errorCode == ERROR_INVALID_HANDLE) {
                pipeOpen = false;
                return;
            }
            pipeOpen = false;
            return;
        }
        if (available == 0) {
            return;
        }

        char buffer[4096];
        const DWORD toRead = std::min<DWORD>(available, static_cast<DWORD>(sizeof(buffer)));
        DWORD bytesRead = 0;
        if (!ReadFile(pipe, buffer, toRead, &bytesRead, nullptr)) {
            const DWORD errorCode = GetLastError();
            if (errorCode == ERROR_BROKEN_PIPE || errorCode == ERROR_HANDLE_EOF || errorCode == ERROR_INVALID_HANDLE) {
                pipeOpen = false;
                return;
            }
            pipeOpen = false;
            return;
        }
        if (bytesRead == 0) {
            return;
        }
        output.append(buffer, buffer + bytesRead);
    }
}

bool markProcessExited(HANDLE process, DWORD& exitCode, std::string& error) {
    if (!GetExitCodeProcess(process, &exitCode)) {
        error = lastWindowsError("failed to get process exit code");
        return false;
    }
    return true;
}

bool duplicateStandardHandle(DWORD standardHandleId, UniqueHandle& duplicate, std::string& error) {
    HANDLE source = GetStdHandle(standardHandleId);
    if (source == nullptr || source == INVALID_HANDLE_VALUE) {
        return false;
    }

    HANDLE duplicatedRaw = nullptr;
    if (!DuplicateHandle(
            GetCurrentProcess(),
            source,
            GetCurrentProcess(),
            &duplicatedRaw,
            0,
            TRUE,
            DUPLICATE_SAME_ACCESS
        )) {
        error = lastWindowsError("failed to duplicate standard handle");
        return false;
    }
    duplicate.reset(duplicatedRaw);
    return true;
}

bool configureForegroundHandles(STARTUPINFOW& startupInfo, UniqueHandle& stdinHandle, UniqueHandle& stdoutHandle, UniqueHandle& stderrHandle, std::string& error) {
    const bool haveStdin = duplicateStandardHandle(STD_INPUT_HANDLE, stdinHandle, error);
    if (!haveStdin && !error.empty()) {
        return false;
    }
    const bool haveStdout = duplicateStandardHandle(STD_OUTPUT_HANDLE, stdoutHandle, error);
    if (!haveStdout && !error.empty()) {
        return false;
    }
    const bool haveStderr = duplicateStandardHandle(STD_ERROR_HANDLE, stderrHandle, error);
    if (!haveStderr && !error.empty()) {
        return false;
    }

    if (haveStdin && haveStdout && haveStderr) {
        startupInfo.dwFlags |= STARTF_USESTDHANDLES;
        startupInfo.hStdInput = stdinHandle.get();
        startupInfo.hStdOutput = stdoutHandle.get();
        startupInfo.hStdError = stderrHandle.get();
    }
    return true;
}

} // namespace

std::string currentExecutablePath(const std::string& argv0) {
    std::vector<wchar_t> buffer(MAX_PATH);
    while (true) {
        SetLastError(ERROR_SUCCESS);
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            break;
        }
        if (length < buffer.size() - 1 || GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
            return wideToUtf8(std::wstring(buffer.data(), buffer.data() + length));
        }
        if (buffer.size() > 32768) {
            break;
        }
        buffer.resize(buffer.size() * 2);
    }

    if (!argv0.empty()) {
        std::error_code ec;
        const auto absolute = std::filesystem::absolute(std::filesystem::path(argv0), ec);
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
    return !ec && regular;
}

ProcessRunResult runProcessCapture(const std::string& binaryPath, const std::vector<std::string>& args, int timeoutSec) {
    ProcessRunResult result;
    if (!isExecutablePath(binaryPath)) {
        result.error = "binary path is not executable: " + binaryPath;
        return result;
    }

    UniqueHandle stdoutRead;
    UniqueHandle stdoutWrite;
    UniqueHandle stderrRead;
    UniqueHandle stderrWrite;
    std::string setupError;
    if (!createInheritablePipe(stdoutRead, stdoutWrite, setupError) ||
        !createInheritablePipe(stderrRead, stderrWrite, setupError)) {
        result.error = setupError;
        return result;
    }

    UniqueHandle stdinHandle = openNulForRead(setupError);
    if (!stdinHandle.valid()) {
        result.error = setupError;
        return result;
    }

    STARTUPINFOW startupInfo{};
    startupInfo.dwFlags = STARTF_USESTDHANDLES;
    startupInfo.hStdInput = stdinHandle.get();
    startupInfo.hStdOutput = stdoutWrite.get();
    startupInfo.hStdError = stderrWrite.get();

    PROCESS_INFORMATION processInfo{};
    if (!createWindowsProcess(binaryPath, args, startupInfo, 0, TRUE, processInfo, result.error)) {
        return result;
    }

    result.started = true;
    UniqueHandle processHandle(processInfo.hProcess);
    UniqueHandle threadHandle(processInfo.hThread);
    stdoutWrite.reset();
    stderrWrite.reset();

    const bool hasTimeout = timeoutSec > 0;
    const int effectiveTimeoutSec = std::max(1, timeoutSec);
    const unsigned long long timeoutMs = static_cast<unsigned long long>(effectiveTimeoutSec) * 1000ULL;
    const ULONGLONG startedAt = GetTickCount64();

    bool processExited = false;
    bool stdoutOpen = true;
    bool stderrOpen = true;
    DWORD childExitCode = 0;
    ULONGLONG drainDeadline = 0;

    while (true) {
        drainAvailablePipe(stdoutRead.get(), result.output, stdoutOpen);
        drainAvailablePipe(stderrRead.get(), result.error, stderrOpen);

        if (!processExited) {
            const DWORD immediateWait = WaitForSingleObject(processHandle.get(), 0);
            if (immediateWait == WAIT_OBJECT_0) {
                if (!markProcessExited(processHandle.get(), childExitCode, result.error)) {
                    return result;
                }
                processExited = true;
                drainDeadline = GetTickCount64() + 500ULL;
            } else if (immediateWait == WAIT_FAILED) {
                result.error = lastWindowsError("failed to wait for process");
                TerminateProcess(processHandle.get(), 1);
                WaitForSingleObject(processHandle.get(), 1000);
                return result;
            } else if (hasTimeout && GetTickCount64() - startedAt >= timeoutMs) {
                result.timedOut = true;
                if (!TerminateProcess(processHandle.get(), 124)) {
                    const DWORD terminateError = GetLastError();
                    const DWORD postFailureWait = WaitForSingleObject(processHandle.get(), 0);
                    if (postFailureWait == WAIT_FAILED) {
                        result.exitCode = 124;
                        result.error = lastWindowsError("failed to check timed out process state after termination failure");
                        return result;
                    }
                    if (postFailureWait != WAIT_OBJECT_0) {
                        result.exitCode = 124;
                        result.error = lastWindowsError("failed to terminate timed out process", terminateError);
                        return result;
                    }
                }

                const DWORD terminateWait = WaitForSingleObject(processHandle.get(), 5000);
                if (terminateWait != WAIT_OBJECT_0) {
                    result.exitCode = 124;
                    if (terminateWait == WAIT_TIMEOUT) {
                        result.error = "timed out process termination did not complete within 5 seconds";
                    } else if (terminateWait == WAIT_FAILED) {
                        result.error = lastWindowsError("failed to wait for timed out process termination");
                    } else {
                        result.error = "unexpected wait state while waiting for timed out process termination";
                    }
                    return result;
                }

                const ULONGLONG timeoutDrainDeadline = GetTickCount64() + 500ULL;
                while (GetTickCount64() < timeoutDrainDeadline) {
                    drainAvailablePipe(stdoutRead.get(), result.output, stdoutOpen);
                    drainAvailablePipe(stderrRead.get(), result.error, stderrOpen);
                    if (!stdoutOpen && !stderrOpen) {
                        break;
                    }
                    Sleep(10);
                }
                drainAvailablePipe(stdoutRead.get(), result.output, stdoutOpen);
                drainAvailablePipe(stderrRead.get(), result.error, stderrOpen);

                result.exitCode = 124;
                const std::string timeoutMessage = "command timed out after " + std::to_string(effectiveTimeoutSec) + " seconds";
                if (result.error.empty()) {
                    result.error = timeoutMessage;
                } else {
                    result.error += "\n" + timeoutMessage;
                }
                return result;
            } else {
                DWORD waitMs = 20;
                if (hasTimeout) {
                    const unsigned long long elapsed = GetTickCount64() - startedAt;
                    const unsigned long long remaining = elapsed >= timeoutMs ? 1ULL : timeoutMs - elapsed;
                    waitMs = static_cast<DWORD>(std::min<unsigned long long>(20ULL, std::max<unsigned long long>(1ULL, remaining)));
                }

                const DWORD waitResult = WaitForSingleObject(processHandle.get(), waitMs);
                if (waitResult == WAIT_OBJECT_0) {
                    if (!markProcessExited(processHandle.get(), childExitCode, result.error)) {
                        return result;
                    }
                    processExited = true;
                    drainDeadline = GetTickCount64() + 500ULL;
                } else if (waitResult == WAIT_FAILED) {
                    result.error = lastWindowsError("failed to wait for process");
                    TerminateProcess(processHandle.get(), 1);
                    WaitForSingleObject(processHandle.get(), 1000);
                    return result;
                }
            }
        } else {
            if (!stdoutOpen && !stderrOpen) {
                break;
            }
            if (drainDeadline != 0 && GetTickCount64() >= drainDeadline) {
                break;
            }
            Sleep(10);
        }
    }

    result.exitCode = processExitCodeToInt(childExitCode);
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

    STARTUPINFOW startupInfo{};
    UniqueHandle stdinHandle;
    UniqueHandle stdoutHandle;
    UniqueHandle stderrHandle;
    if (!configureForegroundHandles(startupInfo, stdinHandle, stdoutHandle, stderrHandle, error)) {
        return 1;
    }

    PROCESS_INFORMATION processInfo{};
    if (!createWindowsProcess(binaryPath, args, startupInfo, 0, TRUE, processInfo, error)) {
        return 1;
    }

    UniqueHandle processHandle(processInfo.hProcess);
    UniqueHandle threadHandle(processInfo.hThread);
    const DWORD waitResult = WaitForSingleObject(processHandle.get(), INFINITE);
    if (waitResult == WAIT_FAILED) {
        error = lastWindowsError("failed to wait for foreground process");
        return 1;
    }

    DWORD exitCode = 1;
    if (!GetExitCodeProcess(processHandle.get(), &exitCode)) {
        error = lastWindowsError("failed to get foreground process exit code");
        return 1;
    }
    return processExitCodeToInt(exitCode);
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
    const auto parentPath = logPath.parent_path();
    if (!parentPath.empty()) {
        std::filesystem::create_directories(parentPath, ec);
        if (ec) {
            result.error = "failed to create log directory: " + ec.message();
            return result;
        }
    }

    UniqueHandle logHandle = openAppendLogFile(logPath, result.error);
    if (!logHandle.valid()) {
        return result;
    }

    std::string stdinError;
    UniqueHandle stdinHandle = openNulForRead(stdinError);
    if (!stdinHandle.valid()) {
        result.error = stdinError;
        return result;
    }

    STARTUPINFOW startupInfo{};
    startupInfo.dwFlags = STARTF_USESTDHANDLES;
    startupInfo.hStdInput = stdinHandle.get();
    startupInfo.hStdOutput = logHandle.get();
    startupInfo.hStdError = logHandle.get();

    PROCESS_INFORMATION processInfo{};
    const DWORD creationFlags = DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP;
    if (!createWindowsProcess(binaryPath, args, startupInfo, creationFlags, TRUE, processInfo, result.error)) {
        return result;
    }

    UniqueHandle processHandle(processInfo.hProcess);
    UniqueHandle threadHandle(processInfo.hThread);
    result.started = true;
    result.pid = static_cast<int>(processInfo.dwProcessId);
    return result;
}

bool isProcessRunning(int pid) {
    if (pid <= 0) {
        return false;
    }

    UniqueHandle processHandle(OpenProcess(
        SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION,
        FALSE,
        static_cast<DWORD>(pid)
    ));
    if (!processHandle.valid()) {
        return false;
    }

    const DWORD waitResult = WaitForSingleObject(processHandle.get(), 0);
    return waitResult == WAIT_TIMEOUT;
}

bool terminateProcess(int pid, int timeoutSec, std::string& error) {
    error.clear();
    if (pid <= 0) {
        return true;
    }

    UniqueHandle processHandle(OpenProcess(
        PROCESS_TERMINATE | SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION,
        FALSE,
        static_cast<DWORD>(pid)
    ));
    if (!processHandle.valid()) {
        const DWORD openError = GetLastError();
        if (openError == ERROR_INVALID_PARAMETER) {
            return true;
        }
        error = lastWindowsError("failed to open process for termination", openError);
        return false;
    }

    const DWORD initialWait = WaitForSingleObject(processHandle.get(), 0);
    if (initialWait == WAIT_OBJECT_0) {
        return true;
    }
    if (initialWait == WAIT_FAILED) {
        error = lastWindowsError("failed to check process before termination");
        return false;
    }
    if (initialWait != WAIT_TIMEOUT) {
        error = "unexpected process wait state before termination";
        return false;
    }

    if (!TerminateProcess(processHandle.get(), 1)) {
        const DWORD terminateError = GetLastError();
        const DWORD postFailureWait = WaitForSingleObject(processHandle.get(), 0);
        if (postFailureWait == WAIT_OBJECT_0) {
            return true;
        }
        if (postFailureWait == WAIT_FAILED) {
            error = lastWindowsError("failed to check process after termination failure");
            return false;
        }
        error = lastWindowsError("failed to terminate process", terminateError);
        return false;
    }

    const DWORD waitResult = WaitForSingleObject(processHandle.get(), timeoutMilliseconds(timeoutSec));
    if (waitResult == WAIT_OBJECT_0) {
        return true;
    }
    if (waitResult == WAIT_TIMEOUT) {
        error = "process did not terminate within " + std::to_string(std::max(1, timeoutSec)) + " seconds";
        return false;
    }

    error = lastWindowsError("failed to wait for terminated process");
    return false;
}

} // namespace subcli
