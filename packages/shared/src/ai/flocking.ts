import { SHEEP_AI } from '../config/sheep.ts';
import { forEachNeighbor, type SpatialGrid } from '../sim/spatialGrid.ts';
import { normalize, type SteeringOut } from './steering.ts';
import type { Entity, World } from '../world.ts';

export const FLOCK_WEIGHT = Object.freeze({
  separation: 1.6,
  alignment: 0.4,
  cohesion: 0.5,
});

export interface FlockNeighbors {
  readonly capacity: number;
  readonly xs: Float64Array;
  readonly zs: Float64Array;
  readonly dirXs: Float64Array;
  readonly dirZs: Float64Array;
  count: number;
  centroidX: number;
  centroidZ: number;
  averageDirX: number;
  averageDirZ: number;
  /** 每槽到自身的距离平方（升序）与邻居 id：按距离有界插入、并按 id 破平局，使结果与遍历顺序无关。 */
  readonly distSq: Float64Array;
  readonly ids: Int32Array;
}

export function createFlockNeighbors(capacity = SHEEP_AI.maxNeighbors): FlockNeighbors {
  return {
    capacity,
    xs: new Float64Array(capacity),
    zs: new Float64Array(capacity),
    dirXs: new Float64Array(capacity),
    dirZs: new Float64Array(capacity),
    distSq: new Float64Array(capacity),
    ids: new Int32Array(capacity),
    count: 0,
    centroidX: 0,
    centroidZ: 0,
    averageDirX: 0,
    averageDirZ: 0,
  };
}

export function resetFlockNeighbors(neighbors: FlockNeighbors): void {
  neighbors.count = 0;
  neighbors.centroidX = 0;
  neighbors.centroidZ = 0;
  neighbors.averageDirX = 0;
  neighbors.averageDirZ = 0;
}

/**
 * 纯追加原语（容量满即丢弃，不参与距离排序）。gatherNeighbors 走 insertNeighborByDistance，
 * 这里保留给不需要距离信息的调用方（测试与将来的自定义聚集）。
 */
export function addNeighbor(
  neighbors: FlockNeighbors,
  x: number,
  z: number,
  dirX: number,
  dirZ: number,
): void {
  if (neighbors.count >= neighbors.capacity) return;
  const index = neighbors.count;
  neighbors.xs[index] = x;
  neighbors.zs[index] = z;
  neighbors.dirXs[index] = dirX;
  neighbors.dirZs[index] = dirZ;
  neighbors.distSq[index] = Number.POSITIVE_INFINITY;
  neighbors.ids[index] = 0;
  neighbors.count += 1;
}

/**
 * 有界按距离插入：容量满时挤掉最远的一个；距离相同按 id 升序，保证与遍历顺序无关。
 * 排序键是 (distanceSq, id)，插入排序在容量 ≤12 时是常数级成本。
 */
export function insertNeighborByDistance(
  neighbors: FlockNeighbors,
  distanceSq: number,
  id: number,
  x: number,
  z: number,
  dirX: number,
  dirZ: number,
): void {
  const capacity = neighbors.capacity;
  let count = neighbors.count;
  if (count >= capacity) {
    const worst = count - 1;
    const worstDist = neighbors.distSq[worst] ?? Number.POSITIVE_INFINITY;
    const worstId = neighbors.ids[worst] ?? 0;
    if (distanceSq > worstDist || (distanceSq === worstDist && id >= worstId)) return;
  } else {
    count += 1;
  }

  let at = 0;
  while (at < count - 1) {
    const slotDist = neighbors.distSq[at] ?? Number.POSITIVE_INFINITY;
    const slotId = neighbors.ids[at] ?? 0;
    if (slotDist > distanceSq || (slotDist === distanceSq && slotId > id)) break;
    at += 1;
  }
  for (let i = count - 1; i > at; i -= 1) {
    neighbors.distSq[i] = neighbors.distSq[i - 1] ?? Number.POSITIVE_INFINITY;
    neighbors.ids[i] = neighbors.ids[i - 1] ?? 0;
    neighbors.xs[i] = neighbors.xs[i - 1] ?? 0;
    neighbors.zs[i] = neighbors.zs[i - 1] ?? 0;
    neighbors.dirXs[i] = neighbors.dirXs[i - 1] ?? 0;
    neighbors.dirZs[i] = neighbors.dirZs[i - 1] ?? 0;
  }
  neighbors.distSq[at] = distanceSq;
  neighbors.ids[at] = id;
  neighbors.xs[at] = x;
  neighbors.zs[at] = z;
  neighbors.dirXs[at] = dirX;
  neighbors.dirZs[at] = dirZ;
  neighbors.count = count;
}

