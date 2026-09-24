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
DamageResult computeDamage(const ac::config::WeaponDef& weapon, ac::config::HitPart part, double distanceM,
                           bool isRage, double victimArmor, DamageResult& out) noexcept;

}  // namespace ac::combat
