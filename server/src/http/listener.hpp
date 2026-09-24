#pragma once
// S14 §2-3 / S15 §5：把 S13 的纯处理层 handleRequest 接到真实 TCP 监听上。
// 一次请求一条连接（HTTP/1.1 + connection: close），无并发、无 keep-alive。
#include <cstddef>
#include <cstdint>
#include <string>

#include "http/server.hpp"
#include "net/tcp_listener.hpp"

namespace ac::http {

inline constexpr std::size_t kMaxRequestBytes = 8192u;  // 请求行 + 头，超出即 500 并断开
inline constexpr int kRequestTimeoutMs = 2000;
inline constexpr int kAcceptBacklog = 32;

// 每个请求前刷新一次依赖：metrics 文本与进程快照必须现渲染（缓存由 handleRequest 自己管）。
struct HttpListenerDeps {
  void* user = nullptr;
  void (*fill)(void* user, HttpDeps& out) = nullptr;
};

class HttpListener {
 public:
  HttpListener() = default;
  HttpListener(const HttpListener&) = delete;
  HttpListener& operator=(const HttpListener&) = delete;

  // state 由调用方持有（生命周期须覆盖本对象）；失败原因写进 *error。
  bool start(std::uint16_t port, HttpState* state, const HttpListenerDeps& deps, std::string* error);
  // 最多处理一条连接；返回本次处理的请求数（timeoutMs = 0 时不等待）。
  std::size_t serveOnce(std::uint32_t nowMs, int timeoutMs = 0);
  void stop();

  std::uint16_t boundPort() const noexcept { return listener_.boundPort(); }
  bool isOpen() const noexcept { return listener_.isOpen(); }
  std::size_t requestCount() const noexcept { return requestCount_; }
  int lastStatus() const noexcept { return lastStatus_; }
  std::string_view lastClientId() const noexcept { return lastClientId_; }
  bool hasOversizedRequest() const noexcept { return hasOversizedRequest_; }

 private:
  ac::net::TcpListener listener_{};
  HttpState* state_ = nullptr;
  HttpListenerDeps deps_{};
  std::string clientId_{};
  std::size_t requestCount_ = 0u;
  int lastStatus_ = 0;
  std::string lastClientId_{};
  bool hasOversizedRequest_ = false;
};

// 状态码 -> reason phrase（只覆盖 §5 冻结的 200/404/429/500/503）。
std::string_view statusText(int status) noexcept;

// 组装响应头（含 content-length 与 connection: close，可选 retry-after）。
std::string buildResponseHead(const Response& response);

}  // namespace ac::http
