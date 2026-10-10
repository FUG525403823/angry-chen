#pragma once
// S08 §5.5/§9：伤害公式（护甲先扣）与部位倍率。
#include "config/combat.hpp"
#include "config/weapons.hpp"

namespace ac::combat {

struct DamageResult {
  double hpDamage = 0.0;
  double armorDamage = 0.0;
  bool isHeadshot = false;
};

// §9 稳定接口：纯函数，逐位对齐 v1 combat/damage.ts computeDamage。
// S16：追加 damageMultiplier（升级乘数，默认 1.0）。乘在 weapon.damage 之后：
// 乘数为 1.0 时位等价于 v1（x * 1.0 == x），对拍向量不受影响。
DamageResult computeDamage(const ac::config::WeaponDef& weapon, ac::config::HitPart part, double distanceM,
                           bool isRage, double victimArmor, DamageResult& out,
                           double damageMultiplier = 1.0) noexcept;

}  // namespace ac::combat
