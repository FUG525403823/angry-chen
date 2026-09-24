#include "net/tcp_listener.hpp"

#include <utility>

#include "net/udp_socket.hpp"  // ensureWinsock：UDP 与 TCP 共用同一份 WinSock 初始化

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace ac::net {
namespace {

#if defined(_WIN32)
using NativeSocket = SOCKET;
constexpr NativeSocket kInvalidSocket = INVALID_SOCKET;
constexpr std::intptr_t kInvalidHandle = -1;
using SockLen = int;
#else
using NativeSocket = int;
constexpr NativeSocket kInvalidSocket = -1;
constexpr std::intptr_t kInvalidHandle = -1;
using SockLen = socklen_t;
#endif

NativeSocket toNative(std::intptr_t handle) noexcept { return static_cast<NativeSocket>(handle); }
std::intptr_t toHandle(NativeSocket socket) noexcept { return static_cast<std::intptr_t>(socket); }

void closeNative(NativeSocket socket) noexcept {
#if defined(_WIN32)
  ::closesocket(socket);
#else
  ::close(socket);
#endif
}

void setNoDelay(NativeSocket socket) noexcept {
  int yes = 1;
  ::setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&yes),
               static_cast<SockLen>(sizeof(yes)));
}

// 可读/可写等待：timeoutMs < 0 视为 0（本实现不做无限等待）。
bool isReady(NativeSocket socket, bool isWrite, int timeoutMs) noexcept {
  const int waitMs = timeoutMs < 0 ? 0 : timeoutMs;
  fd_set set;
  FD_ZERO(&set);
  FD_SET(socket, &set);
  timeval tv{};
  tv.tv_sec = waitMs / 1000;
  tv.tv_usec = static_cast<long>((waitMs % 1000) * 1000);
  const int ready = ::select(static_cast<int>(socket) + 1, isWrite ? nullptr : &set,
                             isWrite ? &set : nullptr, nullptr, &tv);
  return ready > 0;
}

std::intptr_t createStreamSocket() noexcept {
  if (!ensureWinsock()) return kInvalidHandle;
  return toHandle(::socket(AF_INET, SOCK_STREAM, 0));
}

}  // namespace

TcpConnection::~TcpConnection() { close(); }

TcpConnection::TcpConnection(TcpConnection&& other) noexcept
    : handle_(other.handle_), peerIpv4_(other.peerIpv4_) {
  other.handle_ = TcpConnection::kInvalid;
  other.peerIpv4_ = 0u;
}

TcpConnection& TcpConnection::operator=(TcpConnection&& other) noexcept {
  if (this == &other) return *this;
  close();
  handle_ = other.handle_;
  peerIpv4_ = other.peerIpv4_;
  other.handle_ = TcpConnection::kInvalid;
  other.peerIpv4_ = 0u;
  return *this;
}

int TcpConnection::recv(std::span<std::uint8_t> buffer, int timeoutMs) {
  if (!isOpen() || buffer.empty()) return -1;
  const NativeSocket socket = toNative(handle_);
  if (timeoutMs >= 0 && !isReady(socket, false, timeoutMs)) return -1;
  const int got = static_cast<int>(
      ::recv(socket, reinterpret_cast<char*>(buffer.data()), static_cast<int>(buffer.size()), 0));
  return got;
}

bool TcpConnection::sendAll(std::span<const std::uint8_t> bytes) noexcept {
  return sendAll(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

bool TcpConnection::sendAll(const char* text, std::size_t size) noexcept {
  if (!isOpen()) return false;
  const NativeSocket socket = toNative(handle_);
  std::size_t sent = 0u;
  while (sent < size) {
    if (!isReady(socket, true, 2000)) return false;
    const int written =
        static_cast<int>(::send(socket, text + sent, static_cast<int>(size - sent), 0));
    if (written <= 0) return false;
    sent += static_cast<std::size_t>(written);
  }
  return true;
}

void TcpConnection::close() noexcept {
  if (handle_ == kInvalid) return;
  closeNative(toNative(handle_));
  handle_ = kInvalid;
}

TcpListener::~TcpListener() { close(); }

TcpListener::TcpListener(TcpListener&& other) noexcept : handle_(other.handle_), port_(other.port_) {
  other.handle_ = std::intptr_t{-1};
  other.port_ = 0u;
}

TcpListener& TcpListener::operator=(TcpListener&& other) noexcept {
  if (this == &other) return *this;
  close();
  handle_ = other.handle_;
  port_ = other.port_;
  other.handle_ = std::intptr_t{-1};
  other.port_ = 0u;
  return *this;
}

bool TcpListener::bind(std::uint16_t port) noexcept {
  close();
  handle_ = createStreamSocket();
  if (handle_ == kInvalidHandle) return false;
  const NativeSocket socket = toNative(handle_);
  int yes = 1;
  ::setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes),
               static_cast<SockLen>(sizeof(yes)));
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_ANY);
  address.sin_port = htons(port);
  if (::bind(socket, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
    close();
    return false;
  }
  sockaddr_in local{};
  SockLen length = static_cast<SockLen>(sizeof(local));
  if (::getsockname(socket, reinterpret_cast<sockaddr*>(&local), &length) == 0) {
    port_ = ntohs(local.sin_port);
  } else {
    port_ = port;
  }
  return true;
}

bool TcpListener::listen(int backlog) noexcept {
  if (!isOpen()) return false;
  return ::listen(toNative(handle_), backlog) == 0;
}

bool TcpListener::poll(int timeoutMs) const noexcept {
  if (!isOpen()) return false;
  return isReady(toNative(handle_), false, timeoutMs);
}

TcpConnection TcpListener::accept() noexcept {
  if (!isOpen()) return TcpConnection{};
  const NativeSocket socket = toNative(handle_);
  if (!isReady(socket, false, 0)) return TcpConnection{};
  sockaddr_in remote{};
  SockLen length = static_cast<SockLen>(sizeof(remote));
  const NativeSocket accepted =
      ::accept(socket, reinterpret_cast<sockaddr*>(&remote), &length);
  if (accepted == kInvalidSocket) return TcpConnection{};
  setNoDelay(accepted);
  TcpConnection connection{toHandle(accepted), ntohl(remote.sin_addr.s_addr)};
  return connection;
}

void TcpListener::close() noexcept {
  if (handle_ == kInvalidHandle) return;
  closeNative(toNative(handle_));
  handle_ = kInvalidHandle;
  port_ = 0u;
}

TcpConnection connectTcp(std::uint32_t ipv4, std::uint16_t port, int timeoutMs) noexcept {
  const std::intptr_t handle = createStreamSocket();
  if (handle == kInvalidHandle) return TcpConnection{};
  const NativeSocket socket = toNative(handle);
  sockaddr_in remote{};
  remote.sin_family = AF_INET;
  remote.sin_addr.s_addr = htonl(ipv4);
  remote.sin_port = htons(port);
  if (::connect(socket, reinterpret_cast<const sockaddr*>(&remote), sizeof(remote)) != 0) {
    closeNative(socket);
    return TcpConnection{};
  }
  setNoDelay(socket);
  if (timeoutMs > 0) {
    // 连接建立后立刻可写；这里只做一次就绪探测，避免调用方误以为超时保证。
    isReady(socket, true, timeoutMs);
  }
  TcpConnection connection{handle, ipv4};
  return connection;
}

}  // namespace ac::net
