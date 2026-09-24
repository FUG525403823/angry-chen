// S11 §5/§6–§7：恶意输入矩阵（12 类 × 3 变体 = 36 条，用例名同时含 security 与 malicious）
// 与两套滑动窗口、join 节流、命令 seq 幂等、指标接线。
#include "tiny_test.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>

#include "core/math.hpp"
#include "metrics/counters.hpp"
#include "security/pose_validation.hpp"
#include "security/rate_limit.hpp"
#include "security/rewind.hpp"
#include "security/validate.hpp"
#include "sim/arena.hpp"
#include "sim/movement.hpp"
#include "sim/pose_history.hpp"

namespace sec = ac::security;
namespace metrics = ac::metrics;
namespace sim = ac::sim;


namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

ac::sim::Command commandWith(std::size_t index, double value) {
  ac::sim::Command raw{};
  if (index == 0u) {
    raw.moveX = value;
  } else if (index == 1u) {
    raw.moveY = value;
  } else if (index == 2u) {
    raw.yaw = value;
  } else {
    raw.pitch = value;
  }
  return raw;
}

// 矩阵 1：NaN / ±Inf 归零后通过，不崩、计 0 打击。
void expectNonFiniteZeroed(double value, std::size_t index) {
  const ac::sim::Command raw = commandWith(index, value);
  sec::ClampReport report{};
  const ac::sim::Command clean = sec::sanitizeCommand(raw, report);
  AC_CHECK(report.isNonFinite);
  AC_CHECK(clean.moveX == 0.0);
  AC_CHECK(clean.moveY == 0.0);
  AC_CHECK(clean.yaw == 0.0);
  AC_CHECK(clean.pitch == 0.0);
  sec::RateLimiter limiter{};
  metrics::CounterRegistry counters{};
  AC_CHECK(sec::admitMessage(limiter, 1000u, &counters) == sec::RateVerdict::kOk);
  AC_CHECK_EQ(limiter.messages.strikes, 0u);
  AC_CHECK_EQ(metrics::counterValue(counters, metrics::CounterId::kMalformedFrames), 0u);
  AC_CHECK_EQ(metrics::counterValue(counters, metrics::CounterId::kRateLimitedFrames), 0u);
}

// 矩阵 2/3：按位移量判定 (0.36225, 0.543375] 可疑、超出硬上限硬纠正。
void expectSpeedVerdict(double distanceM, sec::PoseVerdict expected) {
  metrics::CounterRegistry counters{};
  sec::PoseSample sample{};
  sample.prevX = 0.0;
  sample.prevZ = 0.0;
  sample.x = distanceM;
  sample.z = 0.0;
  sample.y = ac::sim::arena::kBarn.max.y;  // 高于仓顶，避免位置非法抢先
  const sec::PoseOutcome out = sec::evaluatePose(sample, &counters);
  AC_CHECK(out.verdict == expected);
  AC_CHECK_NEAR(out.distanceM, distanceM, 1e-12);
  if (expected == sec::PoseVerdict::kSuspect) {
    AC_CHECK(!out.isRolledBack);
    AC_CHECK_EQ(metrics::counterValue(counters, metrics::CounterId::kPoseSuspect), 1u);
    AC_CHECK_EQ(metrics::counterValue(counters, metrics::CounterId::kHardCorrect), 0u);
    return;
  }
  AC_CHECK(out.isRolledBack);
  AC_CHECK_NEAR(out.correctedX, 0.0, 1e-12);
  AC_CHECK_NEAR(out.correctedZ, 0.0, 1e-12);
  AC_CHECK_EQ(metrics::counterValue(counters, metrics::CounterId::kHardCorrect), 1u);
  AC_CHECK_EQ(metrics::counterValue(counters, metrics::CounterId::kSpeedViolations), 1u);
}

