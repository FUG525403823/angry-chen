import { SHEEP_AI } from '../config/sheep.ts';
import { normalize, type SteeringOut } from './steering.ts';
import { getEntity, type Entity, type World } from '../world.ts';

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
}

export function createFlockNeighbors(capacity = SHEEP_AI.maxNeighbors): FlockNeighbors {
  return {
    capacity,
    xs: new Float64Array(capacity),
    zs: new Float64Array(capacity),
    dirXs: new Float64Array(capacity),
    dirZs: new Float64Array(capacity),
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
  neighbors.count += 1;
  neighbors.centroidX += x;
  neighbors.centroidZ += z;
  neighbors.averageDirX += dirX;
  neighbors.averageDirZ += dirZ;
}

export function finalizeNeighbors(neighbors: FlockNeighbors): void {
  if (neighbors.count <= 0) return;
  neighbors.centroidX /= neighbors.count;
  neighbors.centroidZ /= neighbors.count;
  neighbors.averageDirX /= neighbors.count;
  neighbors.averageDirZ /= neighbors.count;
}

export function gatherNeighbors(neighbors: FlockNeighbors, world: World, self: Entity): number {
  resetFlockNeighbors(neighbors);
  const ids = world.activeIds;
  for (let i = 0; i < ids.length; i += 1) {
    const other = getEntity(world, ids[i] ?? 0);
    if (other === undefined || !other.active || other.kind !== 'sheep' || other.id === self.id)
      continue;
    const dx = other.pos.x - self.pos.x;
    const dz = other.pos.z - self.pos.z;
    if (dx * dx + dz * dz > SHEEP_AI.neighborRadiusM * SHEEP_AI.neighborRadiusM) continue;
    addNeighbor(neighbors, other.pos.x, other.pos.z, other.vel.x, other.vel.z);
  }
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
