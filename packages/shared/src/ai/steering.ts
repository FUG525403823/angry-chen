import type { ArenaConfig } from '../config/arena.ts';

export interface SteeringOut {
  x: number;
  z: number;
}

export function createSteeringOut(): SteeringOut {
  return { x: 0, z: 0 };
}

export function normalize(out: SteeringOut, speed: number): SteeringOut {
  const length = Math.sqrt(out.x * out.x + out.z * out.z);
  if (length <= 1e-9) {
    out.x = 0;
    out.z = 0;
    return out;
  }
  const scale = speed / length;
  out.x *= scale;
  out.z *= scale;
  return out;
}

export function seek(
  out: SteeringOut,
  selfX: number,
  selfZ: number,
  targetX: number,
  targetZ: number,
  speed: number,
): SteeringOut {
  out.x = targetX - selfX;
  out.z = targetZ - selfZ;
  return normalize(out, speed);
}

export function arrive(
  out: SteeringOut,
  selfX: number,
  selfZ: number,
  targetX: number,
  targetZ: number,
  speed: number,
  slowRadiusM: number,
): SteeringOut {
  const dx = targetX - selfX;
  const dz = targetZ - selfZ;
  const distance = Math.sqrt(dx * dx + dz * dz);
  if (distance <= 1e-9) {
    out.x = 0;
    out.z = 0;
    return out;
  }
  const desired = distance < slowRadiusM ? speed * (distance / slowRadiusM) : speed;
  out.x = dx;
  out.z = dz;
  return normalize(out, desired);
}

export function separation(
  out: SteeringOut,
  selfX: number,
  selfZ: number,
  xs: Float64Array,
  zs: Float64Array,
  count: number,
  radiusM: number,
  strength: number,
): SteeringOut {
  out.x = 0;
  out.z = 0;
  if (count <= 0 || radiusM <= 0) return out;
  const radiusSq = radiusM * radiusM;
  for (let i = 0; i < count; i += 1) {
    const dx = selfX - (xs[i] ?? 0);
    const dz = selfZ - (zs[i] ?? 0);
    const distanceSq = dx * dx + dz * dz;
    if (distanceSq >= radiusSq) continue;
    if (distanceSq < 1e-9) {
      out.x += i % 2 === 0 ? 1 : -1;
      out.z += i % 2 === 0 ? -1 : 1;
      continue;
    }
    const weight = (radiusM - Math.sqrt(distanceSq)) / radiusM / distanceSq;
    out.x += dx * weight;
    out.z += dz * weight;
  }
  return normalize(out, strength);
}

export function obstacleAvoid(
  out: SteeringOut,
  selfX: number,
  selfZ: number,
  dirX: number,
  dirZ: number,
  arena: ArenaConfig,
  radiusM: number,
): SteeringOut {
  const barn = arena.barn;
  const limit = arena.halfSize - arena.fence.thickness / 2 - radiusM;
  let steerX = 0;
  let steerZ = 0;
  if (selfX > limit) steerX -= (selfX - limit) * 2;
  if (selfX < -limit) steerX += (-limit - selfX) * 2;
  if (selfZ > limit) steerZ -= (selfZ - limit) * 2;
  if (selfZ < -limit) steerZ += (-limit - selfZ) * 2;

  const marginM = radiusM + 1;
  const minX = barn.minX - marginM;
  const maxX = barn.maxX + marginM;
  const minZ = barn.minZ - marginM;
  const maxZ = barn.maxZ + marginM;
  if (selfX > minX && selfX < maxX && selfZ > minZ && selfZ < maxZ) {
    const pushLeft = selfX - minX;
    const pushRight = maxX - selfX;
    const pushBack = selfZ - minZ;
    const pushForward = maxZ - selfZ;
    const minPush = Math.min(pushLeft, pushRight, pushBack, pushForward);
    if (minPush === pushLeft) steerX -= (marginM - pushLeft) * 2;
    else if (minPush === pushRight) steerX += (marginM - pushRight) * 2;
    else if (minPush === pushBack) steerZ -= (marginM - pushBack) * 2;
    else steerZ += (marginM - pushForward) * 2;
  }

  out.x = dirX + steerX;
  out.z = dirZ + steerZ;
  return normalize(out, Math.sqrt(dirX * dirX + dirZ * dirZ));
}
