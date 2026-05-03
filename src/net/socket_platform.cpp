#include "net/socket_platform.hpp"

#include <cstring>
#include <mutex>
#include <sstream>

namespace portforwardx::net::detail {

namespace {
std::once_flag g_wsa_init_once;
bool g_wsa_init_ok = false;
std::string g_wsa_error;
}  // namespace

bool EnsureSocketApiInitialized(std::string* error_message) {
#ifdef _WIN32
  std::call_once(g_wsa_init_once, [] {
    WSADATA data{};
    const int rc = WSAStartup(MAKEWORD(2, 2), &data);
    if (rc != 0) {
      g_wsa_error = "WSAStartup failed: " + std::to_string(rc);
      g_wsa_init_ok = false;
      return;
    }
    g_wsa_init_ok = true;
  });
  if (!g_wsa_init_ok && error_message != nullptr) {
    *error_message = g_wsa_error;
  }
  return g_wsa_init_ok;
#else
  (void)error_message;
  return true;
#endif
}

int LastSocketError() {
#ifdef _WIN32
  return WSAGetLastError();
#else
  return errno;
#endif
}

std::string SocketErrorToString(int error) {
#ifdef _WIN32
  char* message_buffer = nullptr;
  const unsigned long size =
      FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                         FORMAT_MESSAGE_IGNORE_INSERTS,
                     nullptr, static_cast<unsigned long>(error),
                     MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                     reinterpret_cast<LPSTR>(&message_buffer), 0, nullptr);
  if (size == 0 || message_buffer == nullptr) {
    return "WinSock error " + std::to_string(error);
  }
  std::string message(message_buffer, size);
  LocalFree(message_buffer);
  return message;
#else
  return std::strerror(error);
#endif
}

void CloseSocket(SocketHandle socket) {
  if (socket == kInvalidSocket) {
    return;
  }
#ifdef _WIN32
  closesocket(socket);
#else
  close(socket);
#endif
}

bool SetReuseAddress(SocketHandle socket, std::string* error_message) {
  const int value = 1;
  const int rc = setsockopt(socket, SOL_SOCKET, SO_REUSEADDR,
                            reinterpret_cast<const char*>(&value),
                            static_cast<socklen_t>(sizeof(value)));
  if (rc == 0) {
    return true;
  }
  if (error_message != nullptr) {
    const int err = LastSocketError();
    *error_message =
        "setsockopt(SO_REUSEADDR) failed: " + SocketErrorToString(err);
  }
  return false;
}

bool SetIpv6Only(SocketHandle socket, bool enabled, std::string* error_message) {
  const int value = enabled ? 1 : 0;
  const int rc = setsockopt(socket, IPPROTO_IPV6, IPV6_V6ONLY,
                            reinterpret_cast<const char*>(&value),
                            static_cast<socklen_t>(sizeof(value)));
  if (rc == 0) {
    return true;
  }
  if (error_message != nullptr) {
    const int err = LastSocketError();
    *error_message =
        "setsockopt(IPV6_V6ONLY) failed: " + SocketErrorToString(err);
  }
  return false;
}

bool ShutdownRead(SocketHandle socket) {
  if (socket == kInvalidSocket) {
    return false;
  }
#ifdef _WIN32
  return shutdown(socket, SD_RECEIVE) == 0;
#else
  return shutdown(socket, SHUT_RD) == 0;
#endif
}

bool ShutdownWrite(SocketHandle socket) {
  if (socket == kInvalidSocket) {
    return false;
  }
#ifdef _WIN32
  return shutdown(socket, SD_SEND) == 0;
#else
  return shutdown(socket, SHUT_WR) == 0;
#endif
}

bool ShutdownBoth(SocketHandle socket) {
  if (socket == kInvalidSocket) {
    return false;
  }
#ifdef _WIN32
  return shutdown(socket, SD_BOTH) == 0;
#else
  return shutdown(socket, SHUT_RDWR) == 0;
#endif
}

std::int64_t Recv(SocketHandle socket, void* buffer, std::size_t length) {
#ifdef _WIN32
  const int rc = recv(socket, static_cast<char*>(buffer), static_cast<int>(length), 0);
  return rc;
#else
  const ssize_t rc = recv(socket, buffer, length, 0);
  return static_cast<std::int64_t>(rc);
#endif
}

std::int64_t Send(SocketHandle socket, const void* buffer, std::size_t length) {
#ifdef _WIN32
  const int rc =
      send(socket, static_cast<const char*>(buffer), static_cast<int>(length), 0);
  return rc;
#else
  const ssize_t rc = send(socket, buffer, length, 0);
  return static_cast<std::int64_t>(rc);
#endif
}

}  // namespace portforwardx::net::detail
