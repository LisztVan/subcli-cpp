#pragma once

#include <cstdint>
#include <string>

#include "subcli/platform.hpp"

namespace subcli {

ProcessCommand testSupportSuccessCommand();
ProcessCommand testSupportFailureCommand();
ProcessCommand testSupportLongRunningCommand(int seconds);
ProcessCommand testSupportShellCommand(const std::string& script);
std::string testSupportMissingExecutablePath();

struct TestTcpServerHandle {
#ifdef _WIN32
    uintptr_t socket = 0;
#else
    int socket = -1;
#endif
    int port = 0;
};

void testSupportEnsureSocketsReady();
TestTcpServerHandle testSupportCreateLoopbackServer();
int testSupportAccept(TestTcpServerHandle& server);
int testSupportRecv(int client, char* buffer, int length);
int testSupportSend(int client, const char* buffer, int length);
void testSupportCloseClient(int client);
void testSupportCloseServer(TestTcpServerHandle& server);

} // namespace subcli
