#include "combat/damage.hpp"

#include "config/player.hpp"

namespace ac::combat {

DamageResult computeDamage(const ac::config::WeaponDef& weapon, ac::config::HitPart part, double distanceM,
                           bool isRage, double victimArmor, DamageResult& out) noexcept {
  const double extra = distanceM - weapon.falloffStartM;
  const double fell = 1.0 - weapon.falloffPerM * (extra > 0.0 ? extra : 0.0);
  const double falloff = fell < ac::config::kFalloffMinMultiplier ? ac::config::kFalloffMinMultiplier : fell;
  const double base = weapon.damage * ac::config::partMultiplier(part, weapon.headshotMultiplier) * falloff *
                      (isRage ? ac::config::kRageDamageMultiplier : 1.0);

  out.armorDamage = victimArmor > 0.0 ? base * ac::config::kArmorAbsorbRatio : 0.0;
  out.hpDamage = base - out.armorDamage;
  out.isHeadshot = part == ac::config::HitPart::kHead;
  return out;
}

}  // namespace ac::combat