export function finalizeNeighbors(neighbors: FlockNeighbors): void {
  const count = neighbors.count;
  if (count <= 0) return;
  let centroidX = 0;
  let centroidZ = 0;
  let averageDirX = 0;
  let averageDirZ = 0;
  for (let i = 0; i < count; i += 1) {
    centroidX += neighbors.xs[i] ?? 0;
    centroidZ += neighbors.zs[i] ?? 0;
    averageDirX += neighbors.dirXs[i] ?? 0;
    averageDirZ += neighbors.dirZs[i] ?? 0;
  }
  neighbors.centroidX = centroidX / count;
  neighbors.centroidZ = centroidZ / count;
  neighbors.averageDirX = averageDirX / count;
  neighbors.averageDirZ = averageDirZ / count;
}

/**
 * 邻居聚集：只访问覆盖半径的整数格，取距离最近的 maxNeighbors 个（与 activeIds 顺序无关）。
 *
 * 访问器是模块级函数 + 模块级上下文，避免每次调用新建闭包（基准里 60 羊/tick ⇒ 会变成稳态分配）。
 */
let gatherEntities: Entity[] = [];
let gatherNeighborsRef: FlockNeighbors | null = null;
let gatherSelfId = 0;
let gatherX = 0;
let gatherZ = 0;
let gatherRadiusSq = 0;

function visitNeighbor(index: number): void {
  const neighbors = gatherNeighborsRef;
  if (neighbors === null) return;
  const other = gatherEntities[index];
  if (other === undefined || !other.active || other.kind !== 'sheep' || other.id === gatherSelfId)
    return;
  const dx = other.pos.x - gatherX;
  const dz = other.pos.z - gatherZ;
  const distanceSq = dx * dx + dz * dz;
  if (distanceSq > gatherRadiusSq) return;
  insertNeighborByDistance(
    neighbors,
    distanceSq,
    other.id,
    other.pos.x,
    other.pos.z,
    other.vel.x,
    other.vel.z,
  );
}

export function gatherNeighbors(
  neighbors: FlockNeighbors,
  world: World,
  self: Entity,
  grid: SpatialGrid,
): number {
  resetFlockNeighbors(neighbors);
  const radius = SHEEP_AI.neighborRadiusM;
  gatherEntities = world.entities;
  gatherNeighborsRef = neighbors;
  gatherSelfId = self.id;
  gatherX = self.pos.x;
  gatherZ = self.pos.z;
  gatherRadiusSq = radius * radius;
  forEachNeighbor(grid, gatherX, gatherZ, radius, visitNeighbor);
  gatherNeighborsRef = null;
  finalizeNeighbors(neighbors);
  return neighbors.count;
}

export function flockForce(
  out: SteeringOut,
  selfX: number,
  selfZ: number,
  neighbors: FlockNeighbors,
  speed: number,
): SteeringOut {
  if (neighbors.count <= 0) {
    out.x = 0;
    out.z = 0;
    return out;
  }
  let separatorX = 0;
  let separatorZ = 0;
  const radiusSq = SHEEP_AI.neighborRadiusM * SHEEP_AI.neighborRadiusM;
  for (let i = 0; i < neighbors.count; i += 1) {
    const dx = selfX - (neighbors.xs[i] ?? 0);
    const dz = selfZ - (neighbors.zs[i] ?? 0);
    const distanceSq = dx * dx + dz * dz;
    if (distanceSq >= radiusSq) continue;
    if (distanceSq < 1e-9) {
      separatorX += i % 2 === 0 ? 1 : -1;
      separatorZ += i % 2 === 0 ? -1 : 1;
      continue;
    }
    const weight = 1 / distanceSq;
    separatorX += dx * weight;
    separatorZ += dz * weight;
  }
  const separationLength = Math.sqrt(separatorX * separatorX + separatorZ * separatorZ);
  if (separationLength > 1e-9) {
    separatorX /= separationLength;
    separatorZ /= separationLength;
  }

  out.x =
    separatorX * FLOCK_WEIGHT.separation +
    (neighbors.averageDirX - 0) * FLOCK_WEIGHT.alignment +
    (neighbors.centroidX - selfX) * FLOCK_WEIGHT.cohesion * 0.1;
  out.z =
    separatorZ * FLOCK_WEIGHT.separation +
    (neighbors.averageDirZ - 0) * FLOCK_WEIGHT.alignment +
    (neighbors.centroidZ - selfZ) * FLOCK_WEIGHT.cohesion * 0.1;
  return normalize(out, speed);
}
