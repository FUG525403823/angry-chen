import { createDownedState, resetDownedState, type DownedState } from './combat/downed.ts';
import { createRageState, resetRageState, type RageState } from './combat/rage.ts';
import { createWeaponState, resetWeaponState, type WeaponState } from './combat/weapon.ts';
import { CONFIG, type WorldConfig } from './config/index.ts';
import { LIMITS } from './net/protocol.ts';
import { createVec3, type Vec3 } from './math.ts';
import { createRng, type Rng } from './rng.ts';

export type EntityId = number;
export type EntityKind = 'player' | 'sheep' | 'projectile' | 'pickup';
export type Team = 0 | 1;

export interface CombatState {
  rage: RageState;
  downed: DownedState;
  interactHeld: boolean;
  knockMs: number;
  knockVx: number;
  knockVz: number;
}

export function createCombatState(): CombatState {
  return {
    rage: createRageState(),
    downed: createDownedState(),
    interactHeld: false,
    knockMs: 0,
    knockVx: 0,
    knockVz: 0,
  };
}

export function resetCombatState(state: CombatState): void {
  state.knockMs = 0;
  state.knockVx = 0;
  state.knockVz = 0;
  resetRageState(state.rage);
  resetDownedState(state.downed);
  state.interactHeld = false;
}

import { createSheepAiState, resetSheepAiState, type SheepAiState } from './ai/sheepBrain.ts';

export interface Entity {
  readonly id: EntityId;
  active: boolean;
  kind: EntityKind;
  pos: Vec3;
  vel: Vec3;
  yaw: number;
  pitch: number;
  hp: number;
  maxHp: number;
  armor: number;
  state: number;
  team: Team;
  ownerId: EntityId;
  aliveMs: number;
  idle: boolean;
  /**
   * 本 tick 内由权威模拟自身（击退 / 实体分离 / 静态碰撞外推）产生的水平位移，单位 m。
   * sim 累加，服务器姿态校验读取后清零；不入快照、不上行。
   */
  derivedMoveX: number;
  derivedMoveZ: number;
  weapon: WeaponState;
  combat: CombatState;
  ai: SheepAiState;
}

export interface SimEvent {
  type: string;
  tick: number;
  flags: number;
  subjectId: EntityId;
  targetId: EntityId;
  x: number;
  y: number;
  z: number;
  value: number;
}

export type SpawnFailureReason = 'entity-pool-exhausted';

const defaultTeamByKind: Record<EntityKind, Team> = {
  player: 0,
  sheep: 1,
  projectile: 0,
  pickup: 0,
};

export type SpawnResult = { ok: true; id: EntityId } | { ok: false; reason: SpawnFailureReason };

/** 每 tick 由 stepWorld 写一次的统计量；director 与 metrics 只读，避免重复全量遍历。 */
export interface WorldStats {
  aliveSheep: number;
  /** 事件池打满后丢弃的事件数（累计）。 */
  eventsDropped: number;
}

export interface World {
  readonly seed: number;
  readonly config: WorldConfig;
  readonly entities: Entity[];
  readonly activeIds: EntityId[];
  readonly freeStack: EntityId[];
  readonly events: SimEvent[];
  /** 事件对象池（容量 LIMITS.eventPoolSize）：仅在当 tick 内有效，pushEvent 会覆写池对象。 */
  readonly eventPool: SimEvent[];
  eventCursor: number;
  readonly stats: WorldStats;
  readonly scratchEntities: Entity[];
  readonly rng: Readonly<{ ai: Rng; spawn: Rng; fx: Rng }>;
  readonly liveCount: number;
  tick: number;
  timeMs: number;
}

export function createEventPool(size: number): SimEvent[] {
  const pool: SimEvent[] = [];
  for (let i = 0; i < size; i += 1) pool.push(createSimEvent());
  return pool;
}

export function createSimEvent(): SimEvent {
  return { type: '', tick: 0, flags: 0, subjectId: 0, targetId: 0, x: 0, y: 0, z: 0, value: 0 };
}

