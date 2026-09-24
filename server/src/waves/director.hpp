#pragma once
// S09 §5.4：WaveDirector —— 预算组队（planWave）、按 tick 节流生成、清波与下一波推进，
// 以及 waveStart / waveClear / matchEnded 三类事件。状态由调用方持有（S10 的房间/对局流程），
// 与 v1 ai/director.ts 的 DirectorState 同形。
#include <cstdint>

#include "config/sheep.hpp"
#include "core/rng.hpp"

namespace ac::sim {
struct World;
}  // namespace ac::sim

namespace ac::waves {

inline constexpr int32_t kDirectorKindCount = ac::config::kSheepKindCount;

struct DirectorState {
  int32_t wave = 0;
  int32_t planned = 0;
  int32_t spawned = 0;
  int32_t plan[kDirectorKindCount]{};
  int32_t cursor = 0;
  bool isWaveStartPending = false;
  bool isFinished = false;
  int32_t totalSpawns = 0;
};

struct DirectorTick {
  int32_t spawned = 0;
  // 每 tick 的事件标志（工程约定 §6：布尔用 is 前缀）。
  bool isWaveStart = false;
  bool isWaveClear = false;
  bool isMatchEnd = false;
};

DirectorState createDirectorState() noexcept;

// §5.4 组队：羊王（每 5 波且预算 >= 20）→ 问界羊（w >= 5）→ 冲撞羊（w >= 3）→ 咩咩兵填满预算。
// 返回本波计划总数（planned）。
int32_t planWave(DirectorState& state, int32_t wave, int32_t playerCount) noexcept;

int32_t planCountFor(const DirectorState& state, ac::config::SheepKind kind) noexcept;

// 计划里的价格总和（1/3/6/20），用于断言"不超预算"。
int32_t plannedBudgetSum(const DirectorState& state) noexcept;

// v1：stepWorld 每 tick 全量数一次羊并写 world.stats.aliveSheep，这里 O(1) 读。
int32_t aliveSheepCount(const ac::sim::World& world) noexcept;

// §5.4 的出生原语：波次导演与 S14 的运行时负载补羊共用这一份实现（不另起第二套生成路径）。
bool spawnSheepAt(ac::sim::World& world, ac::config::SheepKind kind, double x, double z) noexcept;

double nearestPlayerDistanceM(const ac::sim::World& world, double x, double z,
                              const uint16_t* playerIds, uint32_t playerCount) noexcept;

// 从随机起点绕环取出生点，跳过最近玩家 <= 15m 的点；返回写入 out 的个数（<= maxPoints）。
int32_t selectSpawnPointIndices(const ac::sim::World& world, const uint16_t* playerIds,
                                uint32_t playerCount, ac::Rng& rng, int32_t* out,
                                int32_t maxPoints) noexcept;

// 每 tick 调用一次；本 tick 的生成/开局/清波/终局结果写入 DirectorTick。
DirectorTick updateDirector(ac::sim::World& world, DirectorState& state, int32_t playerCount,
                            ac::Rng& rng, const uint16_t* playerIds, uint32_t playerIdCount) noexcept;

}  // namespace ac::waves
