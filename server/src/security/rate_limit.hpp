#pragma once
// S11 §5/§3 交付物 2：两套滑动窗口（命令 30/s、消息 60/s）与 join 失败节流。
// 算法逐字复刻 v1 security.ts 的 checkRateLimit / createJoinThrottle（含「窗口内 0 条则清零打击」）。
#include <cstdint>

#include "metrics/counters.hpp"

namespace ac::security {

// §5 冻结数值
inline constexpr uint32_t kCommandRateLimit = 30u;              // 命令窗口：≤30 命令/s
inline constexpr uint32_t kMaxMessagesPerSecond = 60u;          // 消息窗口：≤60 消息/s
inline constexpr uint32_t kRateWindowMs = 1000u;
inline constexpr uint32_t kRateStrikesBeforeDisconnect = 3u;    // 第 3 次打击 → Disconnect
inline constexpr uint8_t kDisconnectReasonRateLimited = 6u;     // ADR-009 断开原因码
inline constexpr uint32_t kJoinThrottleMaxFailures = 5u;        // 5 次失败 / 10s
inline constexpr uint32_t kJoinThrottleWindowMs = 10000u;

enum class RateVerdict : uint8_t {
  kOk = 0,
  kLimited,     // 本条丢弃（不断开）
  kDisconnect,  // 达到打击阈值：发 Disconnect(reason = 6)
};

const char* rateVerdictName(RateVerdict verdict) noexcept;

// 时间戳环：容量 = kMaxMessagesPerSecond + 1（命令窗口只用前 31 个），无动态分配。
struct RateWindow {
  uint32_t times[kMaxMessagesPerSecond + 1u] = {};
  uint32_t count = 0u;
  uint32_t strikes = 0u;
};

struct RateLimiter {
  RateWindow commands{};  // 30/s：只丢弃、不计打击（§5 矩阵 4「不断开」）
  RateWindow messages{};  // 60/s：超限即打击，第 3 次断开
};

void resetRateLimiter(RateLimiter& limiter) noexcept;

// 消息窗口：超限 → 打击 +1；累计 kRateStrikesBeforeDisconnect 次 → kDisconnect。
RateVerdict admitMessage(RateLimiter& limiter, uint32_t nowMs,
                         ac::metrics::CounterRegistry* counters = nullptr) noexcept;

// 命令窗口：超限 → kLimited（丢弃、计 ac_dropped_frames_total），永不返回 kDisconnect。
RateVerdict admitCommand(RateLimiter& limiter, uint32_t nowMs,
                         ac::metrics::CounterRegistry* counters = nullptr) noexcept;

// join 失败节流（v1 createJoinThrottle）：返回 true = 达到阈值（本次 join 应被拒绝）。
struct JoinThrottle {
  uint32_t failures = 0u;
  uint32_t windowStartMs = 0u;
};

bool noteJoinFailure(JoinThrottle& throttle, uint32_t nowMs) noexcept;
void noteJoinSuccess(JoinThrottle& throttle) noexcept;
bool isJoinThrottled(const JoinThrottle& throttle) noexcept;

}  // namespace ac::security
