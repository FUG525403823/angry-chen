import { ENTITY } from './entity.ts';

export const PLAYER = Object.freeze({
  moveSpeed: 4.5,
  sprintSpeed: 6.3,
  jumpSpeed: 4.8,
  gravity: 14,
  maxHp: ENTITY.baseStats.player.hp,
  maxArmor: ENTITY.baseStats.player.armor,
  armorAbsorbRatio: 0.6,
  radius: ENTITY.radiusByKind.player,
  height: 1.7,
  eyeHeight: 1.6,
} as const);

export type PlayerConfig = typeof PLAYER;
