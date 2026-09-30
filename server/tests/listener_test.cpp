// S14 §2-3：HTTP 监听层的端到端用例（真实 TCP 回环，不碰 8787 之外的端口）。
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <thread>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#undef near
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#endif

#include "http/listener.hpp"
#include "http/server.hpp"
#include "metrics/counters.hpp"
#include "metrics/gauges.hpp"
#include "metrics/metrics.hpp"
#include "net/tcp_listener.hpp"
#include "persist/match_store.hpp"
#include "test_io.hpp"
#include "tiny_test.hpp"
#include "tmp_workdir.hpp"

namespace {

constexpr std::uint32_t kLoopback = 0x7F000001u;

struct Server {
  explicit Server(const char* tag, const ac::http::HttpListenerOptions& options = {}) : dir(tag) {
    std::string error;
    store = ac::persist::openMatchStore(dir.file("data"), &error);
    process.protocol = 2u;
    process.rooms = 1u;
    process.connections = 1u;
    process.players = 1u;
    metricsBody = ac::metrics::renderMetrics({&counters, &gauges, process});
    ac::http::HttpListenerDeps deps{};
    deps.user = this;
    deps.fill = &Server::fill;
    std::string startError;
    isStarted = listener.start(0u, &state, deps, &startError, options);
  }

  static void fill(void* user, ac::http::HttpDeps& out) {
    Server& self = *static_cast<Server*>(user);
    out.store = self.store.get();
    out.metricsBody =
        self.isMetricsServed ? std::string_view(self.metricsBody) : std::string_view{};
    out.health.process = self.process;
    out.health.ticks = 1200u;
    out.counters = &self.counters;
  }

  ac::net::TcpConnection connect(std::string_view target) {
    ac::net::TcpConnection connection =
        ac::net::connectTcp(kLoopback, listener.boundPort(), 500);
    if (!connection.isOpen()) return connection;
    std::string request = "GET ";
    request += target;
    request += " HTTP/1.1\r\nhost: 127.0.0.1\r\n\r\n";
    connection.sendAll(request.data(), request.size());
    return connection;
  }

