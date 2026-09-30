#include "http/listener.hpp"

#include <algorithm>
#include <cstdio>
#include <utility>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif

namespace ac::http {
namespace {

std::string formatClientId(std::uint32_t ipv4) {
  char buffer[kClientIdBufferBytes];
  std::snprintf(buffer, sizeof(buffer), "%u.%u.%u.%u", static_cast<unsigned>((ipv4 >> 24) & 0xFFu),
                static_cast<unsigned>((ipv4 >> 16) & 0xFFu), static_cast<unsigned>((ipv4 >> 8) & 0xFFu),
                static_cast<unsigned>(ipv4 & 0xFFu));
  return std::string(buffer);
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
  out.path = target.substr(0u, questionMark);
  out.query = questionMark == std::string_view::npos ? std::string_view{} : target.substr(questionMark + 1u);
  return !out.method.empty() && !out.path.empty();
}

bool normalizeIp(std::string_view value, std::string& result) {
  if (value.empty() || value.size() >= INET6_ADDRSTRLEN ||
      value.find('\0') != std::string_view::npos) return false;
  const std::string input(value);
  char text[INET6_ADDRSTRLEN];
  in_addr ipv4{};
  if (::inet_pton(AF_INET, input.c_str(), &ipv4) == 1) {
    if (::inet_ntop(AF_INET, &ipv4, text, sizeof(text)) == nullptr) return false;
  } else {
    in6_addr ipv6{};
    if (::inet_pton(AF_INET6, input.c_str(), &ipv6) != 1) return false;
    // IPv4-mapped IPv6 与普通 IPv4 共用额度。
    const auto* bytes = reinterpret_cast<const unsigned char*>(&ipv6);
    bool isMapped = bytes[10] == 0xffu && bytes[11] == 0xffu;
    for (std::size_t i = 0; i < 10u; ++i) isMapped = isMapped && bytes[i] == 0u;
    if (isMapped) {
      result = formatClientId((static_cast<std::uint32_t>(bytes[12]) << 24) |
                              (static_cast<std::uint32_t>(bytes[13]) << 16) |
                              (static_cast<std::uint32_t>(bytes[14]) << 8) | bytes[15]);
      return true;
    }
    if (::inet_ntop(AF_INET6, &ipv6, text, sizeof(text)) == nullptr) return false;
  }
  result = text;
  return true;
}

bool trustedClientId(std::string_view head, std::string& clientId) {
  bool found = false;
  std::size_t cursor = head.find("\r\n");
  if (cursor == std::string_view::npos) return false;
  cursor += 2u;
  while (cursor < head.size()) {
    const auto end = head.find("\r\n", cursor);
    if (end == std::string_view::npos) return false;
    const auto line = head.substr(cursor, end - cursor);
    if (line.empty()) return true;  // 缺头保留 socket 来源，允许本机直接监控。
    if (line.front() == ' ' || line.front() == '\t') return false;
    const auto colon = line.find(':');
    if (colon == std::string_view::npos || colon == 0u) return false;
    auto name = std::string(line.substr(0u, colon));
    for (char& c : name) {
      if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
      if (c <= ' ' || c >= 127) return false;
    }
    if (name == "x-real-ip") {
      if (found) return false;
      auto value = line.substr(colon + 1u);
      while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) value.remove_prefix(1u);
      while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) value.remove_suffix(1u);
      if (!normalizeIp(value, clientId)) return false;
      found = true;
    }
    cursor = end + 2u;
  }
  return false;
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
                         std::string* error, const HttpListenerOptions& options) {
  stop();
  state_ = state;
  deps_ = deps;
  options_ = options;
  if (!listener_.bind(port, options.bindIpv4)) {
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

void HttpListener::stop() {
  listener_.close();
  for (auto& connection : connections_) connection = Connection{};
}

void HttpListener::finish(Connection& connection, int status) {
  ++requestCount_;
  lastStatus_ = status;
  lastClientId_ = connection.clientId;
  connection = Connection{};
}

std::size_t HttpListener::serveOnce(std::uint32_t nowMs, int timeoutMs) {
  (void)timeoutMs;
  if (!listener_.isOpen() || state_ == nullptr || deps_.fill == nullptr) return 0u;
  const auto before = requestCount_;
  const auto now = std::chrono::steady_clock::now();
  for (std::size_t i = 0u; i < kAcceptsPerPoll; ++i) {
    auto socket = listener_.accept();
    if (!socket.isOpen()) break;
    auto slot = std::find_if(connections_.begin(), connections_.end(),
                            [](const Connection& c) { return !c.socket.isOpen(); });
    if (slot == connections_.end()) continue;  // 表满即关闭，不等空位。
    slot->clientId = formatClientId(socket.peerIpv4());
    slot->socket = std::move(socket);
    slot->startedMs = nowMs;
    slot->startedAt = now;
  }

  for (auto& connection : connections_) {
    if (!connection.socket.isOpen()) continue;
    // 每个阶段有独立绝对期限；读写进度均不能续期。
    if (static_cast<std::uint32_t>(nowMs - connection.startedMs) >= kRequestTimeoutMs ||
        now - connection.startedAt >= std::chrono::milliseconds(kRequestTimeoutMs)) {
      finish(connection, 500);
      continue;
    }
    if (connection.output.empty()) {
      std::uint8_t buffer[kRequestReadChunkBytes];
      const int got = connection.socket.recvSome(
          std::span(buffer).first(kMaxRequestBytes - connection.head.size()));
      if (got == ac::net::TcpConnection::kWouldBlock) continue;
      if (got <= 0) {
        finish(connection, 500);
        continue;
      }
      connection.head.append(reinterpret_cast<const char*>(buffer), static_cast<std::size_t>(got));
      const bool isComplete = connection.head.find("\r\n\r\n") != std::string::npos;
      if (!isComplete && connection.head.size() < kMaxRequestBytes) continue;
      Request request{};
      Response response;
      const bool isTrustedProxy = options_.trustLoopbackProxy &&
          (connection.socket.peerIpv4() >> 24) == 127u;
      if (!isComplete || !parseRequestLine(connection.head, request) ||
          (isTrustedProxy && !trustedClientId(connection.head, connection.clientId))) {
        hasOversizedRequest_ = hasOversizedRequest_ || connection.head.size() >= kMaxRequestBytes;
        response.status = 500;
        response.body = "{\"ok\":false,\"error\":\"bad-request\"}";
      } else {
        request.clientId = connection.clientId;
        HttpDeps httpDeps{};
        deps_.fill(deps_.user, httpDeps);
        response = handleRequest(*state_, httpDeps, request, nowMs);
      }
      if (response.body.size() > kMaxBodyBytes) {
        response = Response{};
        response.status = 500;
        response.body = "{\"ok\":false,\"error\":\"response-too-large\"}";
      }
      connection.status = response.status;
      connection.output = buildResponseHead(response) + response.body;
      connection.startedMs = nowMs;
      connection.startedAt = std::chrono::steady_clock::now();
    }
    const auto remaining = connection.output.size() - connection.sent;
    const auto bytes = std::span(reinterpret_cast<const std::uint8_t*>(connection.output.data()) +
                                 connection.sent, std::min(remaining, kResponseWriteChunkBytes));
    const int sent = connection.socket.sendSome(bytes);
    if (sent == ac::net::TcpConnection::kWouldBlock) continue;
    if (sent <= 0) {
      finish(connection, 500);
      continue;
    }
    connection.sent += static_cast<std::size_t>(sent);
    if (connection.sent == connection.output.size()) {
      // 仅丢弃当前已到达的数据一次；绝不等对端 drain/EOF。
      std::uint8_t discarded[kMaxBodyBytes];
      (void)connection.socket.recvSome(discarded);
      finish(connection, connection.status);
    }
  }
  return requestCount_ - before;
}

}  // namespace ac::http
