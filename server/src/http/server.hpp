#pragma once
// S13 §5「HTTP 端点与状态码」：纯请求 -> 响应处理（不碰套接字）。
//
// 监听、端口 8787 与进程生命周期由 S14/S15 的执行批次接线；本模块只冻结四件事：
// 端点表与状态码、读限流（30 次/分钟/IP 滑动窗口 + 429 的 retry-after）、60 s 读缓存（键
// path:limit，写入新对局或触发淘汰即失效）、以及 429/缓存命中两个计数的接线点。
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "metrics/counters.hpp"
#include "metrics/metrics.hpp"
#include "persist/match_store.hpp"

namespace ac::http {

inline constexpr std::size_t kReadRequestsPerMinute = 30u;
inline constexpr std::uint32_t kReadWindowMs = 60000u;
inline constexpr std::uint32_t kCacheTtlMs = 60000u;
inline constexpr std::size_t kMaxLimit = 100u;
inline constexpr std::size_t kDefaultLeaderboardLimit = 20u;
inline constexpr std::size_t kDefaultRecentLimit = 10u;
inline constexpr std::size_t kMaxTrackedClients = 64u;
inline constexpr std::size_t kMaxCacheEntries = 32u;
// HTTP 响应体上限。数值与 net/keepalive.hpp 的 kOutboundBacklogBytes 相同但语义无关（别当同源）。
inline constexpr std::size_t kMaxBodyBytes = 65536u;
inline constexpr std::string_view kJsonContentType = "application/json; charset=utf-8";
inline constexpr std::string_view kRetryAfterHeader = "retry-after";
inline constexpr std::string_view kRetryAfterSeconds = "60";
inline constexpr std::string_view kHealthPath = "/health";
inline constexpr std::string_view kMetricsPath = "/metrics";
inline constexpr std::string_view kLeaderboardPath = "/api/leaderboard";
inline constexpr std::string_view kRecentPath = "/api/matches/recent";

struct Request {
  std::string_view method;    // 只接受 "GET"
  std::string_view path;      // 不含查询串
  std::string_view query;     // 例如 "limit=5"
  std::string_view clientId;  // 记账用标识（生产是 IP）；空串按 "local" 计
};

struct Response {
  int status = 200;
  std::string_view contentType = kJsonContentType;
  std::string body;
  std::string_view extraHeaderName;
  std::string_view extraHeaderValue;
};

// /health 与 /metrics 共用同一份进程快照：调用方只填一次，两个端点不会互相矛盾。
struct HealthSnapshot {
  ac::metrics::ProcessSnapshot process{};
  std::uint64_t ticks = 0u;  // 只有 /health 报的累计 tick（/metrics 没有对应名字）
};

struct HttpDeps {
  const ac::persist::MatchStore* store = nullptr;  // nullptr 或未加载完成 -> 503
  std::string_view metricsBody;                    // metrics::renderMetrics 的产物；空 -> 500
  HealthSnapshot health{};
  ac::metrics::CounterRegistry* counters = nullptr;  // 429 与缓存命中在这里自增
};

// 限流窗口与读缓存的定长状态（生产一个进程一份；测试可各自一份）。
struct HttpState {
  struct ClientWindow {
    bool isUsed = false;
    std::string id;
    std::uint32_t stamps[kReadRequestsPerMinute] = {};
    std::uint32_t next = 0u;
    std::uint32_t count = 0u;
    std::uint32_t lastSeenMs = 0u;
  };
  struct CacheEntry {
    bool isUsed = false;
    std::string key;
    std::uint64_t version = 0u;
    std::uint32_t storedAtMs = 0u;
    std::string body;
  };
  ClientWindow clients[kMaxTrackedClients];
  CacheEntry cache[kMaxCacheEntries];
  std::size_t cacheNext = 0u;
};

Response handleRequest(HttpState& state, const HttpDeps& deps, const Request& request,
                       std::uint32_t nowMs);

// limit 解析：缺省/非法/非正 -> fallback，超过上限 -> kMaxLimit。
std::size_t parseLimit(std::string_view query, std::size_t fallback) noexcept;

}  // namespace ac::http
