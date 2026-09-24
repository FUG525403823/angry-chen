#pragma once
// S09 §5.1/§5.2/§5.3：四种羊形参数表、SHEEP_AI 全字段、状态码与转移表、局部常量与羊形攻击档案。
// 数值逐条抄 v1 packages/shared/src/config/sheep.ts 与 ai/sheepBrain.ts 的冻结常量，不得再调：
// configHash 的 sheep / sheep.ai / sheep.states / sheep.local / sheep.attack 五组与
// tools/export-fixtures.mjs 的 configHashText 逐字节对应（见 server/README.md §12）。
#include <array>
#include <cstddef>
#include <cstdint>

#include "config/combat.hpp"
#include "config/weapons.hpp"
#include "sim/arena.hpp"

namespace ac::config {

inline constexpr int32_t kSheepKindCount = 4;
// 定长容量（数组维度必须是编译期常量）：转移表行宽、邻居槽、仇恨槽。
inline constexpr int32_t kSheepMaxTransitions = 6;
inline constexpr int32_t kSheepMaxNeighbors = 12;
inline constexpr int32_t kSheepAggroSlots = 8;

// §5.1 羊形表：下标 = SheepKind 的线上编号（SHEEP_ORDER 的顺序 grunt/ram/elite/king）。
struct SheepDef {
  double hp;
  double speed;
  double damage;
  double price;
  double radiusM;
  double heightM;
};

inline constexpr std::array<SheepKind, kSheepKindCount> kSheepOrder{
    SheepKind::kGrunt, SheepKind::kRam, SheepKind::kElite, SheepKind::kKing};

inline constexpr std::array<SheepDef, kSheepKindCount> kSheep{{
    {60.0, 2.6, 8.0, 1.0, 0.5, 0.9},      // grunt 咩咩兵
    {140.0, 3.2, 22.0, 3.0, 0.55, 1.0},   // ram 冲撞羊
    {260.0, 2.4, 14.0, 6.0, 0.6, 1.1},    // elite 问界羊
    {2400.0, 2.0, 30.0, 20.0, 1.6, 2.4},  // king 羊王
}};

static_assert(static_cast<uint8_t>(SheepKind::kGrunt) == 0u && static_cast<uint8_t>(SheepKind::kRam) == 1u &&
                  static_cast<uint8_t>(SheepKind::kElite) == 2u && static_cast<uint8_t>(SheepKind::kKing) == 3u,
              "§5.1：羊形编号 0/1/2/3 是线上编号，也是本表的数组下标，不得改动");
static_assert(kSheep[kSheepKindCount - 1u].hp == 2400.0 && kSheep[0].price == 1.0 && kSheep[3].price == 20.0,
              "§5.1：羊形表锚点（hp 2400 / price 1、20）");

inline constexpr std::size_t sheepIndexOf(SheepKind kind) noexcept {
  return static_cast<std::size_t>(kind);
}

inline constexpr const SheepDef& sheepDefFor(SheepKind kind) noexcept { return kSheep[sheepIndexOf(kind)]; }

// §5.2 SHEEP_AI 全字段（字段顺序 = v1 字面量顺序 = configHash 的 sheep.ai 组顺序）。
struct SheepAiParams {
  double sightM;
  double attackRangeM;
  double attackCooldownMs;
  double chargeWindupMs;
  double chargeSpeedMps;
  double eliteBoltRangeM;
  double eliteBoltCooldownMs;
  double eliteKeepMinM;
  double eliteKeepMaxM;
  double boltSpeedMps;
  int32_t kingSummonCount;
  double kingSummonIntervalMs;
  double kingPhase3SpeedMultiplier;
  double kingPhase3CooldownMultiplier;
  double staggerMs;
  double chargeStaggerMs;
  double deadFadeMs;
  double knockbackVelocityMps;
  double neighborRadiusM;
  int32_t maxNeighbors;
  int32_t aggroSlots;
  double aggroDecayPerTick;
  double aggroPerHit;
  double targetSwitchRatio;
  double grazeRadiusM;
};

inline constexpr SheepAiParams kSheepAi{
    35.0,  1.4,   1200.0, 1000.0, 9.0,   25.0,  2500.0, 15.0,  25.0,  14.0,
    4,     8000.0, 1.4,    0.7,    250.0, 1000.0, 1500.0, 7.0,   3.0,   12,
    8,     0.02,   20.0,   1.5,    6.0,
};

static_assert(kSheepAi.sightM == 35.0 && kSheepAi.attackRangeM == 1.4 && kSheepAi.attackCooldownMs == 1200.0,
              "§5.2：感知与攻击参数冻结");
static_assert(kSheepAi.boltSpeedMps == 14.0 && kSheepAi.eliteBoltCooldownMs == 2500.0,
              "§5.2：问号弹参数冻结");
static_assert(kSheepAi.maxNeighbors == kSheepMaxNeighbors && kSheepAi.aggroSlots == kSheepAggroSlots,
              "§5.2：邻居/仇恨槽容量与定长数组维度必须同源");
static_assert(kSheepAi.kingSummonCount == 4 && kSheepAi.kingSummonIntervalMs == 8000.0,
              "§5.2：羊王召唤参数冻结");

// §5.3 状态码（0..12；SHEEP_STATE_NAMES 的顺序逐字对应 v1）。
enum class SheepState : uint8_t {
  kGraze = 0u,
  kAlert = 1u,
  kChase = 2u,
  kWindup = 3u,
  kCharge = 4u,
  kAttack = 5u,
  kRanged = 6u,
  kStagger = 7u,
  kDead = 8u,
  kKingPhase1 = 9u,
  kKingPhase2 = 10u,
  kKingPhase3 = 11u,
  kKingSummoning = 12u,
};

inline constexpr int32_t kSheepStateCount = 13;

inline constexpr uint8_t sheepStateCode(SheepState state) noexcept { return static_cast<uint8_t>(state); }

// §5.3 转移表逐字：kSheepTransitions[from].to[0 .. count) 是允许的目标状态；同态转移由
// canSheepTransition 直接返回 true（v1 的 from === to 短路），越界返回 false。
struct SheepTransitionRow {
  uint8_t count;
  uint8_t to[kSheepMaxTransitions];
};

inline constexpr std::array<SheepTransitionRow, kSheepStateCount> kSheepTransitions{{
    {4u, {1u, 2u, 7u, 8u, 0u, 0u}},    // 0 graze:  [1,2,7,8]
    {5u, {2u, 0u, 6u, 7u, 8u, 0u}},    // 1 alert:  [2,0,6,7,8]
    {6u, {3u, 5u, 6u, 7u, 8u, 9u}},    // 2 chase:  [3,5,6,7,8,9]
    {4u, {4u, 2u, 7u, 8u, 0u, 0u}},    // 3 windup: [4,2,7,8]
    {3u, {2u, 7u, 8u, 0u, 0u, 0u}},    // 4 charge: [2,7,8]
    {3u, {2u, 7u, 8u, 0u, 0u, 0u}},    // 5 attack: [2,7,8]
    {4u, {2u, 1u, 7u, 8u, 0u, 0u}},    // 6 ranged: [2,1,7,8]
    {3u, {2u, 8u, 9u, 0u, 0u, 0u}},    // 7 stagger:[2,8,9]
    {0u, {0u, 0u, 0u, 0u, 0u, 0u}},    // 8 dead:   []
    {4u, {4u, 10u, 7u, 8u, 0u, 0u}},   // 9 kingPhase1:[4,10,7,8]
    {5u, {4u, 12u, 11u, 7u, 8u, 0u}},  // 10 kingPhase2:[4,12,11,7,8]
    {4u, {4u, 12u, 7u, 8u, 0u, 0u}},   // 11 kingPhase3:[4,12,7,8]
    {4u, {2u, 5u, 7u, 8u, 0u, 0u}},    // 12 kingSummoning:[2,5,7,8]
}};

inline constexpr bool canSheepTransition(int32_t from, int32_t to) noexcept {
  if (from == to) return true;
  if (from < 0 || from >= kSheepStateCount || to < 0 || to >= kSheepStateCount) return false;
  const SheepTransitionRow& row = kSheepTransitions[static_cast<std::size_t>(from)];
  for (uint8_t i = 0u; i < row.count; ++i) {
    if (row.to[i] == static_cast<uint8_t>(to)) return true;
  }
  return false;
}

// §5.2 局部常量（v1 ai/sheepBrain.ts 的导出常量与内联字面量）。
inline constexpr double kSheepAlertMs = 300.0;
inline constexpr double kRamChargeTriggerM = 12.0;
inline constexpr double kRamChargeMaxMs = 1600.0;
inline constexpr double kGrazeRepickMs = 2500.0;
inline constexpr double kEliteStrafeMs = 1200.0;
inline constexpr double kEliteStrafeSpeedRatio = 0.5;
inline constexpr double kGrazeSpeedRatio = 0.4;
inline constexpr double kGrazeObstacleRadiusRatio = 0.5;
inline constexpr double kArriveSlowRadiusM = 2.0;
inline constexpr double kFlockWeightSeparation = 1.6;
inline constexpr double kFlockWeightAlignment = 0.4;
inline constexpr double kFlockWeightCohesion = 0.5;
inline constexpr double kFlockCohesionScale = 0.1;
inline constexpr double kFlockBlendRatio = 0.5;
inline constexpr double kBiteKnockbackM = 1.5;
inline constexpr double kChargeKnockbackM = 3.0;
inline constexpr double kQuestionBoltLifeMs = 3000.0;
inline constexpr double kQuestionBoltRadiusM = 0.22;
inline constexpr double kQuestionBoltSpawnHeightM = 0.6;
// v1 的视线高度口径：lamb 眼高 0.6（targeting.ts）、目标胸口 0.9（targeting.ts 与 sheepAttack.ts）。
inline constexpr double kTargetEyeHeightM = 0.6;
inline constexpr double kVictimChestHeightM = 0.9;
inline constexpr double kKingSummonRadiusM = 2.6;
inline constexpr double kKingSummonJitterM = 0.3;
inline constexpr double kKingPhase1MinRatio = 0.66;
inline constexpr double kKingPhase2MinRatio = 0.33;
// v1 的"越界/进场"口径（ai/sheepBrain.ts 的 charge 分支、ai/sheepAttack.ts 的弹道生命周期）：
// limit = halfSize - fence.thickness = 39.5，与 collideStatic 的夹取上限（39.35 - radius）不是一回事。
inline constexpr double kFieldEdgeLimitM = sim::arena::kArenaHalfSizeMeters - sim::arena::kFenceThicknessMeters;

// §5.1/§5.2：羊形攻击档案（v1 config/sheep.ts 的 SHEEP_ATTACK_PROFILE，四形只有 damage 不同）。
inline constexpr std::array<WeaponDef, kSheepKindCount> kSheepAttackProfile{{
    {8.0, 1, 0.0, false, 1, 0.0, 0.0, 1e9, 0.0, 1.0},   // grunt
    {22.0, 1, 0.0, false, 1, 0.0, 0.0, 1e9, 0.0, 1.0},  // ram
    {14.0, 1, 0.0, false, 1, 0.0, 0.0, 1e9, 0.0, 1.0},  // elite
    {30.0, 1, 0.0, false, 1, 0.0, 0.0, 1e9, 0.0, 1.0},  // king
}};

static_assert(kSheepAttackProfile[0].damage == kSheep[0].damage &&
                  kSheepAttackProfile[3].damage == kSheep[3].damage,
              "§5.1：攻击档案的伤害必须等于羊形表的 damage（v1 SHEEP_ATTACK_PROFILE 同源）");
static_assert(kFieldEdgeLimitM == 39.5, "§5.6：场外判定上限 = 40 - 0.5 = 39.5");

}  // namespace ac::config
