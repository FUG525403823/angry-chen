#pragma once
// S06 §5.4/§5.5：玩家、实体与场地的冻结常量表（抄 v1 player.ts / entity.ts / arena.ts）。
// 场地的唯一真值在 sim/arena.hpp（S05 已落地），这里只做引用 + 编译期锚点，不复制数值：
// 两处真值迟早漂移，而漂移在跨语言对拍里是致命的。
#include <array>
#include <cstdint>

#include "core/math.hpp"
#include "sim/arena.hpp"

namespace ac::config {

// §5.4 玩家表
inline constexpr double kMoveSpeedMps = 4.5;
inline constexpr double kSprintSpeedMps = 6.3;
inline constexpr double kJumpSpeedMps = 4.8;  // 本份不参与积分，仅保留常量（v1 竖直速度为恒 0）
inline constexpr double kGravityMps2 = 14.0;  // 同上
inline constexpr double kPlayerRadiusM = 0.4;
inline constexpr double kPlayerHeightM = 1.7;
inline constexpr double kEyeHeightM = 1.6;
inline constexpr int32_t kMaxHp = 100;
inline constexpr int32_t kMaxArmor = 50;
inline constexpr double kArmorAbsorbRatio = 0.6;

// §5.4 实体半径表
inline constexpr double kSheepRadiusM = 0.5;
inline constexpr double kProjectileRadiusM = 0.08;
inline constexpr double kPickupRadiusM = 0.35;

// §5.4/§5.5 分离参数
inline constexpr double kSeparatePrefilterRadiusM = 0.5;  // 预筛取最大半径：保守，只多算不漏算
inline constexpr double kSeparatePrefilterSqM =
    (2.0 * kSeparatePrefilterRadiusM) * (2.0 * kSeparatePrefilterRadiusM);
inline constexpr double kSeparateZeroDistanceM = 1e-6;
inline constexpr uint32_t kSeparatePasses = 1u;

// §5.5 边界夹取：limit = halfSize - thickness / 2 - radius（半径由调用方传入）
inline constexpr double kFenceHalfThicknessM = sim::arena::kFenceThicknessMeters / 2.0;

// §5.2 按钮位域（u8，写盘 & 0xFF）
inline constexpr uint8_t kButtonFire = 1u;
inline constexpr uint8_t kButtonSprint = 2u;
inline constexpr uint8_t kButtonJump = 4u;
inline constexpr uint8_t kButtonReload = 8u;
inline constexpr uint8_t kButtonInteract = 16u;
inline constexpr uint8_t kButtonRage = 32u;
inline constexpr uint8_t kButtonSwitchWeapon = 64u;
inline constexpr uint8_t kButtonReady = 128u;

// §5.6：输入域常量（v1 input.ts）——configHash 的 input 组成员
inline constexpr double kMoveAxisLimit = 1.0;
inline constexpr double kPitchLimitRad = kPi / 2.0;

// §5.6：kind -> 基础属性（v1 entity.ts baseStats，下标 = EntityKind）。
// 玩家两项与 kMaxHp/kMaxArmor 同源；其余三行的值抄 v1 entity.ts，S08/S09 落地时若计划另有数值须先改契约。
struct BaseStats {
  int32_t hp;
  int32_t armor;
};
inline constexpr std::array<BaseStats, 4> kKindBaseStats{{
    {kMaxHp, kMaxArmor},
    {60, 0},
    {1, 0},
    {1, 0},
}};
static_assert(kKindBaseStats[0].hp == kMaxHp && kKindBaseStats[0].armor == kMaxArmor,
              "§5.6：玩家基础属性与玩家表同源");
// 羊形/投射物/掉落物的 hp/armor 抄 v1 entity.ts（60/0、1/0、1/0）。这三个值今天没有第二个真实来源可
// 锚定，所以不写「把字面量再抄一遍」的 static_assert（它永远不会失败）——S09 落地羊形表时改成引用 S09 的表。
static_assert(kMoveAxisLimit == 1.0 && kPitchLimitRad == kPi / 2.0, "§5.6：输入域常量抄 v1 input.ts");

// §5.1/§5.6 固定步长；§5.1 阶段 7 的救援速度上限
inline constexpr uint32_t kStepDtMs = 50u;
inline constexpr double kRescueSpeedClampMps = 1.5;

// §5.4：kind → 半径（下标 = EntityKind 0..3，与 arena.hpp 的尺寸表同源）
inline constexpr std::array<double, 4> kKindRadiusM{
    sim::arena::kKindDimensions[0].radius,
    sim::arena::kKindDimensions[1].radius,
    sim::arena::kKindDimensions[2].radius,
    sim::arena::kKindDimensions[3].radius,
};

// §5.5：场地形状（同样只引用 sim/arena.hpp 的冻结值）
struct ArenaConfig {
  double halfSizeMeters = 0.0;
  double fenceHalfThicknessMeters = 0.0;
  double fenceHeightMeters = 0.0;
  ac::Vec3 barnMin{0.0, 0.0, 0.0};
  ac::Vec3 barnMax{0.0, 0.0, 0.0};
  std::array<ac::Vec3, 4> playerSpawns{};
};

inline constexpr ArenaConfig kArenaConfig{
    sim::arena::kArenaHalfSizeMeters,
    kFenceHalfThicknessM,
    sim::arena::kFenceHeightMeters,
    sim::arena::kBarn.min,
    sim::arena::kBarn.max,
    sim::arena::kPlayerSpawns,
};

static_assert(kMoveSpeedMps == 4.5 && kSprintSpeedMps == 6.3, "§5.4：步行/冲刺速度冻结");
static_assert(kPlayerRadiusM == sim::arena::kKindDimensions[0].radius,
              "§5.5：玩家半径与场地尺寸表必须同源");
static_assert(kSheepRadiusM == sim::arena::kKindDimensions[1].radius &&
                  kProjectileRadiusM == sim::arena::kKindDimensions[2].radius &&
                  kPickupRadiusM == sim::arena::kKindDimensions[3].radius,
              "§5.4/§5.5：实体半径表必须与 arena.hpp 同源");
static_assert(kArenaConfig.halfSizeMeters == 40.0 && kArenaConfig.fenceHalfThicknessMeters == 0.25 &&
                  kArenaConfig.barnMin.x == -4.0 && kArenaConfig.barnMax.y == 5.0,
              "§5.5：场地常量锚点");
static_assert(kStepDtMs == 50u, "§5.1：固定步长 50ms");

}  // namespace ac::config
