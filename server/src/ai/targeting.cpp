#include "ai/targeting.hpp"

#include <cmath>

#include "combat/raycast.hpp"
#include "config/player.hpp"
#include "sim/world.hpp"

namespace ac::ai {
namespace {

// §5.7 局部字面量（v1 targeting.ts：仇恨衰减归零阈值 0.01）。
inline constexpr double kAggroZeroThreshold = 0.01;

}  // namespace

int32_t aggroIndexOf(const uint16_t* playerIds, uint32_t playerCount, uint16_t pid) noexcept {
  for (uint32_t i = 0; i < playerCount && i < static_cast<uint32_t>(ac::config::kSheepAggroSlots); ++i) {
    if (playerIds[i] == pid) return static_cast<int32_t>(i);
  }
  return -1;
}

void decayAggro(TargetState& state, int32_t playerCount) noexcept {
  const int32_t slots = playerCount < ac::config::kSheepAggroSlots ? playerCount : ac::config::kSheepAggroSlots;
  const double decay = 1.0 - ac::config::kSheepAi.aggroDecayPerTick;
  for (int32_t i = 0; i < slots; ++i) {
    const double value = state.aggro[i] * decay;
    state.aggro[i] = value < kAggroZeroThreshold ? 0.0 : value;
  }
}

void noteHit(TargetState& state, const uint16_t* playerIds, uint32_t playerCount, uint16_t pid) noexcept {
  const int32_t index = aggroIndexOf(playerIds, playerCount, pid);
  if (index < 0) return;
  state.aggro[index] = state.aggro[index] + ac::config::kSheepAi.aggroPerHit;
}

bool isVisible(double fromX, double fromY, double fromZ, const ac::sim::Entity& target) noexcept {
  if (target.kind != ac::sim::EntityKind::kPlayer) return true;
  const double dx = target.pos.x - fromX;
  const double dz = target.pos.z - fromZ;
  const double dy = target.pos.y + ac::config::kVictimChestHeightM - fromY;
  const double distance = std::sqrt(dx * dx + dy * dy + dz * dz);
  if (distance <= 1e-6) return true;
  // 与 v1 的补丁口径一致：两个反三角换成 S02 的整数表后直接变回弧度（不做 wrapAngle；见 README §12）。
  const double yaw = ac::radiansFromUnits(ac::angleUnitsFromVector(dx, dz));
  const double pitch = ac::radiansFromUnits(ac::angleUnitsFromRatio(dy / distance));
  const ac::Vec3 direction = ac::yawPitchToDirection(yaw, pitch);
  const ac::config::ArenaConfig& arena = ac::config::kArenaConfig;
  ac::combat::RayHit hit{};
  ac::combat::clearRayHit(hit);
  ac::combat::rayVsAabb(fromX, fromY, fromZ, direction.x, direction.y, direction.z, arena.barnMin.x,
                        arena.barnMin.y, arena.barnMin.z, arena.barnMax.x, arena.barnMax.y,
                        arena.barnMax.z, distance, hit);
  return !hit.hit;
}

uint16_t selectTarget(TargetState& state, const ac::sim::World& world, const ac::sim::Entity& self,
                      const uint16_t* playerIds, uint32_t playerCount) noexcept {
  const ac::sim::Entity* current =
      state.targetId != 0u ? ac::sim::entityById(world, state.targetId) : nullptr;
  const int32_t currentIndex = aggroIndexOf(playerIds, playerCount, state.targetId);
  const double currentAggro = currentIndex < 0 ? 0.0 : state.aggro[currentIndex];
  const bool currentValid = current != nullptr && current->active &&
                            current->kind == ac::sim::EntityKind::kPlayer && current->hp > 0.0;
  uint16_t bestId = currentValid ? state.targetId : 0u;
  double bestScore = currentValid ? currentAggro * ac::config::kSheepAi.targetSwitchRatio : -1.0;
  double bestDistance = state.targetDistanceM;
  const double eyeY = self.pos.y + ac::config::kTargetEyeHeightM;

  for (uint32_t i = 0; i < playerCount; ++i) {
    const uint16_t pid = playerIds[i];
    const ac::sim::Entity* player = ac::sim::entityById(world, pid);
    if (player == nullptr || !player->active || player->kind != ac::sim::EntityKind::kPlayer ||
        player->hp <= 0.0) {
      continue;
    }
    const double dx = player->pos.x - self.pos.x;
    const double dz = player->pos.z - self.pos.z;
    const double distance = std::sqrt(dx * dx + dz * dz);
    if (distance > ac::config::kSheepAi.sightM) continue;
    if (pid != state.targetId && !isVisible(self.pos.x, eyeY, self.pos.z, *player)) continue;
    if (pid == state.targetId) {
      bestDistance = distance;
      continue;
    }
    const double aggro = i < static_cast<uint32_t>(ac::config::kSheepAggroSlots) ? state.aggro[i] : 0.0;
    const double score = aggro + 1.0 / (1.0 + distance);
    if (score <= bestScore) continue;
    bestScore = score;
    bestId = pid;
    bestDistance = distance;
  }

  state.targetId = bestId;
  state.targetDistanceM = bestDistance;
  return bestId;
}

}  // namespace ac::ai
