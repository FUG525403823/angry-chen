import { NET, SNAPSHOT_FLAG } from './config/index.ts';
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

/** 玩家质心写进模块级 scratch：每帧复用同一对象，不再返回新的字面量。 */
const centroidScratch = { x: 0, z: 0 };

function updatePlayersCentroid(world: World): void {
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
  centroidScratch.x = count === 0 ? 0 : sumX / count;
  centroidScratch.z = count === 0 ? 0 : sumZ / count;
}

/**
 * 截断路径的有界最大堆：根 = 当前最差（距离最远，同距离时 id 最大）。
 * 逐实体与根比较，只保留「距离升序、id 升序」意义下最好的 capacity 个，
 * 选完后只对这 capacity 个元素按 id 排一次（旧实现是两次全量排序 + 全量 push）。
 */
const nearEntities: Entity[] = [];
const nearDist = new Float64Array(NET.snapshotMaxEntities);
const nearIds = new Int32Array(NET.snapshotMaxEntities);
let nearCount = 0;

/** 堆元素 i 是否比 (dist, id) 更差（更差 = 距离更大；同距离时 id 更大）。 */
function nearWorse(i: number, dist: number, id: number): boolean {
  const di = nearDist[i] ?? 0;
  if (di !== dist) return di > dist;
  return (nearIds[i] ?? 0) > id;
}

function nearSwap(a: number, b: number): void {
  const ea = nearEntities[a];
  nearEntities[a] = nearEntities[b] as Entity;
  nearEntities[b] = ea as Entity;
  const da = nearDist[a] ?? 0;
  nearDist[a] = nearDist[b] ?? 0;
  nearDist[b] = da;
  const ia = nearIds[a] ?? 0;
  nearIds[a] = nearIds[b] ?? 0;
  nearIds[b] = ia;
}

function nearSiftUp(index: number): void {
  let i = index;
  while (i > 0) {
    const parent = (i - 1) >> 1;
    if (!nearWorse(i, nearDist[parent] ?? 0, nearIds[parent] ?? 0)) break;
    nearSwap(i, parent);
    i = parent;
  }
}

function nearSiftDown(index: number): void {
  let i = index;
  for (;;) {
    const left = i * 2 + 1;
    const right = left + 1;
    let worst = i;
    if (left < nearCount && nearWorse(left, nearDist[worst] ?? 0, nearIds[worst] ?? 0)) {
      worst = left;
    }
    if (right < nearCount && nearWorse(right, nearDist[worst] ?? 0, nearIds[worst] ?? 0)) {
      worst = right;
    }
    if (worst === i) return;
    nearSwap(i, worst);
    i = worst;
  }
}

function collectNearest(world: World, capacity: number): void {
  updatePlayersCentroid(world);
  const anchorX = centroidScratch.x;
  const anchorZ = centroidScratch.z;
  const ids = world.activeIds;
  const limit = Math.min(capacity, nearDist.length);
  nearCount = 0;
  for (let i = 0; i < ids.length; i += 1) {
    const entity = getEntity(world, ids[i] ?? 0);
    if (entity === undefined || !entity.active) continue;
    const dist = distanceSqTo(anchorX, anchorZ, entity);
    const id = entity.id;
    if (nearCount < limit) {
      nearEntities[nearCount] = entity;
      nearDist[nearCount] = dist;
      nearIds[nearCount] = id;
      nearCount += 1;
      nearSiftUp(nearCount - 1);
      continue;
    }
    if (limit === 0) break;
    if (!nearWorse(0, dist, id)) continue;
    nearEntities[0] = entity;
    nearDist[0] = dist;
    nearIds[0] = id;
    nearSiftDown(0);
  }
  const scratch = world.scratchEntities;
  scratch.length = 0;
  for (let i = 0; i < nearCount; i += 1) {
    const entity = nearEntities[i];
    if (entity === undefined) continue;
    scratch.push(entity);
  }
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
