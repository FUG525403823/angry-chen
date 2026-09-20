import { SNAPSHOT_FLAG } from './config/index.ts';
import { clamp01 } from './math.ts';
import { getEntity, type Entity, type EntityId, type EntityKind, type World } from './world.ts';

export interface SnapshotEntity {
  id: EntityId;
  kind: EntityKind;
  flags: number;
  x: number;
  y: number;
  z: number;
  yaw: number;
  pitch: number;
  hpRatio: number;
  state: number;
}

export interface Snapshot {
  tick: number;
  serverTimeMs: number;
  truncated: boolean;
  readonly entities: SnapshotEntity[];
}

export function createSnapshotEntity(): SnapshotEntity {
  return {
    id: 0,
    kind: 'player',
    flags: 0,
    x: 0,
    y: 0,
    z: 0,
    yaw: 0,
    pitch: 0,
    hpRatio: 0,
    state: 0,
  };
}

export function createSnapshot(): Snapshot {
  return { tick: 0, serverTimeMs: 0, truncated: false, entities: [] };
}

const pools = new WeakMap<Snapshot, SnapshotEntity[]>();

function poolOf(out: Snapshot, capacity: number): SnapshotEntity[] {
  const existing = pools.get(out);
  if (existing !== undefined) return existing;
  const pool: SnapshotEntity[] = [];
  for (let i = 0; i < capacity; i += 1) pool.push(createSnapshotEntity());
  pools.set(out, pool);
  return pool;
}

export function computeEntityFlags(entity: Entity): number {
  let flags = 0;
  if (entity.kind === 'player' && entity.hp <= 0) flags |= SNAPSHOT_FLAG.downed;
  if (entity.kind === 'player' && entity.idle) flags |= SNAPSHOT_FLAG.idle;
  return flags;
}

function fill(target: SnapshotEntity, entity: Entity): void {
  target.id = entity.id;
  target.kind = entity.kind;
  target.flags = computeEntityFlags(entity);
  target.x = entity.pos.x;
  target.y = entity.pos.y;
  target.z = entity.pos.z;
  target.yaw = entity.yaw;
  target.pitch = entity.pitch;
  target.hpRatio = entity.maxHp > 0 ? clamp01(entity.hp / entity.maxHp) : 0;
  target.state = entity.state;
}

function distanceSqTo(x: number, z: number, entity: Entity): number {
  const dx = entity.pos.x - x;
  const dz = entity.pos.z - z;
  return dx * dx + dz * dz;
}

function playersCentroid(world: World): { x: number; z: number } {
  const ids = world.activeIds;
  let sumX = 0;
  let sumZ = 0;
  let count = 0;
  for (let i = 0; i < ids.length; i += 1) {
    const entity = getEntity(world, ids[i] ?? 0);
    if (entity === undefined || !entity.active || entity.kind !== 'player') continue;
    sumX += entity.pos.x;
    sumZ += entity.pos.z;
    count += 1;
  }
  if (count === 0) return { x: 0, z: 0 };
  return { x: sumX / count, z: sumZ / count };
}

function collectNearest(world: World, capacity: number): void {
  const anchor = playersCentroid(world);
  const ids = world.activeIds;
  const scratch = world.scratchEntities;
  scratch.length = 0;
  for (let i = 0; i < ids.length; i += 1) {
    const entity = getEntity(world, ids[i] ?? 0);
    if (entity === undefined || !entity.active) continue;
    scratch.push(entity);
  }
  scratch.sort((a, b) => {
    const da = distanceSqTo(anchor.x, anchor.z, a);
    const db = distanceSqTo(anchor.x, anchor.z, b);
    if (da !== db) return da - db;
    return a.id - b.id;
  });
  scratch.length = Math.min(scratch.length, capacity);
  scratch.sort((a, b) => a.id - b.id);
}

export function snapshotWorld(world: World, out: Snapshot = createSnapshot()): Snapshot {
  const pool = poolOf(out, world.config.net.snapshotMaxEntities);
  const capacity = pool.length;
  const ids = world.activeIds;
  const truncated = ids.length > capacity;
  let count = 0;

  if (truncated) {
    collectNearest(world, capacity);
    const scratch = world.scratchEntities;
    for (let i = 0; i < scratch.length && count < capacity; i += 1) {
      const entity = scratch[i];
      const target = pool[count];
      if (entity === undefined || target === undefined) continue;
      fill(target, entity);
      count += 1;
    }
  } else {
    for (let i = 0; i < ids.length && count < capacity; i += 1) {
      const entity = getEntity(world, ids[i] ?? 0);
      const target = pool[count];
      if (entity === undefined || !entity.active || target === undefined) continue;
      fill(target, entity);
      count += 1;
    }
  }

  out.tick = world.tick;
  out.serverTimeMs = world.timeMs;
  out.truncated = truncated;
  const published = out.entities;
  for (let i = 0; i < count; i += 1) {
    const target = pool[i];
    if (target === undefined) continue;
    published[i] = target;
  }
  if (published.length !== count) published.length = count;
  return out;
}
