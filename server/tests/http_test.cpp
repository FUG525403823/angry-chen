// S13 §5 的 HTTP 契约：四个端点与状态码、limit 口径、读限流（30 次/分钟滑动窗口 + retry-after）、
// 60 s 读缓存与版本失效、429/缓存命中两个计数、以及 /metrics 的名字表（45 + S12 移交的 1 条）。
#include "tiny_test.hpp"
#include "test_io.hpp"
#include "tmp_workdir.hpp"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "http/server.hpp"
#include "metrics/metrics.hpp"
#include "persist/match_store.hpp"

namespace {

struct Bench {
  explicit Bench(const char* tag) : dir(tag) {
    std::string error;
    store = ac::persist::openMatchStore(dir.file("data"), &error);
    // /health 与 /metrics 共用这一份快照（填一次，两端点同源）。
    process.protocol = 1u;
    process.rooms = 2u;
    process.connections = 5u;
    process.players = 4u;
    process.graceActive = 1u;
    process.recordsRetained = 3u;
    process.uptimeSeconds = 42u;
    metricsBody = ac::metrics::renderMetrics({&counters, &gauges, process});
    deps.store = store.get();
    deps.metricsBody = metricsBody;
    deps.counters = &counters;
    deps.health.process = process;
    deps.health.ticks = 1200u;
  }

  ac::http::Response get(std::string_view path, std::string_view query, std::uint32_t nowMs,
                         const char* client = "10.0.0.1") {
    const ac::http::Request request{"GET", path, query, client};
    return ac::http::handleRequest(state, deps, request, nowMs);
  }

  ac::test::TempDir dir;
  std::unique_ptr<ac::persist::MatchStore> store;
  ac::metrics::CounterRegistry counters{};
  ac::metrics::GaugeRegistry gauges{};
  ac::metrics::ProcessSnapshot process{};
  std::string metricsBody;
  ac::http::HttpDeps deps{};
  ac::http::HttpState state{};
};

std::size_t countMetricLines(const std::string& body) {
  std::size_t count = 0u;
  std::size_t at = 0u;
  while (at < body.size()) {
    const std::size_t end = body.find('\n', at);
    const std::string_view line(body.data() + at, (end == std::string::npos ? body.size() : end) - at);
    if (line.rfind("ac_", 0) == 0) ++count;
    if (end == std::string::npos) break;
    at = end + 1u;
  }
  return count;
}

}  // namespace

AC_TEST(http_health_reports_runtime_numbers) {
  Bench bench("http");
  AC_CHECK(bench.store != nullptr);
  const ac::http::Response response = bench.get("/health", "", 1000u);
  AC_CHECK_EQ(response.status, 200);
  AC_CHECK(response.contentType == ac::http::kJsonContentType);
  AC_CHECK(response.body.find("\"status\":\"ok\"") != std::string::npos);
  AC_CHECK(response.body.find("\"protocolVersion\":1") != std::string::npos);
  AC_CHECK(response.body.find("\"rooms\":2") != std::string::npos);
  AC_CHECK(response.body.find("\"connections\":5") != std::string::npos);
  AC_CHECK(response.body.find("\"players\":4") != std::string::npos);
  AC_CHECK(response.body.find("\"graceActive\":1") != std::string::npos);
  AC_CHECK(response.body.find("\"recordsRetained\":3") != std::string::npos);
  AC_CHECK(response.body.find("\"uptimeSeconds\":42") != std::string::npos);
  AC_CHECK(response.body.find("\"ticks\":1200") != std::string::npos);
  // 同源断言：同一份快照在 /health 与 /metrics 两侧给出同一个数。
  AC_CHECK(bench.deps.metricsBody.find("ac_rooms 2") != std::string::npos);
  AC_CHECK(bench.deps.metricsBody.find("ac_connections 5") != std::string::npos);
  AC_CHECK(bench.deps.metricsBody.find("ac_players 4") != std::string::npos);
  AC_CHECK(bench.deps.metricsBody.find("ac_grace_active 1") != std::string::npos);
  AC_CHECK(bench.deps.metricsBody.find("ac_records_retained 3") != std::string::npos);
  AC_CHECK(bench.deps.metricsBody.find("ac_uptime_seconds 42") != std::string::npos);
}

