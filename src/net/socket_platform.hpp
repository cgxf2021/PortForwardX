#pragma once

#include <cstdint>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <mstcpip.h>
#include <ws2tcpip.h>
using SocketHandle = SOCKET;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <cerrno>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
using SocketHandle = int;
constexpr SocketHandle kInvalidSocket = -1;
#endif

namespace portforwardx::net::detail {

bool EnsureSocketApiInitialized(std::string* error_message);
int LastSocketError();
std::string SocketErrorToString(int error);
void CloseSocket(SocketHandle socket);
bool SetReuseAddress(SocketHandle socket, std::string* error_message);
bool SetIpv6Only(SocketHandle socket, bool enabled, std::string* error_message);
bool SetSocketBufferSize(SocketHandle socket, int buffer_size, std::string* error_message);
bool DisableUdpConnReset(SocketHandle socket, std::string* error_message);
bool ShutdownRead(SocketHandle socket);
bool ShutdownWrite(SocketHandle socket);
bool ShutdownBoth(SocketHandle socket);
std::int64_t Recv(SocketHandle socket, void* buffer, std::size_t length);
std::int64_t Send(SocketHandle socket, const void* buffer, std::size_t length);

}  // namespace portforwardx::net::detail
