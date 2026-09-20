import {
  ARMOR_ABSORB_RATIO,
  HIT_PART,
  FALLOFF_MIN_MULTIPLIER,
  RAGE,
  partMultiplier,
  type HitPart,
} from '../config/combat.ts';
import type { WeaponDef } from '../config/weapons.ts';

export interface DamageResult {
  hpDamage: number;
  armorDamage: number;
  isHeadshot: boolean;
}

export function createDamageResult(): DamageResult {
  return { hpDamage: 0, armorDamage: 0, isHeadshot: false };
}

export function computeDamage(
  weapon: WeaponDef,
  part: HitPart,
  distanceM: number,
  isRage: boolean,
  victimArmor: number,
  out: DamageResult,
): DamageResult {
  const fell = 1 - weapon.falloffPerM * Math.max(0, distanceM - weapon.falloffStartM);
  const falloff = fell < FALLOFF_MIN_MULTIPLIER ? FALLOFF_MIN_MULTIPLIER : fell;
  const base =
    weapon.damage *
    partMultiplier(part, weapon.headshotMultiplier) *
    falloff *
    (isRage ? RAGE.damageMultiplier : 1);

  out.armorDamage = victimArmor > 0 ? base * ARMOR_ABSORB_RATIO : 0;
  out.hpDamage = base - out.armorDamage;
  out.isHeadshot = part === HIT_PART.head;
  return out;
}

export function damageFor(
  weapon: WeaponDef,
  part: HitPart,
  distanceM: number,
  isRage: boolean,
  victimArmor: number,
): DamageResult {
  return computeDamage(weapon, part, distanceM, isRage, victimArmor, createDamageResult());
}