AC_TEST(http_metrics_body_is_served_with_frozen_content_type) {
  Bench bench("http");
  const ac::http::Response response = bench.get("/metrics", "", 1000u);
  AC_CHECK_EQ(response.status, 200);
  AC_CHECK(response.contentType == std::string_view("text/plain; version=0.0.4"));
  AC_CHECK_EQ(ac::metrics::metricNameCount(), static_cast<std::size_t>(46));
  AC_CHECK_EQ(countMetricLines(response.body), ac::metrics::metricNameCount());
  AC_CHECK(ac::metrics::isMetricNameRegistered("ac_tick_skips_total"));  // S12 §13.1-2 的移交项
  for (std::size_t i = 0u; i < ac::metrics::metricNameCount(); ++i) {
    AC_CHECK(ac::metrics::isMetricNameRegistered(ac::metrics::metricName(i)));
    AC_CHECK(response.body.find(std::string(ac::metrics::metricName(i))) != std::string::npos);
  }
  AC_CHECK(response.body.find("# TYPE ac_rooms gauge") != std::string::npos);
  AC_CHECK(response.body.find("# TYPE ac_bytes_out_total counter") != std::string::npos);
  AC_CHECK(response.body.find("ac_server_version{version=\"0.1.0\",protocol=\"1\",tick_ms=\"50\"} 1") !=
           std::string::npos);
}

AC_TEST(http_leaderboard_uses_top_order) {
  Bench bench("http");
  AC_CHECK(bench.store != nullptr);
  if (bench.store == nullptr) return;
  AC_CHECK(bench.store->append(ac::test::makeMatchRecord("m-low", 1000u, 1u)));
  AC_CHECK(bench.store->append(ac::test::makeMatchRecord("m-top", 2000u, 9u)));
  const ac::http::Response response = bench.get("/api/leaderboard", "limit=5", 1000u);
  AC_CHECK_EQ(response.status, 200);
  AC_CHECK(response.body.find("\"ok\":true") != std::string::npos);
  const std::size_t first = response.body.find("m-top");
  const std::size_t second = response.body.find("m-low");
  AC_CHECK(first != std::string::npos && second != std::string::npos);
  AC_CHECK(first < second);
}

AC_TEST(http_recent_entries_use_recent_order) {
  Bench bench("http");
  AC_CHECK(bench.store != nullptr);
  if (bench.store == nullptr) return;
  AC_CHECK(bench.store->append(ac::test::makeMatchRecord("m-old", 1000u, 9u)));
  AC_CHECK(bench.store->append(ac::test::makeMatchRecord("m-new", 2000u, 1u)));
  const ac::http::Response response = bench.get("/api/matches/recent", "limit=5", 1000u);
  AC_CHECK_EQ(response.status, 200);
  const std::size_t first = response.body.find("m-new");
  const std::size_t second = response.body.find("m-old");
  AC_CHECK(first != std::string::npos && second != std::string::npos);
  AC_CHECK(first < second);
}

AC_TEST(http_limit_defaults_and_upper_bound) {
  AC_CHECK_EQ(ac::http::parseLimit("", 20u), static_cast<std::size_t>(20));
  AC_CHECK_EQ(ac::http::parseLimit("limit=5", 20u), static_cast<std::size_t>(5));
  AC_CHECK_EQ(ac::http::parseLimit("limit=0", 20u), static_cast<std::size_t>(20));
  AC_CHECK_EQ(ac::http::parseLimit("limit=abc", 20u), static_cast<std::size_t>(20));
  AC_CHECK_EQ(ac::http::parseLimit("limit=999", 10u), ac::http::kMaxLimit);
  AC_CHECK_EQ(ac::http::parseLimit("limit=99999999999999999999", 10u), ac::http::kMaxLimit);
  AC_CHECK_EQ(ac::http::kDefaultLeaderboardLimit, static_cast<std::size_t>(20));
  AC_CHECK_EQ(ac::http::kDefaultRecentLimit, static_cast<std::size_t>(10));
}

AC_TEST(http_unknown_path_is_not_found_with_path_echo) {
  Bench bench("http");
  const ac::http::Response response = bench.get("/nope", "", 1000u);
  AC_CHECK_EQ(response.status, 404);
  AC_CHECK(response.body.find("\"error\":\"not-found\"") != std::string::npos);
  AC_CHECK(response.body.find("\"path\":\"/nope\"") != std::string::npos);
}

AC_TEST(http_store_not_ready_returns_503) {
  Bench bench("http");
  bench.deps.store = nullptr;
  const ac::http::Response response = bench.get("/api/leaderboard", "", 1000u);
  AC_CHECK_EQ(response.status, 503);
  AC_CHECK(response.body.find("\"error\":\"store-not-ready\"") != std::string::npos);
}

