#include "combat/rage.hpp"

#include "config/combat.hpp"

namespace ac::combat {

void createRageState(RageState& state) noexcept {
  resetRageState(state);
}

void resetRageState(RageState& state) noexcept {
  state.value = 0.0;
  state.endsAtMs = 0.0;
  state.lastCombatAtMs = 0.0;
}

bool isRageActive(const RageState& state, double nowMs) noexcept {
  return state.endsAtMs > nowMs;
}

double rageRatio(const RageState& state) noexcept {
  return state.value / ac::config::kRageMax;
}

double rageSecondsLeft(const RageState& state, double nowMs) noexcept {
  return state.endsAtMs <= nowMs ? 0.0 : (state.endsAtMs - nowMs) / 1000.0;
}

void noteCombat(RageState& state, double nowMs) noexcept {
  state.lastCombatAtMs = nowMs;
}

double addKillRage(RageState& state, bool isElite, bool isHeadshot, double nowMs) noexcept {
  const double base = isElite ? ac::config::kRagePerEliteKill : ac::config::kRagePerKill;
  const double amount = isHeadshot ? base * ac::config::kRageHeadshotKillMultiplier : base;
  const double next = state.value + amount;
  state.value = next > ac::config::kRageMax ? ac::config::kRageMax : next;
  state.lastCombatAtMs = nowMs;
  return amount;
}

bool activateRage(RageState& state, double nowMs) noexcept {
  if (state.value < ac::config::kRageMax) return false;
  if (isRageActive(state, nowMs)) return false;
  state.value = 0.0;
  state.endsAtMs = nowMs + ac::config::kRageDurationMs;
  state.lastCombatAtMs = nowMs;
  return true;
}

bool updateRage(RageState& state, double nowMs, double dtMs) noexcept {
  const bool wasActive = state.endsAtMs != 0.0 && state.endsAtMs > nowMs;
  if (state.endsAtMs != 0.0 && !wasActive) state.endsAtMs = 0.0;
  if (wasActive) return false;
  if (state.value <= 0.0) return false;
  if (nowMs - state.lastCombatAtMs < ac::config::kRageIdleDecayDelayMs) return false;
  const double decayed = state.value - (ac::config::kRageDecayPerSecond * dtMs) / 1000.0;
  state.value = decayed > 0.0 ? decayed : 0.0;
  return true;
}

}  // namespace ac::combat