// 矩阵 4/5：命令窗口只丢弃，消息窗口才计打击。
void expectCommandRate(uint32_t count, bool expectStrike) {
  sec::RateLimiter limiter{};
  metrics::CounterRegistry counters{};
  uint32_t ok = 0u;
  uint32_t limited = 0u;
  for (uint32_t i = 0u; i < count; ++i) {
    const sec::RateVerdict verdict = sec::admitCommand(limiter, 1000u, &counters);
    if (verdict == sec::RateVerdict::kOk) {
      ++ok;
    } else {
      ++limited;
    }
    AC_CHECK(verdict != sec::RateVerdict::kDisconnect);  // 命令窗口永不断开
  }
  AC_CHECK_EQ(ok, 30u);
  AC_CHECK_EQ(limited, count - 30u);
  AC_CHECK_EQ(metrics::counterValue(counters, metrics::CounterId::kDroppedFrames), count - 30u);
  AC_CHECK_EQ(limiter.commands.strikes, 0u);
  AC_CHECK_EQ(limiter.messages.strikes, expectStrike ? 1u : 0u);
}

// 矩阵 6/7：载荷与整包上限。
void expectPayloadRejected(std::size_t bytes, sec::ValidateReason expected) {
  const sec::ValidateResult result = expected == sec::ValidateReason::kPayloadTooLarge
                                         ? sec::validatePayloadSize(bytes)
                                         : sec::validatePacketSize(bytes);
  AC_CHECK(!result.isOk);
  AC_CHECK(result.reason == expected);
  metrics::CounterRegistry counters{};
  sec::noteValidateFailure(&counters, result.reason);
  const metrics::CounterId id = expected == sec::ValidateReason::kPayloadTooLarge
                                    ? metrics::CounterId::kMalformedFrames
                                    : metrics::CounterId::kOversizedFrames;
  AC_CHECK_EQ(metrics::counterValue(counters, id), 1u);
}

// 矩阵 8：重复 seq 幂等（只应用一次）。
void expectDuplicateOnce(uint32_t repeats) {
  sec::CommandDedup dedup{};
  uint32_t applied = 0u;
  for (uint32_t i = 0u; i < repeats; ++i) {
    if (!sec::noteCommandSequence(dedup, 4242u)) ++applied;
  }
  AC_CHECK_EQ(applied, 1u);
}

// 矩阵 9：未知 opcode 丢弃并计 malformed。
void expectUnknownOpcode(uint8_t type) {
  const sec::ValidateResult result = sec::validateOpcode(type);
  AC_CHECK(!result.isOk);
  AC_CHECK(result.reason == sec::ValidateReason::kUnknownOpcode);
  metrics::CounterRegistry counters{};
  sec::noteValidateFailure(&counters, result.reason);
  AC_CHECK_EQ(metrics::counterValue(counters, metrics::CounterId::kMalformedFrames), 1u);
  // 不断开：判定函数不产生任何断开副作用。
  sec::RateLimiter limiter{};
  AC_CHECK(sec::admitMessage(limiter, 1000u, nullptr) == sec::RateVerdict::kOk);
}

// 矩阵 10：越界字段夹取。
void expectClampedMove(double rawValue, double expected) {
  const ac::sim::Command raw = commandWith(0u, rawValue);
  sec::ClampReport report{};
  const ac::sim::Command clean = sec::sanitizeCommand(raw, report);
  AC_CHECK_NEAR(clean.moveX, expected, 1e-12);
  AC_CHECK(report.isMoveXClamped);
}

// 矩阵 11：过期 / 未来 tick 丢弃。
void expectTickDropped(uint32_t clientTick, uint32_t serverTick, sec::ValidateReason expected) {
  const sec::ValidateResult result = sec::validateClientTick(clientTick, serverTick);
  AC_CHECK(!result.isOk);
  AC_CHECK(result.reason == expected);
  metrics::CounterRegistry counters{};
  sec::noteValidateFailure(&counters, result.reason);
  AC_CHECK_EQ(metrics::counterValue(counters, metrics::CounterId::kDroppedFrames), 1u);
}

// 矩阵 12：回退越限（rttMs / 2 > 200）不得回滚。
void expectRewindOverLimit(uint32_t rttMs) {
  sim::PoseHistory history{};
  metrics::CounterRegistry counters{};
  const sec::RewindOutcome out = sec::sampleRewindPose(history, rttMs, 1u, &counters);
  AC_CHECK(!out.isOk);
  AC_CHECK(out.isClamped);
  AC_CHECK(!out.isFound);
  AC_CHECK_EQ(metrics::counterValue(counters, metrics::CounterId::kRewindClamped), 1u);
}

}  // namespace

