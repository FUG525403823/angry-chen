#pragma once
// 一次请求一条连接（HTTP/1.1 + connection: close），定长非阻塞连接表。
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>

#include "http/server.hpp"
#include "net/tcp_listener.hpp"

namespace ac::http {

inline constexpr std::size_t kMaxRequestBytes = 8192u;
inline constexpr int kRequestTimeoutMs = 2000;
inline constexpr int kAcceptBacklog = 32;
inline constexpr std::size_t kMaxHttpConnections = 32u;
inline constexpr std::size_t kAcceptsPerPoll = 8u;
inline constexpr std::size_t kRequestReadChunkBytes = 8192u;
inline constexpr std::size_t kResponseWriteChunkBytes = 8192u;
inline constexpr std::size_t kClientIdBufferBytes = 24u;

struct HttpListenerDeps {
  void* user = nullptr;
  void (*fill)(void* user, HttpDeps& out) = nullptr;
};

struct HttpListenerOptions {
  std::uint32_t bindIpv4 = 0x7F000001u;
  bool trustLoopbackProxy = false;
};

class HttpListener {
 public:
  HttpListener() = default;
  HttpListener(const HttpListener&) = delete;
  HttpListener& operator=(const HttpListener&) = delete;

  bool start(std::uint16_t port, HttpState* state, const HttpListenerDeps& deps,
             std::string* error, const HttpListenerOptions& options = {});
  // 有界 pump；timeoutMs 仅保留源兼容，始终不等待。返回本轮完成/失败关闭的请求数。
  std::size_t serveOnce(std::uint32_t nowMs, int timeoutMs = 0);
  void stop();

  std::uint16_t boundPort() const noexcept { return listener_.boundPort(); }
  bool isOpen() const noexcept { return listener_.isOpen(); }
  std::size_t requestCount() const noexcept { return requestCount_; }
  int lastStatus() const noexcept { return lastStatus_; }
  std::string_view lastClientId() const noexcept { return lastClientId_; }
  bool hasOversizedRequest() const noexcept { return hasOversizedRequest_; }

 private:
  struct Connection {
    ac::net::TcpConnection socket{};
    std::string head;
    std::string output;
    std::string clientId;
    std::size_t sent = 0u;
    std::uint32_t startedMs = 0u;
    std::chrono::steady_clock::time_point startedAt{};
    int status = 0;
  };
  void finish(Connection& connection, int status);
  ac::net::TcpListener listener_{};
  std::array<Connection, kMaxHttpConnections> connections_{};
  HttpState* state_ = nullptr;
  HttpListenerDeps deps_{};
  HttpListenerOptions options_{};
  std::size_t requestCount_ = 0u;
  int lastStatus_ = 0;
  std::string lastClientId_{};
  bool hasOversizedRequest_ = false;
};

std::string_view statusText(int status) noexcept;
std::string buildResponseHead(const Response& response);

}  // namespace ac::http
