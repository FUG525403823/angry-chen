#include "http/server.hpp"

#include <cstdio>
#include <vector>

#include "core/json_text.hpp"
#include "metrics/metrics.hpp"

namespace ac::http {
namespace {

constexpr std::string_view kEmptyClientId = "local";

Response makeError(int status, std::string_view reason) {
  Response response;
  response.status = status;
  response.body = "{\"ok\":false,\"error\":\"";
  response.body += reason;
  response.body += "\"}";
  return response;
}

std::string makeNotFound(std::string_view path) {
  return "{\"error\":\"not-found\",\"path\":" + ac::core::json::quote(path) + "}";
}

std::string makeEntries(const std::vector<ac::persist::MatchResultRecord>& records) {
  std::string body = "{\"ok\":true,\"entries\":[";
  for (std::size_t i = 0u; i < records.size(); ++i) {
    if (i != 0u) body += ',';
    body += ac::persist::encodeMatchRecordObject(records[i]);  // 与 NDJSON 行同源，但不带行尾换行
  }
  body += "]}";
  return body;
}

// 滑动窗口：只留最近 kReadRequestsPerMinute 次请求的时刻，窗口内达到上限即拒绝。
bool allowRequest(HttpState& state, std::string_view clientId, std::uint32_t nowMs) noexcept {
  const std::string_view id = clientId.empty() ? kEmptyClientId : clientId;
  std::size_t slot = kMaxTrackedClients;
  std::size_t freeSlot = kMaxTrackedClients;
  for (std::size_t i = 0u; i < kMaxTrackedClients; ++i) {
    HttpState::ClientWindow& window = state.clients[i];
    if (!window.isUsed) {
      if (freeSlot == kMaxTrackedClients) freeSlot = i;
      continue;
    }
    if (window.id == id) {
      slot = i;
      break;
    }
  }
  if (slot == kMaxTrackedClients) {
    if (freeSlot == kMaxTrackedClients) {
      // 表满：挤掉最久没露面的那个（限流表只是记账，不做正确性承诺）。
      freeSlot = 0u;
      for (std::size_t i = 1u; i < kMaxTrackedClients; ++i) {
        if (state.clients[i].lastSeenMs < state.clients[freeSlot].lastSeenMs) freeSlot = i;
      }
    }
    HttpState::ClientWindow& fresh = state.clients[freeSlot];
    fresh = HttpState::ClientWindow{};
    fresh.isUsed = true;
    fresh.id.assign(id);
    slot = freeSlot;
  }

  HttpState::ClientWindow& window = state.clients[slot];
  window.lastSeenMs = nowMs;
  std::uint32_t inWindow = 0u;
  for (std::uint32_t i = 0u; i < window.count; ++i) {
    const std::uint32_t stamp = window.stamps[i];
    if (stamp <= nowMs && nowMs - stamp < kReadWindowMs) ++inWindow;
  }
  if (inWindow >= kReadRequestsPerMinute) return false;
  window.stamps[window.next] = nowMs;
  window.next = (window.next + 1u) % static_cast<std::uint32_t>(kReadRequestsPerMinute);
  if (window.count < kReadRequestsPerMinute) ++window.count;
  return true;
}

const std::string* findCached(const HttpState& state, std::string_view key, std::uint64_t version,
                              std::uint32_t nowMs) noexcept {
  for (const HttpState::CacheEntry& entry : state.cache) {
    if (!entry.isUsed) continue;
    if (entry.key != key || entry.version != version) continue;
    if (entry.storedAtMs <= nowMs && nowMs - entry.storedAtMs < kCacheTtlMs) return &entry.body;
  }
  return nullptr;
}

void storeCached(HttpState& state, std::string_view key, std::uint64_t version, std::uint32_t nowMs,
                 const std::string& body) {
  HttpState::CacheEntry& entry = state.cache[state.cacheNext];
  entry.isUsed = true;
  entry.key.assign(key);
  entry.version = version;
  entry.storedAtMs = nowMs;
  entry.body = body;
  state.cacheNext = (state.cacheNext + 1u) % kMaxCacheEntries;
}

}  // namespace

std::size_t parseLimit(std::string_view query, std::size_t fallback) noexcept {
  constexpr std::string_view kKey = "limit=";
  const std::size_t at = query.find(kKey);
  if (at == std::string_view::npos) return fallback;
  std::size_t cursor = at + kKey.size();
  std::size_t value = 0u;
  bool hasDigit = false;
  while (cursor < query.size() && query[cursor] >= '0' && query[cursor] <= '9') {
    hasDigit = true;
    value = value * 10u + static_cast<std::size_t>(query[cursor] - '0');
    if (value > kMaxLimit * 10u) return kMaxLimit;  // 长数字串直接夹到上限，不溢出
    ++cursor;
  }
  if (!hasDigit || value == 0u) return fallback;
  return value > kMaxLimit ? kMaxLimit : value;
}

Response handleRequest(HttpState& state, const HttpDeps& deps, const Request& request,
                       std::uint32_t nowMs) {
  if (!allowRequest(state, request.clientId, nowMs)) {
    ac::metrics::bumpCounter(deps.counters, ac::metrics::CounterId::kHttpRateLimited);
    Response response = makeError(429, "rate-limited");
    response.extraHeaderName = kRetryAfterHeader;
    response.extraHeaderValue = kRetryAfterSeconds;
    return response;
  }

  const bool isGet = request.method == "GET";
  if (isGet && request.path == kMetricsPath) {
    if (deps.metricsBody.empty()) return makeError(500, "metrics-unavailable");
    Response response;
    response.contentType = ac::metrics::kMetricsContentType;
    response.body.assign(deps.metricsBody);
    return response;
  }
  if (isGet && request.path == kHealthPath) {
    // 与 /metrics 读同一份 ProcessSnapshot（评审中-1：不再各存一份房间/连接/玩家数）。
    char buffer[256];
    std::snprintf(buffer, sizeof(buffer),
                  "{\"status\":\"ok\",\"protocolVersion\":%u,\"rooms\":%u,\"connections\":%u,"
                  "\"players\":%u,\"graceActive\":%u,\"recordsRetained\":%llu,\"uptimeSeconds\":%llu,"
                  "\"ticks\":%llu}",
                  static_cast<unsigned>(deps.health.process.protocol),
                  static_cast<unsigned>(deps.health.process.rooms),
                  static_cast<unsigned>(deps.health.process.connections),
                  static_cast<unsigned>(deps.health.process.players),
                  static_cast<unsigned>(deps.health.process.graceActive),
                  static_cast<unsigned long long>(deps.health.process.recordsRetained),
                  static_cast<unsigned long long>(deps.health.process.uptimeSeconds),
                  static_cast<unsigned long long>(deps.health.ticks));
    Response response;
    response.body = buffer;
    return response;
  }

  const bool isLeaderboard = request.path == kLeaderboardPath;
  const bool isRecent = request.path == kRecentPath;
  if (isGet && (isLeaderboard || isRecent)) {
    if (deps.store == nullptr || !deps.store->isReady()) return makeError(503, "store-not-ready");
    const std::size_t limit =
        parseLimit(request.query, isLeaderboard ? kDefaultLeaderboardLimit : kDefaultRecentLimit);
    char key[160];
    std::snprintf(key, sizeof(key), "%.*s:%zu", static_cast<int>(request.path.size()),
                  request.path.data(), limit);
    const std::uint64_t version = deps.store->version();
    if (const std::string* cached = findCached(state, key, version, nowMs)) {
      ac::metrics::bumpCounter(deps.counters, ac::metrics::CounterId::kHttpCacheHits);
      Response response;
      response.body = *cached;
      return response;
    }
    const std::vector<ac::persist::MatchResultRecord> records =
        isLeaderboard ? deps.store->listTop(limit) : deps.store->listRecent(limit);
    Response response;
    response.body = makeEntries(records);
    if (response.body.size() > kMaxBodyBytes) return makeError(500, "response-too-large");
    storeCached(state, key, version, nowMs, response.body);
    return response;
  }

  Response response;
  response.status = 404;
  response.body = makeNotFound(request.path);
  return response;
}

}  // namespace ac::http
