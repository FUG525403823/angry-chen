#pragma once
// S14 §2-3：阻塞式 TCP 监听的最小实现（WinSock2 / POSIX 双实现）。
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
  TcpListener() noexcept = default;
  ~TcpListener();

  TcpListener(const TcpListener&) = delete;
  TcpListener& operator=(const TcpListener&) = delete;
  TcpListener(TcpListener&& other) noexcept;
  TcpListener& operator=(TcpListener&& other) noexcept;

  bool bind(std::uint16_t port) noexcept;  // 0 = 由系统分配，用 boundPort() 取回
  bool listen(int backlog = 32) noexcept;
  bool poll(int timeoutMs) const noexcept;  // 有可接受的连接
  TcpConnection accept() noexcept;          // 失败返回未打开的连接
  std::uint16_t boundPort() const noexcept { return port_; }
  void close() noexcept;
  bool isOpen() const noexcept { return handle_ != std::intptr_t{-1}; }

 private:
  std::intptr_t handle_ = -1;
  std::uint16_t port_ = 0u;
};

// 客户端连接（用例与工具用）：失败返回未打开的连接。面向回环地址，不做重试。
TcpConnection connectTcp(std::uint32_t ipv4, std::uint16_t port, int timeoutMs) noexcept;

}  // namespace ac::net