// ---- 矩阵 1：NaN / +Inf / -Inf ----
AC_TEST(security_malicious_01a_nan_move_x) { expectNonFiniteZeroed(kNaN, 0u); }
AC_TEST(security_malicious_01b_inf_move_y) { expectNonFiniteZeroed(kInf, 1u); }
AC_TEST(security_malicious_01c_neg_inf_yaw_nan_pitch) {
  expectNonFiniteZeroed(-kInf, 2u);
  expectNonFiniteZeroed(kNaN, 3u);
}

// ---- 矩阵 2：超过 0.36225 但 ≤ 0.543375 → Suspect ----
AC_TEST(security_malicious_02a_slight_over_limit_suspect) { expectSpeedVerdict(0.36226, sec::PoseVerdict::kSuspect); }
AC_TEST(security_malicious_02b_mid_range_suspect) { expectSpeedVerdict(0.45, sec::PoseVerdict::kSuspect); }
// 硬上限的字面量 0.543375 与计算值相差约 1 ulp（计算值更小），等值比较会被浮点误差翻面 →
// 矩阵 2/3 的边界用 ±1e-9 相对带的两侧（数值本身在 pose_validation_frozen_values 里以 1e-12 钉住）。
AC_TEST(security_malicious_02c_just_below_hard_limit_suspect) {
  expectSpeedVerdict(sec::kHardCorrectLimitM * (1.0 - 1e-9), sec::PoseVerdict::kSuspect);
}

// ---- 矩阵 3：超过 0.543375 → 硬纠正 ----
AC_TEST(security_malicious_03a_just_over_hard_limit_corrected) {
  expectSpeedVerdict(sec::kHardCorrectLimitM * (1.0 + 1e-9), sec::PoseVerdict::kCorrected);
}
AC_TEST(security_malicious_03b_double_speed_corrected) { expectSpeedVerdict(1.0, sec::PoseVerdict::kCorrected); }
// 栅栏内沿（±39.35 m）之内能构造的最大位移约 78 m；「瞬移」取 10 m（仍在场地内，不触发位置非法优先）。
AC_TEST(security_malicious_03c_teleport_corrected) { expectSpeedVerdict(10.0, sec::PoseVerdict::kCorrected); }

// ---- 矩阵 4：31…60/s 命令，超出丢弃不断开 ----
AC_TEST(security_malicious_04a_31_commands_no_strike) { expectCommandRate(31u, false); }
AC_TEST(security_malicious_04b_45_commands_no_strike) { expectCommandRate(45u, false); }
AC_TEST(security_malicious_04c_60_commands_no_strike) { expectCommandRate(60u, false); }

// ---- 矩阵 5：>60/s 消息，第 3 次打击断开 ----
AC_TEST(security_malicious_05a_61_messages_one_strike) {
  sec::RateLimiter limiter{};
  metrics::CounterRegistry counters{};
  sec::RateVerdict verdict = sec::RateVerdict::kOk;
  for (uint32_t i = 0u; i < 61u; ++i) verdict = sec::admitMessage(limiter, 1000u, &counters);
  AC_CHECK(verdict == sec::RateVerdict::kLimited);
  AC_CHECK_EQ(limiter.messages.strikes, 1u);
  AC_CHECK_EQ(sec::kDisconnectReasonRateLimited, 6u);
}
AC_TEST(security_malicious_05b_63_messages_disconnect) {
  sec::RateLimiter limiter{};
  metrics::CounterRegistry counters{};
  sec::RateVerdict verdict = sec::RateVerdict::kOk;
  for (uint32_t i = 0u; i < 63u; ++i) verdict = sec::admitMessage(limiter, 1000u, &counters);
  AC_CHECK(verdict == sec::RateVerdict::kDisconnect);
  AC_CHECK_EQ(limiter.messages.strikes, 3u);
  AC_CHECK_EQ(metrics::counterValue(counters, metrics::CounterId::kRateLimitedFrames), 3u);
}
AC_TEST(security_malicious_05c_flood_disconnects_at_third_strike) {
  sec::RateLimiter limiter{};
  metrics::CounterRegistry counters{};
  uint32_t disconnectAt = 0u;
  for (uint32_t i = 1u; i <= 300u; ++i) {
    if (sec::admitMessage(limiter, 1000u, &counters) == sec::RateVerdict::kDisconnect && disconnectAt == 0u) {
      disconnectAt = i;
    }
  }
  AC_CHECK_EQ(disconnectAt, 63u);
}

