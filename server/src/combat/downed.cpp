#include "combat/downed.hpp"

#include "config/combat.hpp"

namespace ac::combat {

void createDownedState(DownedState& state) noexcept {
  resetDownedState(state);
}

void resetDownedState(DownedState& state) noexcept {
  state.downed = false;
  state.reviveProgressMs = 0.0;
  state.reviverId = 0u;
  state.resetAtMs = 0.0;
  state.lastEventRatio = 0.0;
}

void markDowned(DownedState& state, double nowMs) noexcept {
  state.downed = true;
  state.reviveProgressMs = 0.0;
  state.reviverId = 0u;
  state.resetAtMs = 0.0;
  state.lastEventRatio = 0.0;
  (void)nowMs;
}

double reviveRatio(const DownedState& state) noexcept {
  const double ratio = state.reviveProgressMs / ac::config::kReviveDurationMs;
  return ratio > 1.0 ? 1.0 : ratio;
}

bool canBeRevived(const DownedState& state) noexcept {
  return state.downed;
}

ReviveOutcome reviveStep(DownedState& state, double nowMs, double dtMs, EntityId reviverId,
                         bool reviverAllowed) noexcept {
  if (!state.downed) return ReviveOutcome::kIdle;

  if (reviverAllowed && reviverId != 0u) {
    state.reviverId = reviverId;
    state.resetAtMs = 0.0;
    state.reviveProgressMs += dtMs;
    if (state.reviveProgressMs >= ac::config::kReviveDurationMs) {
      state.reviveProgressMs = ac::config::kReviveDurationMs;
      return ReviveOutcome::kDone;
    }
    return ReviveOutcome::kProgress;
  }

  if (state.reviverId != 0u) {
    state.reviverId = 0u;
    state.resetAtMs = nowMs + ac::config::kReviveResetDelayMs;
    return ReviveOutcome::kInterrupted;
  }

  if (state.resetAtMs != 0.0 && nowMs >= state.resetAtMs) {
    state.reviveProgressMs = 0.0;
    state.lastEventRatio = 0.0;
    state.resetAtMs = 0.0;
    return ReviveOutcome::kInterrupted;
  }
  return ReviveOutcome::kIdle;
}

double reviveTo(DownedState& state, double maxHp, double ratio) noexcept {
  state.downed = false;
  state.reviveProgressMs = 0.0;
  state.reviverId = 0u;
  state.resetAtMs = 0.0;
  state.lastEventRatio = 0.0;
  return maxHp * ratio;
}

}  // namespace ac::combat
