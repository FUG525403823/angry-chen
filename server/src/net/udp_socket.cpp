// S04 §5.7：非阻塞 UDP 套接字。WinSock2 与 POSIX 两套实现共享同一接口语义。
#include "net/udp_socket.hpp"

#include <utility>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace ac::net {

namespace {

#if defined(_WIN32)
using NativeSocket = SOCKET;
constexpr NativeSocket kInvalidSocket = INVALID_SOCKET;

bool ensureWinsock() noexcept {
  static bool isStarted = false;
  if (isStarted) return true;
  WSADATA data{};
  isStarted = WSAStartup(MAKEWORD(2, 2), &data) == 0;
  return isStarted;
}

SocketError mapLastError() noexcept {
  switch (WSAGetLastError()) {
    case WSAEWOULDBLOCK:
      return SocketError::kWouldBlock;
    case WSAEINTR:
      return SocketError::kInterrupted;
    case WSAENOTSOCK:
    case WSAECONNRESET:
      return SocketError::kClosed;
    default:
      return SocketError::kOther;
  }
}
#else
using NativeSocket = int;
constexpr NativeSocket kInvalidSocket = -1;

bool ensureWinsock() noexcept { return true; }

SocketError mapLastError() noexcept {
  switch (errno) {
    case EAGAIN:
#if defined(EWOULDBLOCK) && EWOULDBLOCK != EAGAIN
    case EWOULDBLOCK:
#endif
      return SocketError::kWouldBlock;
    case EINTR:
      return SocketError::kInterrupted;
    case EBADF:
    case ENOTSOCK:
    case ECONNRESET:
      return SocketError::kClosed;
    default:
      return SocketError::kOther;
  }
}
#endif

NativeSocket toNative(std::intptr_t handle) noexcept { return static_cast<NativeSocket>(handle); }

sockaddr_in toSockaddr(const Endpoint& endpoint) noexcept {
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(endpoint.port);
  address.sin_addr.s_addr = htonl(endpoint.ipv4);
  return address;
}

Endpoint fromSockaddr(const sockaddr_in& address) noexcept {
  Endpoint endpoint{};
  endpoint.ipv4 = ntohl(address.sin_addr.s_addr);
  endpoint.port = ntohs(address.sin_port);
  return endpoint;
}

void closeNative(NativeSocket socket) noexcept {
#if defined(_WIN32)
  closesocket(socket);
#else
  ::close(socket);
#endif
}

bool setNonBlocking(NativeSocket socket) noexcept {
#if defined(_WIN32)
  u_long mode = 1u;
  return ioctlsocket(socket, FIONBIO, &mode) == 0;
#else
  const int flags = fcntl(socket, F_GETFL, 0);
  if (flags < 0) return false;
  return fcntl(socket, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

}  // namespace

UdpSocket::~UdpSocket() { close(); }

UdpSocket::UdpSocket(UdpSocket&& other) noexcept
    : handle_(other.handle_), port_(other.port_), lastError_(other.lastError_) {
  other.handle_ = -1;
  other.port_ = 0u;
}

UdpSocket& UdpSocket::operator=(UdpSocket&& other) noexcept {
  if (this != &other) {
    close();
    handle_ = other.handle_;
    port_ = other.port_;
    lastError_ = other.lastError_;
    other.handle_ = -1;
    other.port_ = 0u;
  }
  return *this;
}

bool UdpSocket::bind(uint16_t port) {
  close();
  lastError_ = SocketError::kNone;
  if (!ensureWinsock()) {
    lastError_ = SocketError::kOther;
    return false;
  }
  const NativeSocket socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (socket == kInvalidSocket) {
    lastError_ = mapLastError();
    return false;
  }
  if (!setNonBlocking(socket)) {
    lastError_ = mapLastError();
    closeNative(socket);
    return false;
  }
  const Endpoint local{0u, port};
  const sockaddr_in address = toSockaddr(local);
  if (::bind(socket, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
    lastError_ = mapLastError();
    closeNative(socket);
    return false;
  }
  handle_ = static_cast<std::intptr_t>(socket);
  sockaddr_in bound{};
#if defined(_WIN32)
  int length = sizeof(bound);
#else
  socklen_t length = sizeof(bound);
#endif
  if (::getsockname(socket, reinterpret_cast<sockaddr*>(&bound), &length) != 0) {
    lastError_ = mapLastError();
    close();
    return false;
  }
  port_ = ntohs(bound.sin_port);
  return true;
}

int UdpSocket::sendTo(const Endpoint& to, std::span<const uint8_t> bytes) {
  lastError_ = SocketError::kNone;
  if (!isOpen()) {
    lastError_ = SocketError::kClosed;
    return -1;
  }
  const sockaddr_in address = toSockaddr(to);
  const int sent = static_cast<int>(::sendto(toNative(handle_), reinterpret_cast<const char*>(bytes.data()),
                                             static_cast<int>(bytes.size()), 0,
                                             reinterpret_cast<const sockaddr*>(&address),
                                             sizeof(address)));
  if (sent < 0) lastError_ = mapLastError();
  return sent;
}

int UdpSocket::recvFrom(Endpoint& from, std::span<uint8_t> buffer) {
  lastError_ = SocketError::kNone;
  from = Endpoint{};
  if (!isOpen()) {
    lastError_ = SocketError::kClosed;
    return -1;
  }
  sockaddr_in address{};
#if defined(_WIN32)
  int length = sizeof(address);
#else
  socklen_t length = sizeof(address);
#endif
  const int received =
      static_cast<int>(::recvfrom(toNative(handle_), reinterpret_cast<char*>(buffer.data()),
                                  static_cast<int>(buffer.size()), 0,
                                  reinterpret_cast<sockaddr*>(&address), &length));
  if (received < 0) {
    lastError_ = mapLastError();
    return -1;
  }
  from = fromSockaddr(address);
  return received;
}

bool UdpSocket::poll(int timeoutMs) {
  lastError_ = SocketError::kNone;
  if (!isOpen()) {
    lastError_ = SocketError::kClosed;
    return false;
  }
  fd_set readable;
  FD_ZERO(&readable);
  const NativeSocket socket = toNative(handle_);
  FD_SET(socket, &readable);
  timeval timeout{};
  timeout.tv_sec = timeoutMs / 1000;
  timeout.tv_usec = static_cast<long>(timeoutMs % 1000) * 1000L;
  const int ready = ::select(static_cast<int>(socket) + 1, &readable, nullptr, nullptr, &timeout);
  if (ready < 0) {
    lastError_ = mapLastError();
    return false;
  }
  return ready > 0;
}

void UdpSocket::close() {
  if (handle_ < 0) return;
  closeNative(toNative(handle_));
  handle_ = -1;
  port_ = 0u;
}

bool UdpSocket::isOpen() const noexcept { return handle_ >= 0; }

}  // namespace ac::net