export function createEntity(id: EntityId): Entity {
  return {
    id,
    active: false,
    kind: 'player',
    pos: createVec3(),
    vel: createVec3(),
    yaw: 0,
    pitch: 0,
    hp: 0,
    maxHp: 0,
    armor: 0,
    state: 0,
    team: 0,
    ownerId: 0,
    aliveMs: 0,
    idle: false,
    derivedMoveX: 0,
    derivedMoveZ: 0,
    weapon: createWeaponState(),
    combat: createCombatState(),
    ai: createSheepAiState(),
  };
}

export function createWorld(seed: number, config: WorldConfig = CONFIG): World {
  const capacity = config.entity.maxEntities;
  const entities: Entity[] = [];
  for (let i = 0; i < capacity; i += 1) entities.push(createEntity(i + 1));

  const freeStack: EntityId[] = [];
  for (let id = capacity; id >= 1; id -= 1) freeStack.push(id);

  const activeIds: EntityId[] = [];

  const world: World = {
    seed,
    config,
    entities,
    activeIds,
    freeStack,
    events: [],
    eventPool: createEventPool(LIMITS.eventPoolSize),
    eventCursor: 0,
    stats: { aliveSheep: 0, eventsDropped: 0 },
    scratchEntities: [],
    rng: { ai: createRng(seed, 'ai'), spawn: createRng(seed, 'spawn'), fx: createRng(seed, 'fx') },
    get liveCount(): number {
      return activeIds.length;
    },
    tick: 0,
    timeMs: 0,
  };

  const spawnPoints = config.arena.playerSpawnPoints;
  for (let i = 0; i < spawnPoints.length; i += 1) {
    const point = spawnPoints[i];
    if (point === undefined) continue;
    spawnEntity(world, 'player', point.x, 0, point.z);
  }

  return world;
}

export function getEntity(world: World, id: EntityId): Entity | undefined {
  if (!Number.isInteger(id) || id < 1 || id > world.entities.length) return undefined;
  return world.entities[id - 1];
}

function insertActiveId(world: World, id: EntityId): void {
  const ids = world.activeIds;
  let index = ids.length;
  while (index > 0 && (ids[index - 1] ?? 0) > id) index -= 1;
  ids.splice(index, 0, id);
}

export function spawnEntity(
  world: World,
  kind: EntityKind,
  x = 0,
  y = 0,
  z = 0,
  team: Team = defaultTeamByKind[kind],
  ownerId: EntityId = 0,
): SpawnResult {
  const id = world.freeStack.pop();
  if (id === undefined) return { ok: false, reason: 'entity-pool-exhausted' };
  const entity = world.entities[id - 1];
  if (entity === undefined) return { ok: false, reason: 'entity-pool-exhausted' };

  const stats = world.config.entity.baseStats[kind];
  entity.active = true;
  entity.kind = kind;
  entity.pos.x = x;
  entity.pos.y = y;
  entity.pos.z = z;
  entity.vel.x = 0;
  entity.vel.y = 0;
  entity.vel.z = 0;
  entity.yaw = 0;
  entity.pitch = 0;
  entity.hp = stats.hp;
  entity.maxHp = stats.hp;
  entity.armor = stats.armor;
  entity.state = 0;
  entity.team = team;
  entity.ownerId = ownerId;
  entity.aliveMs = 0;
  entity.idle = false;
  entity.derivedMoveX = 0;
  entity.derivedMoveZ = 0;
  resetWeaponState(entity.weapon);
  resetCombatState(entity.combat);
  resetSheepAiState(entity.ai);
  insertActiveId(world, id);
  return { ok: true, id };
}

export function despawnEntity(world: World, id: EntityId): boolean {
  const entity = getEntity(world, id);
  if (entity === undefined || !entity.active) return false;
  entity.active = false;
  entity.hp = 0;
  entity.armor = 0;
  entity.ownerId = 0;
  const index = world.activeIds.indexOf(id);
  if (index >= 0) world.activeIds.splice(index, 1);
  world.freeStack.push(id);
  return true;
}

export function countActive(world: World, kind?: EntityKind): number {
  const ids = world.activeIds;
  if (kind === undefined) return ids.length;
  let count = 0;
  for (let i = 0; i < ids.length; i += 1) {
    const entity = getEntity(world, ids[i] ?? 0);
    if (entity !== undefined && entity.kind === kind) count += 1;
  }
  return count;
}

export function radiusOf(world: World, kind: EntityKind): number {
  return world.config.entity.radiusByKind[kind];
}
