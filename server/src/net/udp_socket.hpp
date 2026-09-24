#pragma once
// S04 §5.7 生产侧缝：非阻塞 UDP 套接字（WinSock2 / POSIX 双实现）。
// 错误码统一映射到 SocketError 后再判断（§8 风险表：两套实现的 errno/WSAGetLastError 语义不同）。
#include <cstdint>
#include <span>

namespace ac::net {

struct Endpoint {
  uint32_t ipv4 = 0u;  // 主机字节序，0x7F000001 = 127.0.0.1
  uint16_t port = 0u;

  bool operator==(const Endpoint& other) const noexcept {
    return ipv4 == other.ipv4 && port == other.port;
  }
};

// WinSock 初始化（幂等）：Windows 首次调用执行 WSAStartup，POSIX 恒真。UDP 与 TCP 共用一份。
bool ensureWinsock() noexcept;

enum class SocketError : int {
  kNone = 0,
  kWouldBlock = 1,
  kInterrupted = 2,
  kClosed = 3,
  kOther = 4,
};

class UdpSocket {
 public:
  UdpSocket() noexcept = default;
  ~UdpSocket();

  UdpSocket(const UdpSocket&) = delete;
  UdpSocket& operator=(const UdpSocket&) = delete;
  UdpSocket(UdpSocket&& other) noexcept;
  UdpSocket& operator=(UdpSocket&& other) noexcept;

  bool bind(uint16_t port);  // 0 = 由系统分配，可用 boundPort() 取回
  uint16_t boundPort() const noexcept { return port_; }

  int sendTo(const Endpoint& to, std::span<const uint8_t> bytes);
  int recvFrom(Endpoint& from, std::span<uint8_t> buffer);
  bool poll(int timeoutMs);
  void close();
  bool isOpen() const noexcept;
  SocketError lastError() const noexcept { return lastError_; }

 private:
  std::intptr_t handle_ = -1;
  uint16_t port_ = 0u;
  SocketError lastError_ = SocketError::kNone;
};

}  // namespace ac::net
