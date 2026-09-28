#include "net/tcp_listener.hpp"

#include <utility>

#include "net/udp_socket.hpp"  // ensureWinsock：UDP 与 TCP 共用同一份 WinSock 初始化

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
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

int lastSocketError() noexcept {
#if defined(_WIN32)
  return ::WSAGetLastError();
#else
  return errno;
#endif
}

// 非阻塞开关：非阻塞 connect 用它切进去，成功后切回阻塞（recv/sendAll 的既有语义不变）。
bool setBlocking(NativeSocket socket, bool isBlocking) noexcept {
#if defined(_WIN32)
  u_long mode = isBlocking ? 0ul : 1ul;
  return ::ioctlsocket(socket, FIONBIO, &mode) == 0;
#else
  const int flags = ::fcntl(socket, F_GETFL, 0);
  if (flags < 0) return false;
  const int updated = isBlocking ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK);
  return ::fcntl(socket, F_SETFL, updated) == 0;
#endif
}

// 「连接还在进行中」的两种平台表述：Windows WSAEWOULDBLOCK/WSAEINPROGRESS，POSIX EINPROGRESS/EALREADY。
bool isConnectInProgress(int error) noexcept {
#if defined(_WIN32)
  return error == WSAEWOULDBLOCK || error == WSAEINPROGRESS || error == WSAEALREADY;
#else
  return error == EINPROGRESS || error == EALREADY || error == EWOULDBLOCK;
#endif
}

// 可写只说明「有结果了」；SO_ERROR 才是 connect 的权威结论（0 = 已建立）。
int pendingSocketError(NativeSocket socket) noexcept {
  int soError = 0;
  SockLen length = static_cast<SockLen>(sizeof(soError));
  if (::getsockopt(socket, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&soError), &length) != 0) {
    return lastSocketError();
  }
  return soError;
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
  other.handle_ = TcpListener::kInvalid;
  other.port_ = 0u;
}

TcpListener& TcpListener::operator=(TcpListener&& other) noexcept {
  if (this == &other) return *this;
  close();
  handle_ = other.handle_;
  port_ = other.port_;
  other.handle_ = TcpListener::kInvalid;
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

// 超时语义见 tcp_listener.hpp 的 connectTcp 注释：timeoutMs > 0 走非阻塞 connect + select 可写 +
// SO_ERROR 复核；timeoutMs <= 0 保持阻塞语义。任何失败分支都在返回前关闭 socket（不泄漏 fd/handle）。
TcpConnection connectTcp(std::uint32_t ipv4, std::uint16_t port, int timeoutMs) noexcept {
  const std::intptr_t handle = createStreamSocket();
  if (handle == kInvalidHandle) return TcpConnection{};
  const NativeSocket socket = toNative(handle);
  sockaddr_in remote{};
  remote.sin_family = AF_INET;
  remote.sin_addr.s_addr = htonl(ipv4);
  remote.sin_port = htons(port);

  if (timeoutMs <= 0) {
    if (::connect(socket, reinterpret_cast<const sockaddr*>(&remote), sizeof(remote)) != 0) {
      closeNative(socket);
      return TcpConnection{};
    }
  } else {
    if (!setBlocking(socket, false)) {
      closeNative(socket);
      return TcpConnection{};
    }
    if (::connect(socket, reinterpret_cast<const sockaddr*>(&remote), sizeof(remote)) != 0) {
      if (!isConnectInProgress(lastSocketError())) {  // 立刻失败（拒绝/无路由）：不必等
        closeNative(socket);
        return TcpConnection{};
      }
      if (!isReady(socket, true, timeoutMs)) {  // 到点仍不可写（或 select 出错）：按超时失败
        closeNative(socket);
        return TcpConnection{};
      }
      if (pendingSocketError(socket) != 0) {  // 可写但被拒：SO_ERROR 非 0
        closeNative(socket);
        return TcpConnection{};
      }
    }
    if (!setBlocking(socket, true)) {
      closeNative(socket);
      return TcpConnection{};
    }
  }
  setNoDelay(socket);
  return TcpConnection{handle, ipv4};
}

}  // namespace ac::net