  std::size_t serveOnce(std::uint32_t nowMs) {
    for (int i = 0; i < 100; ++i) {
      const auto completed = listener.serveOnce(nowMs, 0);
      if (completed != 0u || !listener.isOpen()) return completed;
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return 0u;
  }

  std::string roundTrip(std::string_view target, std::uint32_t nowMs) {
    ac::net::TcpConnection connection = connect(target);
    if (!connection.isOpen()) return std::string();
    serveOnce(nowMs);
    return readAll(connection);
  }

  std::string forwardedRequest(std::string_view headers, std::uint32_t nowMs = 1000u) {
    auto connection = ac::net::connectTcp(kLoopback, listener.boundPort(), 500);
    if (!connection.isOpen()) return {};
    std::string request = "GET /api/leaderboard HTTP/1.1\r\nhost: localhost\r\n";
    request += headers;
    request += "\r\n";
    if (!connection.sendAll(request.data(), request.size())) return {};
    serveOnce(nowMs);
    return readAll(connection);
  }

  static std::string readAll(ac::net::TcpConnection& connection) {
    std::string out;
    std::uint8_t buffer[512];
    while (true) {
      const int got = connection.recv(buffer, 500);
      if (got <= 0) break;
      out.append(reinterpret_cast<const char*>(buffer), static_cast<std::size_t>(got));
    }
    return out;
  }

  ac::test::TempDir dir;
  ac::http::HttpState state{};
  ac::metrics::CounterRegistry counters{};
  ac::metrics::GaugeRegistry gauges{};
  ac::metrics::ProcessSnapshot process{};
  std::string metricsBody;
  std::unique_ptr<ac::persist::MatchStore> store;
  ac::http::HttpListener listener{};
  bool isStarted = false;
  bool isMetricsServed = true;
};

}  // namespace

AC_TEST(listener_partial_request_does_not_block_poll) {
  Server server("listen");
  AC_CHECK(server.isStarted);
  auto slow = ac::net::connectTcp(kLoopback, server.listener.boundPort(), 500);
  AC_CHECK(slow.isOpen());
  const std::string partial = "GET /health HTTP/1.1\r\nhost: localhost\r\n";
  AC_CHECK(slow.sendAll(partial.data(), partial.size()));
  const auto started = std::chrono::steady_clock::now();
  const auto completed = server.listener.serveOnce(1000u, 0);
  const auto elapsed = std::chrono::duration<double, std::milli>(
      std::chrono::steady_clock::now() - started).count();
  std::printf("partialRequestPoll elapsed=%.1fms completed=%zu\n", elapsed, completed);
  AC_CHECK(elapsed < 100.0);
  AC_CHECK_EQ(completed, static_cast<std::size_t>(0));
}

AC_TEST(listener_response_has_its_own_absolute_deadline) {
  Server server("listen");
  server.metricsBody.assign(60000u, 'm');
  auto connection = ac::net::connectTcp(kLoopback, server.listener.boundPort(), 500);
  AC_CHECK(connection.isOpen());
  const std::string partial = "GET /metrics HTTP/1.1\r\nhost: localhost\r\n";
  AC_CHECK(connection.sendAll(partial.data(), partial.size()));
  AC_CHECK_EQ(server.listener.serveOnce(1000u), static_cast<std::size_t>(0));
  AC_CHECK(connection.sendAll("\r\n", 2u));
  AC_CHECK_EQ(server.listener.serveOnce(2999u), static_cast<std::size_t>(0));
  AC_CHECK_EQ(server.serveOnce(3001u), static_cast<std::size_t>(1));
  const auto response = Server::readAll(connection);
  const auto bodyAt = response.find("\r\n\r\n");
  AC_CHECK(bodyAt != std::string::npos);
  AC_CHECK_EQ(response.substr(bodyAt + 4u), server.metricsBody);
  AC_CHECK_EQ(server.listener.lastStatus(), 200);
}

AC_TEST(listener_trusted_proxy_has_independent_client_quotas) {
  Server server("listen", {kLoopback, true});
  for (std::uint32_t i = 0u; i < 30u; ++i) {
    AC_CHECK(server.forwardedRequest("X-Real-IP: 198.51.100.1\r\n", 1000u + i)
                 .starts_with("HTTP/1.1 200"));
  }
  AC_CHECK(server.forwardedRequest("X-Real-IP: 198.51.100.1\r\n", 1030u)
               .starts_with("HTTP/1.1 429"));
  AC_CHECK(server.forwardedRequest("x-real-ip: 198.51.100.2\r\n", 1031u)
               .starts_with("HTTP/1.1 200"));
  AC_CHECK_EQ(server.listener.lastClientId(), std::string_view("198.51.100.2"));
}

AC_TEST(listener_proxy_disabled_ignores_forged_headers) {
  Server server("listen");
  for (std::uint32_t i = 0u; i < 30u; ++i) {
    AC_CHECK(server.forwardedRequest("X-Real-IP: 198.51.100." + std::to_string(i + 1u) +
                                        "\r\n", 1000u + i).starts_with("HTTP/1.1 200"));
  }
  AC_CHECK(server.forwardedRequest("X-Real-IP: invalid, spoofed\r\n", 1030u)
               .starts_with("HTTP/1.1 429"));
  AC_CHECK_EQ(server.listener.lastClientId(), std::string_view("127.0.0.1"));
}

AC_TEST(listener_nonloopback_peer_cannot_spoof_proxy_identity) {
  Server server("listen", {0u, true});
  char hostname[256]{};
  AC_CHECK(::gethostname(hostname, sizeof(hostname)) == 0);
  addrinfo hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo* addresses = nullptr;
  AC_CHECK(::getaddrinfo(hostname, nullptr, &hints, &addresses) == 0);
  std::uint32_t localIp = 0u;
  for (const auto* address = addresses; address != nullptr; address = address->ai_next) {
    const auto* ipv4 = reinterpret_cast<const sockaddr_in*>(address->ai_addr);
    const auto candidate = ntohl(ipv4->sin_addr.s_addr);
    if (candidate != 0u && (candidate >> 24) != 127u) { localIp = candidate; break; }
  }
  if (addresses != nullptr) ::freeaddrinfo(addresses);
  if (localIp == 0u) {
    std::printf("nonloopbackProxyIdentity not verified: no local nonloopback IPv4\n");
    return;
  }
  auto connection = ac::net::connectTcp(localIp, server.listener.boundPort(), 500);
  AC_CHECK(connection.isOpen());
  const std::string request = "GET /api/leaderboard HTTP/1.1\r\nhost: localhost\r\n"
                              "X-Real-IP: forged, invalid\r\n\r\n";
  AC_CHECK(connection.sendAll(request.data(), request.size()));
  AC_CHECK_EQ(server.serveOnce(1000u), static_cast<std::size_t>(1));
  AC_CHECK(Server::readAll(connection).starts_with("HTTP/1.1 200"));
  char expected[INET_ADDRSTRLEN]{};
  in_addr address{};
  address.s_addr = htonl(localIp);
  AC_CHECK(::inet_ntop(AF_INET, &address, expected, sizeof(expected)) != nullptr);
  AC_CHECK_EQ(server.listener.lastClientId(), std::string_view(expected));
  std::printf("nonloopbackProxyIdentity verified peer=%s\n", expected);
}

AC_TEST(listener_proxy_mode_allows_direct_local_monitoring_without_header) {
  Server server("listen", {kLoopback, true});
  AC_CHECK(server.roundTrip("/health", 1000u).starts_with("HTTP/1.1 200"));
  AC_CHECK(server.roundTrip("/metrics", 1001u).starts_with("HTTP/1.1 200"));
  AC_CHECK_EQ(server.listener.lastClientId(), std::string_view("127.0.0.1"));
}

AC_TEST(listener_trusted_proxy_rejects_ambiguous_or_invalid_identity) {
  Server server("listen", {kLoopback, true});
  const std::string_view invalid[] = {
      "X-Real-IP: 198.51.100.1\r\nx-real-ip: 198.51.100.2\r\n",
      "X-Real-IP: 198.51.100.1, 198.51.100.2\r\n",
      "X-Real-IP: \r\n", "X-Real-IP: hostname\r\n", "X-Real-IP: 256.0.0.1\r\n",
      "X-Real-IP: 198.51.100.1:80\r\n", "X-Real-IP: [::1]\r\n",
      "X-Real-IP: fe80::1%eth0\r\n", "X-Real-IP: 198.51.100.1\r\n 198.51.100.2\r\n",
      "X-Real-IP : 198.51.100.1\r\n"};
  for (const auto header : invalid)
    AC_CHECK(server.forwardedRequest(header).starts_with("HTTP/1.1 500"));
}

AC_TEST(listener_proxy_normalizes_ipv6_and_ipv4_mapped_identity) {
  Server server("listen", {kLoopback, true});
  AC_CHECK(server.forwardedRequest("X-Real-IP: 2001:0DB8:0000:0000:0000:0000:0000:0001\r\n")
               .starts_with("HTTP/1.1 200"));
  AC_CHECK_EQ(server.listener.lastClientId(), std::string_view("2001:db8::1"));
  for (std::uint32_t i = 0u; i < 30u; ++i)
    AC_CHECK(server.forwardedRequest("X-Real-IP: 198.51.100.1\r\n", 1000u + i)
                 .starts_with("HTTP/1.1 200"));
  AC_CHECK(server.forwardedRequest("X-Real-IP: ::ffff:198.51.100.1\r\n", 1030u)
               .starts_with("HTTP/1.1 429"));
}

AC_TEST(listener_fragmented_request_survives_eagain_while_other_client_completes) {
  Server server("listen");
  auto slow = ac::net::connectTcp(kLoopback, server.listener.boundPort(), 500);
  AC_CHECK(slow.isOpen());
  AC_CHECK(slow.sendAll("GET /health HTTP/1.1\r\n", 22u));
  AC_CHECK_EQ(server.listener.serveOnce(1000u), static_cast<std::size_t>(0));
  AC_CHECK_EQ(server.listener.serveOnce(1001u), static_cast<std::size_t>(0));
  AC_CHECK(server.roundTrip("/health", 1002u).starts_with("HTTP/1.1 200"));
  AC_CHECK(slow.sendAll("\r\n", 2u));
  AC_CHECK_EQ(server.serveOnce(1003u), static_cast<std::size_t>(1));
  AC_CHECK(Server::readAll(slow).starts_with("HTTP/1.1 200"));
}

AC_TEST(listener_read_deadline_is_absolute_despite_progress) {
  Server server("listen");
  auto slow = ac::net::connectTcp(kLoopback, server.listener.boundPort(), 500);
  AC_CHECK(slow.isOpen());
  AC_CHECK(slow.sendAll("G", 1u));
  AC_CHECK_EQ(server.listener.serveOnce(1000u), static_cast<std::size_t>(0));
  AC_CHECK(slow.sendAll("E", 1u));
  AC_CHECK_EQ(server.listener.serveOnce(2999u), static_cast<std::size_t>(0));
  AC_CHECK_EQ(server.listener.serveOnce(3000u), static_cast<std::size_t>(1));
  std::uint8_t buffer[1];
  AC_CHECK_EQ(slow.recv(buffer, 100), 0);
  AC_CHECK_EQ(server.listener.lastStatus(), 500);
}

AC_TEST(listener_early_eof_is_not_treated_as_would_block) {
  Server server("listen");
  auto client = ac::net::connectTcp(kLoopback, server.listener.boundPort(), 500);
  AC_CHECK(client.isOpen());
  AC_CHECK(client.sendAll("G", 1u));
  AC_CHECK_EQ(server.listener.serveOnce(1000u), static_cast<std::size_t>(0));
  client.close();
  AC_CHECK_EQ(server.serveOnce(1001u), static_cast<std::size_t>(1));
  AC_CHECK_EQ(server.listener.lastStatus(), 500);
  AC_CHECK(server.roundTrip("/health", 1002u).starts_with("HTTP/1.1 200"));
}

AC_TEST(listener_response_deadline_does_not_extend_on_write_progress) {
  Server server("listen");
  server.metricsBody.assign(60000u, 'm');
  auto client = server.connect("/metrics");
  AC_CHECK(client.isOpen());
  AC_CHECK_EQ(server.listener.serveOnce(1000u), static_cast<std::size_t>(0));
  AC_CHECK_EQ(server.listener.serveOnce(2999u), static_cast<std::size_t>(0));
  AC_CHECK_EQ(server.listener.serveOnce(3000u), static_cast<std::size_t>(1));
  const auto response = Server::readAll(client);
  const auto bodyAt = response.find("\r\n\r\n");
  AC_CHECK(bodyAt != std::string::npos);
  AC_CHECK(response.size() - bodyAt - 4u < server.metricsBody.size());
  AC_CHECK_EQ(server.listener.lastStatus(), 500);
}

AC_TEST(listener_serves_health_over_real_socket) {
  Server server("listen");
  AC_CHECK(server.isStarted);
  AC_CHECK(server.listener.boundPort() != 0u);
  const std::string text = server.roundTrip("/health", 1000u);
  AC_CHECK(text.rfind("HTTP/1.1 200 OK", 0u) == 0u);
  AC_CHECK(text.find("content-type: application/json") != std::string::npos);
  AC_CHECK(text.find("content-length: ") != std::string::npos);
  AC_CHECK(text.find("connection: close") != std::string::npos);
  AC_CHECK(text.find("\"protocolVersion\":2") != std::string::npos);
  AC_CHECK(text.find("\"rooms\":1") != std::string::npos);
  AC_CHECK(text.find("\"ticks\":1200") != std::string::npos);
  AC_CHECK_EQ(server.listener.lastStatus(), 200);
  AC_CHECK(server.listener.lastClientId() == std::string_view("127.0.0.1"));
  AC_CHECK_EQ(server.listener.requestCount(), static_cast<std::size_t>(1));
}

AC_TEST(listener_returns_not_found_for_unknown_path) {
  Server server("listen");
  const std::string text = server.roundTrip("/nope", 1000u);
  AC_CHECK(text.rfind("HTTP/1.1 404 Not Found", 0u) == 0u);
  AC_CHECK(text.find("\"error\":\"not-found\"") != std::string::npos);
  AC_CHECK_EQ(server.listener.lastStatus(), 404);
}

AC_TEST(listener_returns_500_when_metrics_body_is_missing) {
  Server server("listen");
  server.isMetricsServed = false;
  const std::string text = server.roundTrip("/metrics", 1000u);
  AC_CHECK(text.rfind("HTTP/1.1 500 Internal Server Error", 0u) == 0u);
  AC_CHECK(text.find("metrics-unavailable") != std::string::npos);
  AC_CHECK_EQ(server.listener.lastStatus(), 500);
}

AC_TEST(listener_rate_limits_reads_after_thirty_requests) {
  Server server("listen");
  std::uint32_t now = 1000u;
  for (std::size_t i = 0u; i < 30u; ++i) {
    now += 100u;
    const std::string text = server.roundTrip("/api/leaderboard", now);
    AC_CHECK(text.rfind("HTTP/1.1 200 OK", 0u) == 0u);
  }
  const std::string limited = server.roundTrip("/api/leaderboard", now + 100u);
  AC_CHECK(limited.rfind("HTTP/1.1 429 Too Many Requests", 0u) == 0u);
  AC_CHECK(limited.find("retry-after: 60") != std::string::npos);
  AC_CHECK_EQ(ac::metrics::counterValue(server.counters, ac::metrics::CounterId::kHttpRateLimited),
              static_cast<std::uint64_t>(1));
  // 窗口滑过 60 s 后恢复。
  const std::string recovered = server.roundTrip("/api/leaderboard", now + 61000u);
  AC_CHECK(recovered.rfind("HTTP/1.1 200 OK", 0u) == 0u);
}

AC_TEST(listener_serves_leaderboard_and_recent_over_socket) {
  Server server("listen");
  AC_CHECK(server.store != nullptr);
  if (server.store == nullptr) return;
  AC_CHECK(server.store->append(ac::test::makeMatchRecord("m-1", 2000u, 9u)));
  AC_CHECK(server.store->append(ac::test::makeMatchRecord("m-2", 1000u, 1u)));
  const std::string board = server.roundTrip("/api/leaderboard?limit=1", 1000u);
  AC_CHECK(board.rfind("HTTP/1.1 200 OK", 0u) == 0u);
  AC_CHECK(board.find("\"entries\":[{\"matchId\":\"m-1\"") != std::string::npos);
  AC_CHECK(board.find("m-2") == std::string::npos);
  const std::string recent = server.roundTrip("/api/matches/recent?limit=2", 1100u);
  AC_CHECK(recent.rfind("HTTP/1.1 200 OK", 0u) == 0u);
  AC_CHECK(recent.find("m-2") != std::string::npos);
  // 响应体里不再夹 NDJSON 的行尾换行（S13 评审中-8 的形状修正）。
  const std::size_t bodyAt = board.find("\r\n\r\n");
  AC_CHECK(bodyAt != std::string::npos);
  const std::string body = bodyAt == std::string::npos ? std::string() : board.substr(bodyAt + 4u);
  AC_CHECK(body.find('\n') == std::string::npos);
  AC_CHECK(body.rfind("{\"ok\":true,\"entries\":[{\"matchId\":\"m-1\"", 0u) == 0u);
}

AC_TEST(listener_rejects_giant_request_head) {
  Server server("listen");
  // 只发超长噪声、不发完整请求行（connect() 会先发一条合法请求）。
  ac::net::TcpConnection connection =
      ac::net::connectTcp(kLoopback, server.listener.boundPort(), 500);
  AC_CHECK(connection.isOpen());
  if (!connection.isOpen()) return;
  const std::string filler(9000u, 'a');
  AC_CHECK(connection.sendAll(filler.data(), filler.size()));
  AC_CHECK_EQ(server.serveOnce(1000u), static_cast<std::size_t>(1));
  const std::string text = Server::readAll(connection);
  AC_CHECK(text.rfind("HTTP/1.1 500 Internal Server Error", 0u) == 0u);
  AC_CHECK(server.listener.hasOversizedRequest());
}

AC_TEST(listener_stops_accepting_after_stop) {
  Server server("listen");
  AC_CHECK(server.roundTrip("/health", 1000u).rfind("HTTP/1.1 200 OK", 0u) == 0u);
  server.listener.stop();
  AC_CHECK(!server.listener.isOpen());
  ac::net::TcpConnection connection = server.connect("/health");
  AC_CHECK(!connection.isOpen());
  AC_CHECK_EQ(server.serveOnce(2000u), static_cast<std::size_t>(0));
}

// S15 §15.4 D1：connectTcp 的超时必须真生效。192.0.2.1 是 RFC 5737 的 TEST-NET-1（黑洞地址），
// 不会回 SYN-ACK；若本机直接回不可达也算通过（两条路径都是「快速失败」），关键是**不许挂住**。
// 上限 3×timeout 同时排除了「实现偷偷按系统默认 ~21s 超时返回」。
AC_TEST(listener_connect_timeout_is_enforced) {
  constexpr int kTimeoutMs = 300;
  constexpr std::uint32_t kBlackHole = 0xC0000201u;  // 192.0.2.1
  const auto started = std::chrono::steady_clock::now();
  ac::net::TcpConnection connection = ac::net::connectTcp(kBlackHole, 9u, kTimeoutMs);
  const double elapsedMs = std::chrono::duration<double, std::milli>(
                               std::chrono::steady_clock::now() - started)
                               .count();
  std::printf("blackholeConnect timeout=%dms elapsed=%.1fms open=%d\n", kTimeoutMs, elapsedMs,
              connection.isOpen() ? 1 : 0);
  AC_CHECK(!connection.isOpen());
  AC_CHECK(elapsedMs < 3.0 * static_cast<double>(kTimeoutMs));
  // 失败路径必须不泄漏 fd：连续 64 次超时失败后仍能连上真实监听者。
  for (int i = 0; i < 64; ++i) {
    const ac::net::TcpConnection failed = ac::net::connectTcp(kBlackHole, 9u, 1);
    AC_CHECK(!failed.isOpen());
  }
  Server server("listen");
  AC_CHECK(server.isStarted);
  const std::string text = server.roundTrip("/health", 1000u);
  AC_CHECK(text.rfind("HTTP/1.1 200 OK", 0u) == 0u);
}
