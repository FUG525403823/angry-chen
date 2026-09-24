#include "security/rate_limit.hpp"

namespace ac::security {
namespace {

// v1 checkRateLimit 逐字端口：先滑动（保留窗口内的），再判阈值。
// strikeOnOverflow = false 时只丢弃不产生打击（命令窗口）。
RateVerdict admit(RateWindow& window, uint32_t nowMs, uint32_t maxPerWindow, bool strikeOnOverflow) noexcept {
  std::size_t kept = 0u;
  for (std::size_t i = 0u; i < window.count; ++i) {
    const uint32_t at = window.times[i];
    if (nowMs - at < kRateWindowMs) {
      window.times[kept] = at;
      kept += 1u;
    }
  }
  window.count = static_cast<uint32_t>(kept);
  if (kept >= static_cast<std::size_t>(maxPerWindow)) {
    if (!strikeOnOverflow) return RateVerdict::kLimited;
    window.strikes += 1u;
    return window.strikes >= kRateStrikesBeforeDisconnect ? RateVerdict::kDisconnect : RateVerdict::kLimited;
  }
  if (window.strikes > 0u && kept == 0u) window.strikes = 0u;
  window.times[kept] = nowMs;
  window.count = static_cast<uint32_t>(kept + 1u);
  return RateVerdict::kOk;
}

}  // namespace

const char* rateVerdictName(RateVerdict verdict) noexcept {
  switch (verdict) {
    case RateVerdict::kOk: return "ok";
    case RateVerdict::kLimited: return "limited";
    case RateVerdict::kDisconnect: return "disconnect";
  }
  return "unknown";
}

void resetRateLimiter(RateLimiter& limiter) noexcept { limiter = RateLimiter{}; }

RateVerdict admitMessage(RateLimiter& limiter, uint32_t nowMs,
                         ac::metrics::CounterRegistry* counters) noexcept {
  const RateVerdict verdict = admit(limiter.messages, nowMs, kMaxMessagesPerSecond, true);
  if (verdict != RateVerdict::kOk && counters != nullptr) {
    ac::metrics::addCounter(*counters, ac::metrics::CounterId::kRateLimitedFrames);
  }
  return verdict;
}

RateVerdict admitCommand(RateLimiter& limiter, uint32_t nowMs,
                         ac::metrics::CounterRegistry* counters) noexcept {
  const RateVerdict verdict = admit(limiter.commands, nowMs, kCommandRateLimit, false);
  if (verdict != RateVerdict::kOk && counters != nullptr) {
    ac::metrics::addCounter(*counters, ac::metrics::CounterId::kDroppedFrames);
  }
  return verdict;
}

bool noteJoinFailure(JoinThrottle& throttle, uint32_t nowMs) noexcept {
  if (throttle.failures == 0u || nowMs - throttle.windowStartMs >= kJoinThrottleWindowMs) {
    throttle.failures = 0u;
    throttle.windowStartMs = nowMs;
  }
  throttle.failures += 1u;
  return throttle.failures >= kJoinThrottleMaxFailures;
}

void noteJoinSuccess(JoinThrottle& throttle) noexcept {
  throttle.failures = 0u;
  throttle.windowStartMs = 0u;
}

bool isJoinThrottled(const JoinThrottle& throttle) noexcept {
  return throttle.failures >= kJoinThrottleMaxFailures;
}

}  // namespace ac::security
