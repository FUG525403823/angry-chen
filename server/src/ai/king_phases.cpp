#include "ai/king_phases.hpp"

#include "ai/sheep_brain.hpp"
#include "config/player.hpp"
#include "core/quantize.hpp"
#include "core/rng.hpp"
#include "sim/world.hpp"

namespace ac::ai {

int32_t kingPhaseFor(double hpRatio) noexcept {
  if (hpRatio > ac::config::kKingPhase1MinRatio) return 1;
  if (hpRatio > ac::config::kKingPhase2MinRatio) return 2;
  return 3;
}

int32_t kingStateFor(int32_t phase) noexcept {
  return phase == 1 ? static_cast<int32_t>(ac::config::SheepState::kKingPhase1)
                    : (phase == 2 ? static_cast<int32_t>(ac::config::SheepState::kKingPhase2)
                                  : static_cast<int32_t>(ac::config::SheepState::kKingPhase3));
}

double kingSpeedMultiplier(int32_t phase) noexcept {
  return phase >= 3 ? ac::config::kSheepAi.kingPhase3SpeedMultiplier : 1.0;
}

double kingAttackCooldownMs(int32_t phase) noexcept {
  return phase >= 3 ? ac::config::kSheepAi.attackCooldownMs * ac::config::kSheepAi.kingPhase3CooldownMultiplier
                    : ac::config::kSheepAi.attackCooldownMs;
}

int32_t summonGrunts(ac::sim::World& world, ac::sim::Entity& king) noexcept {
  int32_t spawned = 0;
  const int32_t count = ac::config::kSheepAi.kingSummonCount;
  for (int32_t i = 0; i < count; ++i) {
    const double angle = ac::kTwoPi * static_cast<double>(i) / static_cast<double>(count);
    const double units = ac::quantizeAngle(angle);
    // 抽取顺序与 v1 逐字一致：先 x 抖动，再 z 抖动（JS 的实参从左到右求值）。
    const double jitterX = ac::rngRange(world.rng.spawn, -ac::config::kKingSummonJitterM,
                                        ac::config::kKingSummonJitterM);
    const double jitterZ = ac::rngRange(world.rng.spawn, -ac::config::kKingSummonJitterM,
                                        ac::config::kKingSummonJitterM);
    const double x = king.pos.x + ac::cosUnits(units) * ac::config::kKingSummonRadiusM + jitterX;
    const double z = king.pos.z + ac::sinUnits(units) * ac::config::kKingSummonRadiusM + jitterZ;

    ac::sim::SpawnParams params{};
    params.kind = ac::sim::EntityKind::kSheep;
    params.pos = ac::Vec3{x, king.pos.y, z};
    params.team = ac::config::kDefaultTeamByKind[static_cast<std::size_t>(ac::sim::EntityKind::kSheep)];
    params.hp = static_cast<double>(ac::config::kKindBaseStats[static_cast<std::size_t>(ac::sim::EntityKind::kSheep)].hp);
    params.armor = ac::config::kKindBaseStats[static_cast<std::size_t>(ac::sim::EntityKind::kSheep)].armor;
    const ac::sim::SpawnResult result = ac::sim::spawnEntity(world, params);
    if (!result.isOk) continue;
    ac::sim::Entity* grunt = ac::sim::entityById(world, result.id);
    if (grunt == nullptr) continue;
    applySheepKind(*grunt, ac::config::SheepKind::kGrunt);
    ++spawned;
  }
  king.ai.summoned += spawned;
  return spawned;
}

int32_t updateKing(ac::sim::World& world, ac::sim::Entity& king, uint32_t dtMs) noexcept {
  const double ratio = king.maxHp > 0.0 ? king.hp / king.maxHp : 0.0;
  const int32_t phase = kingPhaseFor(ratio);
  int32_t spawned = 0;
  if (phase != king.ai.phase) {
    king.ai.phase = phase;
    setSheepState(king, kingStateFor(phase));
    king.ai.summonMs = phase >= 2 ? ac::config::kSheepAi.kingSummonIntervalMs : 0.0;
  }
  if (phase >= 2) {
    king.ai.summonMs -= static_cast<double>(dtMs);
    if (king.ai.summonMs <= 0.0) {
      king.ai.summonMs = ac::config::kSheepAi.kingSummonIntervalMs;
      spawned = summonGrunts(world, king);
    }
  }
  return spawned;
}

}  // namespace ac::ai

namespace ac::sim {

// S06 冻结的签名是 int updateKing(World&, Entity&, uint32_t)：S09 实现落在 ac::ai，这里做一层同名转发。
int updateKing(World& world, Entity& king, uint32_t dtMs) noexcept {
  return ac::ai::updateKing(world, king, dtMs);
}

int32_t updateKings(World& world, uint32_t dtMs) noexcept {
  int32_t spawned = 0;
  for (std::size_t i = 0u; i < world.activeCount; ++i) {
    Entity* found = entityById(world, world.activeIds[i]);
    if (found == nullptr || !found->active || found->kind != EntityKind::kSheep) continue;
    if (found->sheepKind != static_cast<uint8_t>(ac::config::SheepKind::kKing)) continue;
    spawned += ac::ai::updateKing(world, *found, dtMs);
  }
  return spawned;
}

}  // namespace ac::sim