// ---- 矩阵 6：命令载荷 > 64 B ----
AC_TEST(security_malicious_06a_65_byte_payload) { expectPayloadRejected(65u, sec::ValidateReason::kPayloadTooLarge); }
AC_TEST(security_malicious_06b_100_byte_payload) { expectPayloadRejected(100u, sec::ValidateReason::kPayloadTooLarge); }
AC_TEST(security_malicious_06c_4096_byte_payload) { expectPayloadRejected(4096u, sec::ValidateReason::kPayloadTooLarge); }

// ---- 矩阵 7：UDP 载荷 > 1200 B ----
AC_TEST(security_malicious_07a_1201_byte_packet) { expectPayloadRejected(1201u, sec::ValidateReason::kPacketTooLarge); }
AC_TEST(security_malicious_07b_1500_byte_packet) { expectPayloadRejected(1500u, sec::ValidateReason::kPacketTooLarge); }
AC_TEST(security_malicious_07c_65535_byte_packet) { expectPayloadRejected(65535u, sec::ValidateReason::kPacketTooLarge); }

// ---- 矩阵 8：重复 msgId / seq（重传）幂等 ----
AC_TEST(security_malicious_08a_duplicate_applied_once) { expectDuplicateOnce(2u); }
AC_TEST(security_malicious_08b_triple_retransmit_applied_once) { expectDuplicateOnce(3u); }
AC_TEST(security_malicious_08c_retransmit_burst_applied_once) { expectDuplicateOnce(10u); }

// ---- 矩阵 9：未知 opcode ----
AC_TEST(security_malicious_09a_opcode_zero) { expectUnknownOpcode(0u); }
AC_TEST(security_malicious_09b_opcode_eleven) { expectUnknownOpcode(11u); }
AC_TEST(security_malicious_09c_opcode_255) { expectUnknownOpcode(255u); }

// ---- 矩阵 10：越界字段夹取 ----
AC_TEST(security_malicious_10a_move_x_twelve_clamped) { expectClampedMove(12.0, 1.0); }
AC_TEST(security_malicious_10b_pitch_nine_clamped) {
  ac::sim::Command raw{};
  raw.pitch = 9.0;
  sec::ClampReport report{};
  const ac::sim::Command clean = sec::sanitizeCommand(raw, report);
  AC_CHECK_NEAR(clean.pitch, ac::config::kPitchLimitRad, 1e-12);
  AC_CHECK(report.isPitchClamped);
}
AC_TEST(security_malicious_10c_switch_to_seven_zeroed) {
  ac::sim::Command raw{};
  raw.switchTo = 7u;
  sec::ClampReport report{};
  const ac::sim::Command clean = sec::sanitizeCommand(raw, report);
  AC_CHECK_EQ(clean.switchTo, 0u);
  AC_CHECK(report.isSwitchToReset);
}

// ---- 矩阵 11：负 / 未来 tick ----
AC_TEST(security_malicious_11a_future_tick_dropped) { expectTickDropped(101u, 100u, sec::ValidateReason::kFutureTick); }
AC_TEST(security_malicious_11b_negative_tick_encoding_dropped) { expectTickDropped(0xFFFFFFFFu, 100u, sec::ValidateReason::kFutureTick); }
AC_TEST(security_malicious_11c_stale_tick_dropped) { expectTickDropped(99u, 100u, sec::ValidateReason::kStaleTick); }

