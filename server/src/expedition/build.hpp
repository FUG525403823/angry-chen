#pragma once
#include <cstdint>
#include "config/combat.hpp"

namespace ac::sim { struct Entity; struct World; }
namespace ac::expedition {
// Four players per room; target-owned slots isolate each shooter's marks/cooldowns.
struct TargetTrigger {
  uint16_t sourceId = 0u;
  uint32_t markUntilMs = 0u;
  uint32_t staggerReadyAtMs = 0u;
};
struct BuildState {
  bool enabled = false;
  uint8_t selectedMask = 0u;
  uint32_t pulseReadyAtMs = 0u;
  uint32_t triggerCount = 0u;
  uint32_t stationaryMs = 0u;
  uint32_t refundReadyAtMs = 0u;
  uint32_t healReadyAtMs = 0u;
  uint32_t tempoReadyAtMs = 0u;
  TargetTrigger targets[4]{};
};
void enableBuild(BuildState& state, bool enabled = true) noexcept;
void resetBuild(BuildState& state) noexcept;
bool selectCard(BuildState& state, uint8_t cardId) noexcept;
double calculateHitModifier(const ac::sim::Entity& shooter, const ac::sim::Entity& target,
                            ac::config::HitPart part, double distanceM, uint32_t nowMs) noexcept;
void onHit(ac::sim::Entity& shooter, ac::sim::Entity& target, ac::config::HitPart part,
           double distanceM, uint32_t nowMs) noexcept;
void onKill(ac::sim::Entity& shooter, const ac::sim::Entity& target, double distanceM,
            uint32_t nowMs) noexcept;
void updateBuild(ac::sim::Entity& entity, uint32_t nowMs, uint32_t dtMs) noexcept;
bool pulse(ac::sim::World& world, ac::sim::Entity& user, uint32_t nowMs) noexcept;
}  // namespace ac::expedition
