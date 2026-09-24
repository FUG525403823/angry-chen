#include "http/listener.hpp"

#include <cstdio>

namespace ac::http {
namespace {

constexpr std::uint32_t loopbackIpv4() noexcept { return 0x7F000001u; }

// 响应写不出去时的记账状态（评审 #6：写失败不算送达）。
inline constexpr std::size_t kWriteFailureStatus = 500u;

std::string formatClientId(std::uint32_t ipv4) {
  char buffer[24];
  std::snprintf(buffer, sizeof(buffer), "%u.%u.%u.%u", static_cast<unsigned>((ipv4 >> 24) & 0xFFu),
                static_cast<unsigned>((ipv4 >> 16) & 0xFFu), static_cast<unsigned>((ipv4 >> 8) & 0xFFu),
                static_cast<unsigned>(ipv4 & 0xFFu));
  return std::string(buffer);
}

// 请求头读完（\r\n\r\n）即停；超过上限返回 false。
bool readRequestHead(ac::net::TcpConnection& connection, std::string& out) {
  std::uint8_t buffer[256];
  while (out.find("\r\n\r\n") == std::string::npos) {
    if (out.size() >= kMaxRequestBytes) return false;
    const int got = connection.recv(buffer, kRequestTimeoutMs);
    if (got <= 0) return false;
    out.append(reinterpret_cast<const char*>(buffer), static_cast<std::size_t>(got));
  }
  return true;
}

bool parseRequestLine(const std::string& head, Request& out) {
  const std::size_t lineEnd = head.find("\r\n");
  const std::string_view line(head.data(), lineEnd == std::string::npos ? head.size() : lineEnd);
  const std::size_t firstSpace = line.find(' ');
  if (firstSpace == std::string_view::npos) return false;
  const std::size_t secondSpace = line.find(' ', firstSpace + 1u);
  if (secondSpace == std::string_view::npos) return false;
  out.method = line.substr(0u, firstSpace);
  const std::string_view target = line.substr(firstSpace + 1u, secondSpace - firstSpace - 1u);
  const std::size_t questionMark = target.find('?');
  if (questionMark == std::string_view::npos) {
    out.path = target;
    out.query = std::string_view{};
  } else {
    out.path = target.substr(0u, questionMark);
    out.query = target.substr(questionMark + 1u);
  }
  return !out.method.empty();
}

// 响应写完后再把残留请求读空：带未读数据 close 会让对端收到 RST，
// 客户端就拿不到刚写出的响应（超长请求头这条路径踩过，S14 实测）。
void drainInput(ac::net::TcpConnection& connection) noexcept {
  std::uint8_t scratch[512];
  std::size_t drained = 0u;
  while (drained < 65536u) {
    const int got = connection.recv(scratch, 50);
    if (got <= 0) break;
    drained += static_cast<std::size_t>(got);
  }
}

}  // namespace

std::string_view statusText(int status) noexcept {
  switch (status) {
    case 200: return "OK";
    case 404: return "Not Found";
    case 429: return "Too Many Requests";
    case 500: return "Internal Server Error";
    case 503: return "Service Unavailable";
    default: return "Status";
  }
}

std::string buildResponseHead(const Response& response) {
  std::string head = "HTTP/1.1 ";
  head += std::to_string(response.status);
  head += ' ';
  head += statusText(response.status);
  head += "\r\ncontent-type: ";
  head += response.contentType;
  head += "\r\ncontent-length: ";
  head += std::to_string(response.body.size());
  head += "\r\nconnection: close\r\n";
  if (!response.extraHeaderName.empty() && !response.extraHeaderValue.empty()) {
    head += response.extraHeaderName;
    head += ": ";
    head += response.extraHeaderValue;
    head += "\r\n";
  }
  head += "\r\n";
  return head;
}

bool HttpListener::start(std::uint16_t port, HttpState* state, const HttpListenerDeps& deps,
                         std::string* error) {
  state_ = state;
  deps_ = deps;
  if (!listener_.bind(port)) {
    if (error != nullptr) *error = "bind failed on port " + std::to_string(port);
    return false;
  }
  if (!listener_.listen(kAcceptBacklog)) {
    if (error != nullptr) *error = "listen failed on port " + std::to_string(listener_.boundPort());
    listener_.close();
    return false;
  }
  return true;
}

void HttpListener::stop() { listener_.close(); }

std::size_t HttpListener::serveOnce(std::uint32_t nowMs, int timeoutMs) {
  if (!listener_.isOpen() || state_ == nullptr || deps_.fill == nullptr) return 0u;
  if (!listener_.poll(timeoutMs)) return 0u;
  ac::net::TcpConnection connection = listener_.accept();
  if (!connection.isOpen()) return 0u;
  clientId_ = formatClientId(connection.peerIpv4() == 0u ? loopbackIpv4() : connection.peerIpv4());

  Response response;
  std::string head;
  Request request{};
  if (!readRequestHead(connection, head) || !parseRequestLine(head, request)) {
    // 请求不可解析：按 §5 的 500 出口回一条最小响应（连接照常关闭）。
    response = Response{};
    response.status = 500;
    response.body = "{\"ok\":false,\"error\":\"bad-request\"}";
  } else {
    request.clientId = clientId_;
    HttpDeps httpDeps{};
    deps_.fill(deps_.user, httpDeps);
    response = handleRequest(*state_, httpDeps, request, nowMs);
  }
  if (head.size() >= kMaxRequestBytes) hasOversizedRequest_ = true;

  const std::string responseHead = buildResponseHead(response);
  // §16「失败用返回值」：写不出去的响应不算送达，状态按 5xx 记账（评审 #6）。
  const bool isWritten = connection.sendAll(responseHead.data(), responseHead.size()) &&
                         connection.sendAll(response.body.data(), response.body.size());
  drainInput(connection);
  connection.close();

  ++requestCount_;
  lastStatus_ = isWritten ? response.status : kWriteFailureStatus;
  lastClientId_ = clientId_;
  return 1u;
}

}  // namespace ac::http