// ---- 矩阵 12：请求回退 300 ms ----
AC_TEST(security_malicious_12a_rewind_300ms_not_ok) { expectRewindOverLimit(600u); }
AC_TEST(security_malicious_12b_rewind_250ms_not_ok) { expectRewindOverLimit(500u); }
AC_TEST(security_malicious_12c_rewind_402ms_boundary) { expectRewindOverLimit(403u); }

// ---- 非矩阵：契约、边界与接线 ----
AC_TEST(security_reason_and_verdict_names) {
  AC_CHECK_EQ(std::string_view(sec::validateReasonName(sec::ValidateReason::kOk)), std::string_view("ok"));
  AC_CHECK_EQ(std::string_view(sec::validateReasonName(sec::ValidateReason::kPayloadTooLarge)),
              std::string_view("payloadTooLarge"));
  AC_CHECK_EQ(std::string_view(sec::validateReasonName(sec::ValidateReason::kDuplicateSequence)),
              std::string_view("duplicateSequence"));
  AC_CHECK_EQ(std::string_view(sec::rateVerdictName(sec::RateVerdict::kDisconnect)), std::string_view("disconnect"));
  AC_CHECK_EQ(std::string_view(sec::poseVerdictName(sec::PoseVerdict::kRejected)), std::string_view("rejected"));
}

AC_TEST(security_five_authority_counters_registered) {
  AC_CHECK_EQ(metrics::counterCount(), 9u);
  AC_CHECK(metrics::isCounterRegistered("ac_pose_suspect_total"));
  AC_CHECK(metrics::isCounterRegistered("ac_pose_rejected_total"));
  AC_CHECK(metrics::isCounterRegistered("ac_hard_correct_total"));
  AC_CHECK(metrics::isCounterRegistered("ac_rewind_clamped_total"));
  AC_CHECK(metrics::isCounterRegistered("ac_speed_violations_total"));
  AC_CHECK(metrics::isCounterRegistered("ac_malformed_frames_total"));
  AC_CHECK(metrics::isCounterRegistered("ac_oversized_frames_total"));
  AC_CHECK(metrics::isCounterRegistered("ac_rate_limited_frames_total"));
  AC_CHECK(metrics::isCounterRegistered("ac_dropped_frames_total"));
  AC_CHECK(!metrics::isCounterRegistered("ac_not_a_counter_total"));
}

AC_TEST(security_counter_increment_and_reset) {
  metrics::CounterRegistry counters{};
  metrics::addCounter(counters, metrics::CounterId::kHardCorrect, 2u);
  metrics::addCounter(counters, metrics::CounterId::kHardCorrect);
  AC_CHECK_EQ(metrics::counterValue(counters, metrics::CounterId::kHardCorrect), 3u);
  AC_CHECK_EQ(std::string_view(metrics::counterName(metrics::CounterId::kHardCorrect)),
              std::string_view("ac_hard_correct_total"));
  metrics::resetCounters(counters);
  AC_CHECK_EQ(metrics::counterValue(counters, metrics::CounterId::kHardCorrect), 0u);
}

AC_TEST(security_size_boundaries) {
  AC_CHECK(sec::validatePayloadSize(64u).isOk);
  AC_CHECK(!sec::validatePayloadSize(65u).isOk);
  AC_CHECK(sec::validatePacketSize(1200u).isOk);
  AC_CHECK(!sec::validatePacketSize(1201u).isOk);
  AC_CHECK_EQ(sec::kMaxCommandPayloadBytes, 64u);
  AC_CHECK_EQ(ac::net::kMaxPacketBytes, 1200u);
  AC_CHECK_EQ(sec::kDedupWindow, 64u);
}

AC_TEST(security_opcode_whitelist) {
  AC_CHECK(sec::isClientToServerType(1u));
  AC_CHECK(sec::validateOpcode(3u).isOk);
  AC_CHECK(sec::validateOpcode(4u).isOk);
  AC_CHECK(sec::validateOpcode(7u).isOk);
  AC_CHECK(sec::validateOpcode(8u).isOk);
  AC_CHECK(sec::validateOpcode(9u).isOk);
  AC_CHECK(!sec::validateOpcode(1u).isOk);  // Hello 属握手层
  AC_CHECK(!sec::isClientToServerType(2u));
  AC_CHECK(!sec::isClientToServerType(5u));
  AC_CHECK(!sec::isClientToServerType(6u));
  AC_CHECK(!sec::isClientToServerType(10u));
}

