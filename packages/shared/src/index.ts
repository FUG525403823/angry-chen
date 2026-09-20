export const PROTOCOL_VERSION = 1 as const;

export const SERVER_TICK_MS = 50 as const;

export {
  ARENA,
  BUTTON,
  BUTTON_MASK_ALL,
  CONFIG,
  ENTITY,
  INPUT,
  MAX_ENTITIES,
  NET,
  PLAYER,
  SNAPSHOT_FLAG,
  SNAPSHOT_MAX_ENTITIES,
} from './config/index.ts';
export type {
  ArenaConfig,
  EntityConfig,
  InputConfig,
  NetConfig,
  PlayerConfig,
  WorldConfig,
} from './config/index.ts';

export {
  WEAPON_PISTOL,
  WEAPON_RIFLE,
  WEAPON_SHOTGUN,
  createCommand,
  sanitizeCommand,
} from './command.ts';
export type { Command, CommandInput } from './command.ts';

export {
  TWO_PI,
  addScaled,
  approxEq,
  clamp,
  clamp01,
  copyVec3,
  createVec3,
  forwardFromYaw,
  length,
  lengthSq,
  normalize,
  rightFromYaw,
  setVec3,
  wrapAngle,
  yawPitchToDirection,
} from './math.ts';
export type { Vec3 } from './math.ts';

export { createRng, mulberry32, rngFloat, rngInt, rngRange } from './rng.ts';
export type { Rng, RngStream } from './rng.ts';

export {
  countActive,
  createEntity,
  createWorld,
  despawnEntity,
  getEntity,
  radiusOf,
  spawnEntity,
} from './world.ts';
export type {
  Entity,
  EntityId,
  EntityKind,
  SimEvent,
  SpawnFailureReason,
  SpawnResult,
  Team,
  World,
} from './world.ts';

export {
  computeEntityFlags,
  createSnapshot,
  createSnapshotEntity,
  snapshotWorld,
} from './snapshot.ts';
export type { Snapshot, SnapshotEntity } from './snapshot.ts';

export { stepWorld } from './sim.ts';
