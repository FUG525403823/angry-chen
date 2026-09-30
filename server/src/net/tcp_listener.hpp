#pragma once
// S14 §2-3：非阻塞 TCP 监听（WinSock2 / POSIX 双实现）。
// 只服务 HTTP 面：接受一条连接、读一次请求、写一次响应、随即关闭（connection: close）。
#include <cstddef>
#include <cstdint>
#include <span>

namespace ac::net {

// 已接受的连接。句柄不可拷贝（避免双重 close），可移动。
class TcpConnection {
 public:
  static constexpr std::intptr_t kInvalid = -1;

  TcpConnection() noexcept = default;
  explicit TcpConnection(std::intptr_t handle, std::uint32_t peerIpv4 = 0u) noexcept
      : handle_(handle), peerIpv4_(peerIpv4) {}
  ~TcpConnection();

  TcpConnection(const TcpConnection&) = delete;
  TcpConnection& operator=(const TcpConnection&) = delete;
  TcpConnection(TcpConnection&& other) noexcept;
  TcpConnection& operator=(TcpConnection&& other) noexcept;

  // 返回读到的字节数；0 = 对端关闭；-1 = 未就绪/出错（timeoutMs >= 0 时最多等这么久）。
  int recv(std::span<std::uint8_t> buffer, int timeoutMs);
  // 非阻塞单次 I/O：正数=进度，0=EOF，-1=错误，kWouldBlock=稍后重试。
  static constexpr int kWouldBlock = -2;
  int recvSome(std::span<std::uint8_t> buffer) noexcept;
  int sendSome(std::span<const std::uint8_t> bytes) noexcept;
  bool sendAll(std::span<const std::uint8_t> bytes) noexcept;
  bool sendAll(const char* text, std::size_t size) noexcept;

  std::uint32_t peerIpv4() const noexcept { return peerIpv4_; }  // 主机字节序
  void close() noexcept;
  bool isOpen() const noexcept { return handle_ != kInvalid; }

 private:
  std::intptr_t handle_ = kInvalid;
  std::uint32_t peerIpv4_ = 0u;
};

class TcpListener {
 public:
  static constexpr std::intptr_t kInvalid = -1;  // 移动后置位用同一个具名常量（D4：消灭裸 -1）

  TcpListener() noexcept = default;
  ~TcpListener();

  TcpListener(const TcpListener&) = delete;
  TcpListener& operator=(const TcpListener&) = delete;
  TcpListener(TcpListener&& other) noexcept;
  TcpListener& operator=(TcpListener&& other) noexcept;

  bool bind(std::uint16_t port, std::uint32_t ipv4 = 0u) noexcept;  // 主机字节序；端口0=自动分配
  bool listen(int backlog = 32) noexcept;
  bool poll(int timeoutMs) const noexcept;  // 有可接受的连接
  TcpConnection accept() noexcept;          // 失败返回未打开的连接
  std::uint16_t boundPort() const noexcept { return port_; }
  void close() noexcept;
  bool isOpen() const noexcept { return handle_ != kInvalid; }

 private:
  std::intptr_t handle_ = kInvalid;
  std::uint16_t port_ = 0u;
};

// 客户端连接（用例、门禁与机器人用）：失败返回未打开的连接（handle 已关闭，不泄漏）。
//
// 超时契约（S15 §15.4 D1 修订）：
//   timeoutMs > 0 —— 真实的连接超时。实现走「非阻塞 connect + 等待可写」：
//     POSIX  `connect` 返回 -1 且 errno == EINPROGRESS/EALREADY，
//     Windows `connect` 返回 SOCKET_ERROR 且 WSAGetLastError() == WSAEWOULDBLOCK/WSAEINPROGRESS；
//     随后 `select` 等可写（POSIX 亦可用 poll，本实现统一用 select），到点即失败并关闭 socket。
//     可写**不等于**连接成功：还必须 `getsockopt(SO_ERROR)` 为 0（被拒/不可达在这一步暴露）。
//   timeoutMs <= 0 —— 阻塞 connect，无超时保证（沿用旧调用方的语义，只有回环这类必然立刻返回的场景可用）。
// 连接成功后恢复阻塞模式，后续 recv/sendAll 的语义与旧实现一致。
TcpConnection connectTcp(std::uint32_t ipv4, std::uint16_t port, int timeoutMs) noexcept;

}  // namespace ac::net