AC_TEST(security_tick_boundary_accepts_equal) {
  AC_CHECK(sec::validateClientTick(100u, 100u).isOk);
  AC_CHECK(!sec::validateClientTick(100u, 101u).isOk);
  AC_CHECK(!sec::validateClientTick(101u, 100u).isOk);
}

AC_TEST(security_clamp_table_full) {
  ac::sim::Command raw{};
  raw.moveX = -5.0;
  raw.moveY = 5.0;
  raw.yaw = 3.0 * ac::kPi;
  raw.pitch = -3.0;
  raw.buttons = 0xFFu;
  raw.switchTo = 2u;
  sec::ClampReport report{};
  const ac::sim::Command clean = sec::sanitizeCommand(raw, report);
  AC_CHECK_NEAR(clean.moveX, -1.0, 1e-12);
  AC_CHECK_NEAR(clean.moveY, 1.0, 1e-12);
  AC_CHECK_NEAR(clean.yaw, ac::kPi, 1e-12);
  AC_CHECK_NEAR(clean.pitch, -ac::config::kPitchLimitRad, 1e-12);
  AC_CHECK_EQ(clean.buttons, 0xFFu);
  AC_CHECK_EQ(clean.switchTo, 2u);
  AC_CHECK(report.hasAnyAdjustment());
  AC_CHECK(report.isMoveXClamped && report.isMoveYClamped && report.isYawWrapped && report.isPitchClamped);
  AC_CHECK(!report.isSwitchToReset);
  AC_CHECK_EQ(clean.seq, raw.seq);
  AC_CHECK_EQ(clean.clientTick, raw.clientTick);
}

AC_TEST(security_yaw_wrap_is_bounded_for_huge_values) {
  const double values[3] = {1.0e300, -1.0e300, 12345.678};
  for (double value : values) {
    ac::sim::Command raw{};
    raw.yaw = value;
    sec::ClampReport report{};
    const ac::sim::Command clean = sec::sanitizeCommand(raw, report);
    AC_CHECK(std::isfinite(clean.yaw));
    AC_CHECK(clean.yaw >= -ac::kPi && clean.yaw <= ac::kPi);
  }
}

AC_TEST(security_command_window_exactly_thirty) {
  sec::RateLimiter limiter{};
  metrics::CounterRegistry counters{};
  for (uint32_t i = 0u; i < 30u; ++i) {
    AC_CHECK(sec::admitCommand(limiter, 1000u, &counters) == sec::RateVerdict::kOk);
  }
  AC_CHECK(sec::admitCommand(limiter, 1000u, &counters) == sec::RateVerdict::kLimited);
  AC_CHECK_EQ(metrics::counterValue(counters, metrics::CounterId::kDroppedFrames), 1u);
  AC_CHECK(sec::admitCommand(limiter, 2001u, &counters) == sec::RateVerdict::kOk);  // 窗口滑出
}

AC_TEST(security_message_strikes_reset_after_quiet_window) {
  sec::RateLimiter limiter{};
  for (uint32_t i = 0u; i < 63u; ++i) (void)sec::admitMessage(limiter, 1000u, nullptr);
  AC_CHECK_EQ(limiter.messages.strikes, 3u);
  AC_CHECK(sec::admitMessage(limiter, 3000u, nullptr) == sec::RateVerdict::kOk);
  AC_CHECK_EQ(limiter.messages.strikes, 0u);  // 窗口内 0 条 → 打击清零（v1 语义）
}

AC_TEST(security_join_throttle_five_failures) {
  sec::JoinThrottle throttle{};
  for (uint32_t i = 1u; i < 5u; ++i) {
    AC_CHECK(!sec::noteJoinFailure(throttle, 1000u));
  }
  AC_CHECK(sec::noteJoinFailure(throttle, 1000u));
  AC_CHECK(sec::isJoinThrottled(throttle));
  sec::noteJoinSuccess(throttle);
  AC_CHECK(!sec::isJoinThrottled(throttle));
  for (uint32_t i = 1u; i < 5u; ++i) {
    AC_CHECK(!sec::noteJoinFailure(throttle, 5000u));
  }
  AC_CHECK(!sec::noteJoinFailure(throttle, 16000u));  // 窗口过期后重新计数
}