AC_TEST(http_metrics_body_missing_returns_500) {
  Bench bench("http");
  bench.deps.metricsBody = std::string_view{};
  const ac::http::Response response = bench.get("/metrics", "", 1000u);
  AC_CHECK_EQ(response.status, 500);
  AC_CHECK(response.body.find("\"error\":\"metrics-unavailable\"") != std::string::npos);
}

AC_TEST(http_rate_limit_blocks_thirty_first_request) {
  Bench bench("http");
  for (std::size_t i = 0u; i < ac::http::kReadRequestsPerMinute; ++i) {
    const ac::http::Response response = bench.get("/health", "", static_cast<std::uint32_t>(1000u + i));
    AC_CHECK_EQ(response.status, 200);
  }
  const ac::http::Response blocked = bench.get("/health", "", 1030u);
  AC_CHECK_EQ(blocked.status, 429);
  AC_CHECK(blocked.extraHeaderName == ac::http::kRetryAfterHeader);
  AC_CHECK(blocked.extraHeaderValue == std::string_view("60"));
  AC_CHECK_EQ(ac::metrics::counterValue(bench.counters, ac::metrics::CounterId::kHttpRateLimited),
              static_cast<std::uint64_t>(1));
  // 另一个客户端不受影响
  const ac::http::Response other = bench.get("/health", "", 1031u, "10.0.0.2");
  AC_CHECK_EQ(other.status, 200);
}

AC_TEST(http_rate_limit_window_slides_after_a_minute) {
  Bench bench("http");
  for (std::size_t i = 0u; i < ac::http::kReadRequestsPerMinute; ++i) {
    AC_CHECK_EQ(bench.get("/health", "", static_cast<std::uint32_t>(1000u + i)).status, 200);
  }
  AC_CHECK_EQ(bench.get("/health", "", 1030u).status, 429);
  AC_CHECK_EQ(bench.get("/health", "", 1000u + ac::http::kReadWindowMs + 1u).status, 200);
  AC_CHECK_EQ(ac::metrics::counterValue(bench.counters, ac::metrics::CounterId::kHttpRateLimited),
              static_cast<std::uint64_t>(1));
}

AC_TEST(http_cache_hits_avoid_second_store_query) {
  Bench bench("http");
  AC_CHECK(bench.store != nullptr);
  if (bench.store == nullptr) return;
  AC_CHECK(bench.store->append(ac::test::makeMatchRecord("m-1", 1000u, 1u)));
  const ac::http::Response first = bench.get("/api/leaderboard", "limit=5", 1000u);
  AC_CHECK_EQ(first.status, 200);
  AC_CHECK_EQ(ac::metrics::counterValue(bench.counters, ac::metrics::CounterId::kHttpCacheHits),
              static_cast<std::uint64_t>(0));
  const ac::http::Response second = bench.get("/api/leaderboard", "limit=5", 2000u);
  AC_CHECK_EQ(second.status, 200);
  AC_CHECK_EQ(second.body, first.body);
  AC_CHECK_EQ(ac::metrics::counterValue(bench.counters, ac::metrics::CounterId::kHttpCacheHits),
              static_cast<std::uint64_t>(1));
  // 键带 limit：换一个 limit 是另一条缓存，不命中
  const ac::http::Response third = bench.get("/api/leaderboard", "limit=7", 2001u);
  AC_CHECK_EQ(third.status, 200);
  AC_CHECK_EQ(ac::metrics::counterValue(bench.counters, ac::metrics::CounterId::kHttpCacheHits),
              static_cast<std::uint64_t>(1));
}

AC_TEST(http_cache_invalidated_by_new_record) {
  Bench bench("http");
  AC_CHECK(bench.store != nullptr);
  if (bench.store == nullptr) return;
  AC_CHECK(bench.store->append(ac::test::makeMatchRecord("m-1", 1000u, 1u)));
  const ac::http::Response first = bench.get("/api/leaderboard", "limit=5", 1000u);
  AC_CHECK_EQ(first.status, 200);
  AC_CHECK(first.body.find("m-2") == std::string::npos);
  AC_CHECK(bench.store->append(ac::test::makeMatchRecord("m-2", 2000u, 9u)));
  const ac::http::Response after = bench.get("/api/leaderboard", "limit=5", 1001u);
  AC_CHECK_EQ(after.status, 200);
  AC_CHECK(after.body.find("m-2") != std::string::npos);
  AC_CHECK_EQ(ac::metrics::counterValue(bench.counters, ac::metrics::CounterId::kHttpCacheHits),
              static_cast<std::uint64_t>(0));
}

