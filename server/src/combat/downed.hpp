#pragma once
// S08 §5.7：倒地与救援进度状态机（idle / progress / done / interrupted），逐位对齐 v1 combat/downed.ts。
#include <cstdint>

namespace ac::combat {

using EntityId = uint16_t;

enum class ReviveOutcome : uint8_t { kIdle = 0u, kProgress = 1u, kDone = 2u, kInterrupted = 3u };

struct DownedState {
  bool downed = false;
  double reviveProgressMs = 0.0;
  EntityId reviverId = 0u;
  double resetAtMs = 0.0;
  double lastEventRatio = 0.0;
};

void createDownedState(DownedState& state) noexcept;
void resetDownedState(DownedState& state) noexcept;
void markDowned(DownedState& state, double nowMs) noexcept;
double reviveRatio(const DownedState& state) noexcept;
bool canBeRevived(const DownedState& state) noexcept;
ReviveOutcome reviveStep(DownedState& state, double nowMs, double dtMs, EntityId reviverId,
                         bool reviverAllowed) noexcept;
// 复苏：清空救援状态并返回新的生命值（v1 reviveTo：hp = maxHp * ratio）。
double reviveTo(DownedState& state, double maxHp, double ratio) noexcept;

}  // namespace ac::combat