AC_TEST(security_dedup_window_eviction) {
  sec::CommandDedup dedup{};
  for (uint16_t seq = 1u; seq <= 64u; ++seq) {
    AC_CHECK(!sec::noteCommandSequence(dedup, seq));
  }
  AC_CHECK(!sec::noteCommandSequence(dedup, 65u));  // 第 65 条挤掉最早的一条
  AC_CHECK(!sec::noteCommandSequence(dedup, 1u));   // 已被挤出窗口 → 视为新命令
  AC_CHECK(sec::noteCommandSequence(dedup, 64u));
  sec::resetCommandDedup(dedup);
  AC_CHECK(!sec::noteCommandSequence(dedup, 64u));
}

AC_TEST(security_dedup_handles_seq_wraparound) {
  sec::CommandDedup dedup{};
  AC_CHECK(!sec::noteCommandSequence(dedup, 65535u));
  AC_CHECK(!sec::noteCommandSequence(dedup, 0u));
  AC_CHECK(sec::noteCommandSequence(dedup, 65535u));
  AC_CHECK(sec::noteCommandSequence(dedup, 0u));
}

AC_TEST(security_failure_counter_mapping) {
  metrics::CounterRegistry counters{};
  sec::noteValidateFailure(&counters, sec::ValidateReason::kPayloadTooLarge);
  sec::noteValidateFailure(&counters, sec::ValidateReason::kUnknownOpcode);
  sec::noteValidateFailure(&counters, sec::ValidateReason::kPacketTooLarge);
  sec::noteValidateFailure(&counters, sec::ValidateReason::kStaleTick);
  sec::noteValidateFailure(&counters, sec::ValidateReason::kFutureTick);
  sec::noteValidateFailure(&counters, sec::ValidateReason::kDuplicateSequence);
  sec::noteValidateFailure(&counters, sec::ValidateReason::kOk);
  sec::noteValidateFailure(nullptr, sec::ValidateReason::kOk);
  AC_CHECK_EQ(metrics::counterValue(counters, metrics::CounterId::kMalformedFrames), 2u);
  AC_CHECK_EQ(metrics::counterValue(counters, metrics::CounterId::kOversizedFrames), 1u);
  AC_CHECK_EQ(metrics::counterValue(counters, metrics::CounterId::kDroppedFrames), 3u);
  AC_CHECK_EQ(metrics::counterValue(counters, metrics::CounterId::kPoseSuspect), 0u);
}

AC_TEST(security_rate_windows_are_independent) {
  sec::RateLimiter limiter{};
  metrics::CounterRegistry counters{};
  for (uint32_t i = 0u; i < 30u; ++i) {
    AC_CHECK(sec::admitCommand(limiter, 2000u, &counters) == sec::RateVerdict::kOk);
    AC_CHECK(sec::admitMessage(limiter, 2000u, &counters) == sec::RateVerdict::kOk);
  }
  AC_CHECK(sec::admitCommand(limiter, 2000u, &counters) == sec::RateVerdict::kLimited);
  AC_CHECK(sec::admitMessage(limiter, 2000u, &counters) == sec::RateVerdict::kOk);  // 消息窗口还有余量
  AC_CHECK_EQ(limiter.messages.strikes, 0u);
  AC_CHECK_EQ(metrics::counterValue(counters, metrics::CounterId::kDroppedFrames), 1u);
  AC_CHECK(sec::kCommandRateLimit == 30u && sec::kMaxMessagesPerSecond == 60u && sec::kRateWindowMs == 1000u);
  AC_CHECK_EQ(sec::kRateStrikesBeforeDisconnect, 3u);
  AC_CHECK_EQ(sec::kJoinThrottleMaxFailures, 5u);
  AC_CHECK_EQ(sec::kJoinThrottleWindowMs, 10000u);
}
