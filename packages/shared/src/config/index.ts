import { ARENA } from './arena.ts';
import { ENTITY } from './entity.ts';
import { INPUT } from './input.ts';
import { NET } from './net.ts';
import { PLAYER } from './player.ts';

export { ARENA, type ArenaConfig } from './arena.ts';
export * from './combat.ts';
export * from './weapons.ts';
export { ENTITY, type EntityConfig } from './entity.ts';
export { INPUT, BUTTON, BUTTON_MASK_ALL, type InputConfig } from './input.ts';
export { NET, SNAPSHOT_FLAG, type NetConfig } from './net.ts';
export { PLAYER, type PlayerConfig } from './player.ts';

export const CONFIG = Object.freeze({
  arena: ARENA,
  entity: ENTITY,
  player: PLAYER,
  input: INPUT,
  net: NET,
} as const);

export const MAX_ENTITIES = ENTITY.maxEntities;

export const SNAPSHOT_MAX_ENTITIES = NET.snapshotMaxEntities;

export type WorldConfig = typeof CONFIG;
