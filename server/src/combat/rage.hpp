#pragma once
// S08 §5.7：怒气四态（累积 / 闲置衰减 / 激活 / 到期），逐位对齐 v1 combat/rage.ts。
#include <cstdint>

namespace ac::combat {

struct RageState {
  double value = 0.0;
  double endsAtMs = 0.0;
  double lastCombatAtMs = 0.0;
};

void createRageState(RageState& state) noexcept;
void resetRageState(RageState& state) noexcept;
bool isRageActive(const RageState& state, double nowMs) noexcept;
double rageRatio(const RageState& state) noexcept;
double rageSecondsLeft(const RageState& state, double nowMs) noexcept;
void noteCombat(RageState& state, double nowMs) noexcept;
double addKillRage(RageState& state, bool isElite, bool isHeadshot, double nowMs) noexcept;
bool activateRage(RageState& state, double nowMs) noexcept;
bool updateRage(RageState& state, double nowMs, double dtMs) noexcept;

}  // namespace ac::combat
