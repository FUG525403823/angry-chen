// S14 §2-3：HTTP 监听层的端到端用例（真实 TCP 回环，不碰 8787 之外的端口）。
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

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
  explicit Server(const char* tag) : dir(tag) {
    std::string error;
    store = ac::persist::openMatchStore(dir.file("data"), &error);
    process.protocol = 1u;
    process.rooms = 1u;
    process.connections = 1u;
    process.players = 1u;
    metricsBody = ac::metrics::renderMetrics({&counters, &gauges, process});
    ac::http::HttpListenerDeps deps{};
    deps.user = this;
    deps.fill = &Server::fill;
    std::string startError;
    isStarted = listener.start(0u, &state, deps, &startError);
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

  std::size_t serveOnce(std::uint32_t nowMs) { return listener.serveOnce(nowMs, 500); }

  std::string roundTrip(std::string_view target, std::uint32_t nowMs) {
    ac::net::TcpConnection connection = connect(target);
    if (!connection.isOpen()) return std::string();
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

AC_TEST(listener_serves_health_over_real_socket) {
  Server server("listen");
  AC_CHECK(server.isStarted);
  AC_CHECK(server.listener.boundPort() != 0u);
  const std::string text = server.roundTrip("/health", 1000u);
  AC_CHECK(text.rfind("HTTP/1.1 200 OK", 0u) == 0u);
  AC_CHECK(text.find("content-type: application/json") != std::string::npos);
  AC_CHECK(text.find("content-length: ") != std::string::npos);
  AC_CHECK(text.find("connection: close") != std::string::npos);
  AC_CHECK(text.find("\"protocolVersion\":1") != std::string::npos);
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
