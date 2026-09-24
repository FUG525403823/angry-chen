#pragma once
// S11 §5（§3 交付物 1）：一切来自客户端的值在这里被夹取或丢弃。
// 非法输入只返回 { isOk, reason }：不抛异常、不分配、不打印（计数由调用方经 registry 累加）。
// 布尔字段一律 is/has 前缀（工程约定 §6；与 S09 §10.1-14 的 ok→isOk 回改同口径）。
#include <cstddef>
#include <cstdint>

#include "metrics/counters.hpp"
#include "net/wire.hpp"
#include "sim/movement.hpp"

namespace ac::security {

// §5 冻结上限
inline constexpr std::size_t kMaxCommandPayloadBytes = 64u;  // 命令载荷；超出整帧丢弃
inline constexpr std::size_t kDedupWindow = 64u;             // 命令 seq 幂等窗口
// 整包上限直接用 net::kMaxPacketBytes（1200，ADR-009），本模块不再起同值别名。

enum class ValidateReason : uint8_t {
  kOk = 0,
  kPacketTooLarge,    // >1200 B：oversized
  kPayloadTooLarge,   // >64 B：malformed
  kUnknownOpcode,     // 白名单外：malformed
  kStaleTick,         // 过期（clientTick < serverTick）：dropped
  kFutureTick,        // 未来（clientTick > serverTick）：dropped，不预支
  kDuplicateSequence, // 重复命令：幂等丢弃
};

struct ValidateResult {
  bool isOk = true;
  ValidateReason reason = ValidateReason::kOk;
};

const char* validateReasonName(ValidateReason reason) noexcept;

// 包长与 opcode
ValidateResult validatePacketSize(std::size_t bytes) noexcept;
ValidateResult validatePayloadSize(std::size_t bytes) noexcept;
bool isClientToServerType(uint8_t type) noexcept;
ValidateResult validateOpcode(uint8_t type) noexcept;

// §5 字段表：tick 早于服务器当前 tick 者过期丢弃；晚于者丢弃（不重放、不预支）
ValidateResult validateClientTick(uint32_t clientTick, uint32_t serverTick) noexcept;

// §5 字段表：NaN/±Inf 归零；moveX/moveY 夹到 [-1,1]；yaw 取模到 [-π,π]；pitch 夹到 [-π/2,π/2]；
// switchTo 不在 {0,1,2} 时归 0；buttons 恒 & 0xFF（u8 上为恒等，保留位域契约）。
// yaw 取模复用 S02 的 core::wrapAngle（§5.1 冻结左开右闭 (-π, π]），不另写 std::fmod 版本。
struct ClampReport {
  bool isMoveXClamped = false;
  bool isMoveYClamped = false;
  bool isYawWrapped = false;
  bool isPitchClamped = false;
  bool isSwitchToReset = false;
  bool isNonFinite = false;

  bool hasAnyAdjustment() const noexcept {
    return isMoveXClamped || isMoveYClamped || isYawWrapped || isPitchClamped || isSwitchToReset ||
           isNonFinite;
  }
};

ac::sim::Command sanitizeCommand(const ac::sim::Command& raw, ClampReport& report) noexcept;

// 命令 seq 幂等：返回 true = 重复（重传），调用方不得重复应用。
// 线上重复包由 S04 的 msgId ack 位图去重（net/reliability.hpp）；这里防的是「同一命令被应用两次」。
struct CommandDedup {
  uint16_t recent[kDedupWindow] = {};
  uint8_t count = 0u;
  uint8_t next = 0u;
};

bool noteCommandSequence(CommandDedup& dedup, uint16_t seq) noexcept;
void resetCommandDedup(CommandDedup& dedup) noexcept;

// 计数接线（registry 可为 nullptr = 只判定不计数）
void noteValidateFailure(ac::metrics::CounterRegistry* counters, ValidateReason reason) noexcept;

}  // namespace ac::security