AC_TEST(http_cache_expires_after_ttl) {
  Bench bench("http");
  AC_CHECK(bench.store != nullptr);
  if (bench.store == nullptr) return;
  AC_CHECK(bench.store->append(ac::test::makeMatchRecord("m-1", 1000u, 1u)));
  AC_CHECK_EQ(bench.get("/api/matches/recent", "", 1000u).status, 200);
  AC_CHECK_EQ(bench.get("/api/matches/recent", "", 1000u + ac::http::kCacheTtlMs - 1u).status, 200);
  AC_CHECK_EQ(ac::metrics::counterValue(bench.counters, ac::metrics::CounterId::kHttpCacheHits),
              static_cast<std::uint64_t>(1));
  const ac::http::Response expired = bench.get("/api/matches/recent", "", 1000u + ac::http::kCacheTtlMs + 1u);
  AC_CHECK_EQ(expired.status, 200);
  AC_CHECK_EQ(ac::metrics::counterValue(bench.counters, ac::metrics::CounterId::kHttpCacheHits),
              static_cast<std::uint64_t>(1));
}

AC_TEST(http_non_get_is_rejected_as_not_found) {
  Bench bench("http");
  const ac::http::Request request{"POST", "/health", "", "10.0.0.1"};
  const ac::http::Response response = ac::http::handleRequest(bench.state, bench.deps, request, 1000u);
  AC_CHECK_EQ(response.status, 404);
  AC_CHECK(response.body.find("not-found") != std::string::npos);
}

AC_TEST(http_client_table_evicts_the_least_recent) {
  Bench bench("http");
  for (std::size_t i = 0u; i < ac::http::kMaxTrackedClients + 6u; ++i) {
    char client[32];
    std::snprintf(client, sizeof(client), "10.0.0.%zu", i);
    AC_CHECK_EQ(bench.get("/health", "", static_cast<std::uint32_t>(1000u + i), client).status, 200);
  }
  AC_CHECK_EQ(bench.get("/metrics", "", 5000u, "10.0.0.70").status, 200);
}

AC_TEST(http_cache_invalidated_by_store_eviction) {
  Bench bench("http");
  AC_CHECK(bench.store != nullptr);
  if (bench.store == nullptr) return;
  for (std::size_t i = 0u; i < ac::persist::kMaxRecords + 1u; ++i) {
    char id[32];
    std::snprintf(id, sizeof(id), "m-%05zu", i);
    AC_CHECK(bench.store->append(ac::test::makeMatchRecord(id, 1000u + i, 1u)));
  }
  // 越过上限即批量淘汰 kEvictBatch 条最旧记录（常驻数在上限与上限-批量之间摆动）。
  std::printf("evictedRetained=%zu cap=%zu batch=%zu\n", bench.store->recordCount(),
              ac::persist::kMaxRecords, ac::persist::kEvictBatch);
  AC_CHECK_EQ(bench.store->recordCount(), ac::persist::kMaxRecords + 1u - ac::persist::kEvictBatch);
  const std::uint64_t versionBefore = bench.store->version();
  AC_CHECK_EQ(bench.get("/api/matches/recent", "limit=5", 1000u).status, 200);
  AC_CHECK_EQ(bench.get("/api/matches/recent", "limit=5", 1001u).status, 200);
  AC_CHECK_EQ(ac::metrics::counterValue(bench.counters, ac::metrics::CounterId::kHttpCacheHits),
              static_cast<std::uint64_t>(1));
  // 再追加一条即越过常驻上限 → 批量淘汰改变 store 版本 → 同一路径的读缓存必须失效
  AC_CHECK(bench.store->append(ac::test::makeMatchRecord("m-newest", 999999u, 99u)));
  AC_CHECK(bench.store->version() > versionBefore);
  const ac::http::Response after = bench.get("/api/matches/recent", "limit=5", 1002u);
  AC_CHECK_EQ(after.status, 200);
  AC_CHECK(after.body.find("m-newest") != std::string::npos);
  AC_CHECK_EQ(ac::metrics::counterValue(bench.counters, ac::metrics::CounterId::kHttpCacheHits),
              static_cast<std::uint64_t>(1));
}
