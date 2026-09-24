#include "waves/director.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "ai/sheep_brain.hpp"
#include "config/player.hpp"
#include "config/waves.hpp"
#include "core/rng.hpp"
#include "sim/arena.hpp"
#include "sim/world.hpp"

namespace ac::waves {
namespace {

// §5.4 的出生点抖动（v1 director.ts 的内联字面量）。
inline constexpr double kSpawnJitterM = 1.5;

// 返回 false 表示实体池耗尽（v1 的 !spawned.ok）：调用方丢弃本次生成、不重试。
bool spawnSheepAt(ac::sim::World& world, ac::config::SheepKind kind, double x, double z) noexcept {
  ac::sim::SpawnParams params{};
  params.kind = ac::sim::EntityKind::kSheep;
  params.pos = ac::Vec3{x, 0.0, z};
  params.team = ac::config::kDefaultTeamByKind[static_cast<std::size_t>(ac::sim::EntityKind::kSheep)];
  const ac::config::BaseStats& stats =
      ac::config::kKindBaseStats[static_cast<std::size_t>(ac::sim::EntityKind::kSheep)];
  params.hp = static_cast<double>(stats.hp);
  params.armor = stats.armor;
  const ac::sim::SpawnResult result = ac::sim::spawnEntity(world, params);
  if (!result.isOk) return false;
  ac::sim::Entity* entity = ac::sim::entityById(world, result.id);
  if (entity == nullptr) return false;
  ac::ai::applySheepKind(*entity, kind);
  entity->state = ac::config::sheepStateCode(ac::config::SheepState::kGraze);
  return true;
}

}  // namespace

DirectorState createDirectorState() noexcept { return DirectorState{}; }

int32_t planWave(DirectorState& state, int32_t wave, int32_t playerCount) noexcept {
  for (int32_t i = 0; i < kDirectorKindCount; ++i) state.plan[i] = 0;
  state.wave = wave;
  state.cursor = 0;
  state.spawned = 0;

  int32_t remaining = ac::config::waveBudget(wave, playerCount);
  const int32_t kingIndex = ac::config::sheepKindIndex(ac::config::SheepKind::kKing);
  const int32_t eliteIndex = ac::config::sheepKindIndex(ac::config::SheepKind::kElite);
  const int32_t ramIndex = ac::config::sheepKindIndex(ac::config::SheepKind::kRam);
  const int32_t gruntIndex = ac::config::sheepKindIndex(ac::config::SheepKind::kGrunt);

  if (ac::config::isBossWave(wave) && remaining >= 20) {
    state.plan[kingIndex] += 1;
    remaining -= 20;
  }
  if (wave >= 5) {
    const int32_t cap = std::min(2 + wave / 5, remaining / 6);
    if (cap > 0) {
      state.plan[eliteIndex] += cap;
      remaining -= cap * 6;
    }
  }
  if (wave >= 3) {
    const int32_t cap = std::min(remaining / 3, std::max(1, wave / 3));
    if (cap > 0) {
      state.plan[ramIndex] += cap;
      remaining -= cap * 3;
    }
  }
  if (remaining > 0) state.plan[gruntIndex] += remaining;

  state.planned = 0;
  for (int32_t i = 0; i < kDirectorKindCount; ++i) state.planned += state.plan[i];
  state.isWaveStartPending = true;
  return state.planned;
}

int32_t planCountFor(const DirectorState& state, ac::config::SheepKind kind) noexcept {
  return state.plan[ac::config::sheepIndexOf(kind)];
}

int32_t plannedBudgetSum(const DirectorState& state) noexcept {
  int32_t total = 0;
  for (int32_t i = 0; i < kDirectorKindCount; ++i) {
    total += state.plan[i] * static_cast<int32_t>(ac::config::kSheep[static_cast<std::size_t>(i)].price);
  }
  return total;
}

int32_t aliveSheepCount(const ac::sim::World& world) noexcept {
  return static_cast<int32_t>(world.stats.aliveSheep);
}

double nearestPlayerDistanceM(const ac::sim::World& world, double x, double z,
                              const uint16_t* playerIds, uint32_t playerCount) noexcept {
  double best = std::numeric_limits<double>::infinity();
  for (uint32_t i = 0; i < playerCount; ++i) {
    const ac::sim::Entity* player = ac::sim::entityById(world, playerIds[i]);
    if (player == nullptr || !player->active || player->kind != ac::sim::EntityKind::kPlayer) continue;
    const double dx = player->pos.x - x;
    const double dz = player->pos.z - z;
    const double distance = std::sqrt(dx * dx + dz * dz);
    if (distance < best) best = distance;
  }
  return best;
}

int32_t selectSpawnPointIndices(const ac::sim::World& world, const uint16_t* playerIds,
                                uint32_t playerCount, ac::Rng& rng, int32_t* out,
                                int32_t maxPoints) noexcept {
  constexpr int32_t kPointCount = static_cast<int32_t>(ac::sim::arena::kSpawnPoints.size());
  int32_t count = 0;
  const int32_t start = ac::rngInt(rng, 0, kPointCount - 1);
  for (int32_t offset = 0; offset < kPointCount && count < maxPoints; ++offset) {
    const int32_t index = (start + offset) % kPointCount;
    const ac::Vec3& point = ac::sim::arena::kSpawnPoints[static_cast<std::size_t>(index)];
    if (nearestPlayerDistanceM(world, point.x, point.z, playerIds, playerCount) <=
        ac::config::kMinSpawnDistanceM) {
      continue;
    }
    out[count] = index;
    ++count;
  }
  return count;
}

DirectorTick updateDirector(ac::sim::World& world, DirectorState& state, int32_t playerCount,
                            ac::Rng& rng, const uint16_t* playerIds, uint32_t playerIdCount) noexcept {
  DirectorTick result{};
  if (state.isFinished || state.wave <= 0) return result;

  if (state.spawned < state.planned) {
    if (state.isWaveStartPending) {
      state.isWaveStartPending = false;
      ac::sim::pushEvent(world, ac::sim::kEventWaveStart, 0u, 0u, 0u, 0.0, 0.0, 0.0,
                         static_cast<double>(state.wave));
      result.isWaveStart = true;
    }

    int32_t pointScratch[ac::config::kMaxActiveSpawnPoints];
    const int32_t pointCount = selectSpawnPointIndices(world, playerIds, playerIdCount, rng, pointScratch,
                                                       ac::config::kMaxActiveSpawnPoints);
    int32_t budget = ac::config::kMaxSpawnsPerTick;
    while (budget > 0 && state.spawned < state.planned) {
      while (state.cursor < kDirectorKindCount && state.plan[state.cursor] <= 0) ++state.cursor;
      if (state.cursor >= kDirectorKindCount) break;
      const ac::config::SheepKind kind =
          ac::config::kSheepOrder[static_cast<std::size_t>(state.cursor)];
      const int32_t pointIndex = pointCount > 0 ? pointScratch[state.spawned % pointCount] : -1;
      const double baseX = pointIndex >= 0
                               ? ac::sim::arena::kSpawnPoints[static_cast<std::size_t>(pointIndex)].x
                               : 0.0;
      const double baseZ = pointIndex >= 0
                               ? ac::sim::arena::kSpawnPoints[static_cast<std::size_t>(pointIndex)].z
                               : 0.0;
      // 抽取顺序与 v1 一致：先 x 抖动，再 z 抖动。
      const double x = baseX + ac::rngRange(rng, -kSpawnJitterM, kSpawnJitterM);
      const double z = baseZ + ac::rngRange(rng, -kSpawnJitterM, kSpawnJitterM);
      if (!spawnSheepAt(world, kind, x, z)) break;  // 实体池耗尽：丢弃本次生成且不重试
      state.plan[state.cursor] -= 1;
      state.spawned += 1;
      state.totalSpawns += 1;
      result.spawned += 1;
      budget -= 1;
    }
    // 本 tick 的出生数增量记账，保持 stats.aliveSheep 与「上一次 stepWorld 结束 + 本 tick spawns」一致。
    world.stats.aliveSheep += static_cast<uint32_t>(result.spawned);
    return result;
  }

  if (aliveSheepCount(world) > 0) return result;

  ac::sim::pushEvent(world, ac::sim::kEventWaveClear, 0u, 0u, 0u, 0.0, 0.0, 0.0,
                     static_cast<double>(state.wave));
  result.isWaveClear = true;
  if (state.wave >= ac::config::kWaveMax) {
    state.isFinished = true;
    ac::sim::pushEvent(world, ac::sim::kEventMatchEnded, 0u, 0u, 0u, 0.0, 0.0, 0.0,
                       static_cast<double>(state.wave));
    result.isMatchEnd = true;
  } else {
    planWave(state, state.wave + 1, playerCount);
  }
  return result;
}

}  // namespace ac::waves
