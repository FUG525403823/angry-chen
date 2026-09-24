#include "security/validate.hpp"

#include <cmath>

#include "core/math.hpp"

namespace ac::security {
namespace {

double zeroIfNotFinite(double value, bool& flagged) noexcept {
  if (std::isfinite(value)) return value;
  flagged = true;
  return 0.0;
}

double clampRange(double value, double low, double high, bool& flagged) noexcept {
  if (value < low) {
    flagged = true;
    return low;
  }
  if (value > high) {
    flagged = true;
    return high;
  }
  return value;
}

// §5 字段表：yaw 取模复用 S02 的 core::wrapAngle（§5.1 冻结：左开右闭 (-π, π]，O(1) 无循环），
// 避免再写一份 std::fmod 版本造成两套边界语义漂移。这里只多报一个「是否被改动」的诊断位。
double wrapYaw(double yaw, bool& flagged) noexcept {
  const double wrapped = ac::wrapAngle(yaw);
  if (wrapped != yaw) flagged = true;
  return wrapped;
}

}  // namespace

const char* validateReasonName(ValidateReason reason) noexcept {
  switch (reason) {
    case ValidateReason::kOk: return "ok";
    case ValidateReason::kPacketTooLarge: return "packetTooLarge";
    case ValidateReason::kPayloadTooLarge: return "payloadTooLarge";
    case ValidateReason::kUnknownOpcode: return "unknownOpcode";
    case ValidateReason::kStaleTick: return "staleTick";
    case ValidateReason::kFutureTick: return "futureTick";
    case ValidateReason::kDuplicateSequence: return "duplicateSequence";
  }
  return "unknown";
}

ValidateResult validatePacketSize(std::size_t bytes) noexcept {
  if (bytes > ac::net::kMaxPacketBytes) return ValidateResult{false, ValidateReason::kPacketTooLarge};
  return ValidateResult{};
}

ValidateResult validatePayloadSize(std::size_t bytes) noexcept {
  if (bytes > kMaxCommandPayloadBytes) return ValidateResult{false, ValidateReason::kPayloadTooLarge};
  return ValidateResult{};
}

bool isClientToServerType(uint8_t type) noexcept {
  // ADR-009 §5.1 类型码：1 Hello / 3 Resume / 4 Command / 7 KeepAlive / 8 Disconnect / 9 Fragment。
  // Hello 只出现在握手层（S04）之前，会话期的白名单见 validateOpcode。
  switch (static_cast<ac::net::PacketType>(type)) {
    case ac::net::PacketType::kHello:
    case ac::net::PacketType::kResume:
    case ac::net::PacketType::kCommand:
    case ac::net::PacketType::kKeepAlive:
    case ac::net::PacketType::kDisconnect:
    case ac::net::PacketType::kFragment:
      return true;
    case ac::net::PacketType::kHelloAck:
    case ac::net::PacketType::kSnapshot:
    case ac::net::PacketType::kEvent:
    case ac::net::PacketType::kMatchState:
      return false;
  }
  return false;
}

ValidateResult validateOpcode(uint8_t type) noexcept {
  if (!isClientToServerType(type) || type == static_cast<uint8_t>(ac::net::PacketType::kHello)) {
    return ValidateResult{false, ValidateReason::kUnknownOpcode};
  }
  return ValidateResult{};
}

ValidateResult validateClientTick(uint32_t clientTick, uint32_t serverTick) noexcept {
  if (clientTick < serverTick) return ValidateResult{false, ValidateReason::kStaleTick};
  if (clientTick > serverTick) return ValidateResult{false, ValidateReason::kFutureTick};
  return ValidateResult{};
}

ac::sim::Command sanitizeCommand(const ac::sim::Command& raw, ClampReport& report) noexcept {
  report = ClampReport{};
  ac::sim::Command out = raw;
  out.moveX = clampRange(zeroIfNotFinite(raw.moveX, report.isNonFinite), -ac::config::kMoveAxisLimit,
                         ac::config::kMoveAxisLimit, report.isMoveXClamped);
  out.moveY = clampRange(zeroIfNotFinite(raw.moveY, report.isNonFinite), -ac::config::kMoveAxisLimit,
                         ac::config::kMoveAxisLimit, report.isMoveYClamped);
  out.yaw = wrapYaw(zeroIfNotFinite(raw.yaw, report.isNonFinite), report.isYawWrapped);
  out.pitch = clampRange(zeroIfNotFinite(raw.pitch, report.isNonFinite), -ac::config::kPitchLimitRad,
                         ac::config::kPitchLimitRad, report.isPitchClamped);
  out.buttons = static_cast<uint8_t>(raw.buttons & 0xFFu);
  if (raw.switchTo > 2u) {
    report.isSwitchToReset = true;
    out.switchTo = 0u;
  }
  return out;
}

bool noteCommandSequence(CommandDedup& dedup, uint16_t seq) noexcept {
  const std::size_t filled = dedup.count;
  for (std::size_t i = 0u; i < filled; ++i) {
    if (dedup.recent[i] == seq) return true;
  }
  if (filled < kDedupWindow) {
    dedup.recent[filled] = seq;
    dedup.count = static_cast<uint8_t>(filled + 1u);
  } else {
    dedup.recent[dedup.next] = seq;
    dedup.next = static_cast<uint8_t>((static_cast<std::size_t>(dedup.next) + 1u) % kDedupWindow);
  }
  return false;
}

void resetCommandDedup(CommandDedup& dedup) noexcept { dedup = CommandDedup{}; }

void noteValidateFailure(ac::metrics::CounterRegistry* counters, ValidateReason reason) noexcept {
  // 规格 §5 的四个帧计数：整帧过大 → oversized；载荷/opcode 非法 → malformed；tick 非法与重复 → dropped。
  switch (reason) {
    case ValidateReason::kPacketTooLarge:
      ac::metrics::bumpCounter(counters, ac::metrics::CounterId::kOversizedFrames);
      break;
    case ValidateReason::kPayloadTooLarge:
    case ValidateReason::kUnknownOpcode:
      ac::metrics::bumpCounter(counters, ac::metrics::CounterId::kMalformedFrames);
      break;
    case ValidateReason::kStaleTick:
    case ValidateReason::kFutureTick:
    case ValidateReason::kDuplicateSequence:
      ac::metrics::bumpCounter(counters, ac::metrics::CounterId::kDroppedFrames);
      break;
    case ValidateReason::kOk:
      break;
  }
}

}  // namespace ac::security
