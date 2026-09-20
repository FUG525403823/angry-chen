export interface RayHit {
  hit: boolean;
  t: number;
  x: number;
  y: number;
  z: number;
  nx: number;
  ny: number;
  nz: number;
}

const epsilon = 1e-12;

export function createRayHit(): RayHit {
  return { hit: false, t: 0, x: 0, y: 0, z: 0, nx: 0, ny: 0, nz: 0 };
}

export function clearRayHit(out: RayHit): RayHit {
  out.hit = false;
  out.t = 0;
  out.x = 0;
  out.y = 0;
  out.z = 0;
  out.nx = 0;
  out.ny = 0;
  out.nz = 0;
  return out;
}

function writeHit(
  out: RayHit,
  t: number,
  ox: number,
  oy: number,
  oz: number,
  dx: number,
  dy: number,
  dz: number,
  nx: number,
  ny: number,
  nz: number,
): RayHit {
  out.hit = true;
  out.t = t;
  out.x = ox + dx * t;
  out.y = oy + dy * t;
  out.z = oz + dz * t;
  out.nx = nx;
  out.ny = ny;
  out.nz = nz;
  return out;
}

function sphereT(
  ox: number,
  oy: number,
  oz: number,
  dx: number,
  dy: number,
  dz: number,
  cx: number,
  cy: number,
  cz: number,
  radius: number,
  maxDist: number,
): number {
  const mx = ox - cx;
  const my = oy - cy;
  const mz = oz - cz;
  const b = mx * dx + my * dy + mz * dz;
  const c = mx * mx + my * my + mz * mz - radius * radius;
  if (c > 0 && b > 0) return -1;
  const disc = b * b - c;
  if (disc < 0) return -1;
  let t = -b - Math.sqrt(disc);
  if (t < 0) t = 0;
  return t <= maxDist ? t : -1;
}

/** 方向向量必须是单位向量。 */
export function rayVsSphere(
  ox: number,
  oy: number,
  oz: number,
  dx: number,
  dy: number,
  dz: number,
  cx: number,
  cy: number,
  cz: number,
  radius: number,
  maxDist: number,
  out: RayHit,
): RayHit {
  clearRayHit(out);
  const t = sphereT(ox, oy, oz, dx, dy, dz, cx, cy, cz, radius, maxDist);
  if (t < 0) return out;
  const inverse = radius > epsilon ? 1 / radius : 0;
  return writeHit(
    out,
    t,
    ox,
    oy,
    oz,
    dx,
    dy,
    dz,
    (ox + dx * t - cx) * inverse,
    (oy + dy * t - cy) * inverse,
    (oz + dz * t - cz) * inverse,
  );
}

/** 轴对齐盒体（slab 法）。方向向量必须是单位向量。 */
export function rayVsAabb(
  ox: number,
  oy: number,
  oz: number,
  dx: number,
  dy: number,
  dz: number,
  minX: number,
  minY: number,
  minZ: number,
  maxX: number,
  maxY: number,
  maxZ: number,
  maxDist: number,
  out: RayHit,
): RayHit {
  clearRayHit(out);
  let tMin = 0;
  let tMax = maxDist;
  let axis = 0;
  let sign = 1;
  const origins = [ox, oy, oz];
  const dirs = [dx, dy, dz];
  const lows = [minX, minY, minZ];
  const highs = [maxX, maxY, maxZ];
  for (let i = 0; i < 3; i += 1) {
    const origin = origins[i] ?? 0;
    const dir = dirs[i] ?? 0;
    const low = lows[i] ?? 0;
    const high = highs[i] ?? 0;
    if (Math.abs(dir) < epsilon) {
      if (origin < low || origin > high) return out;
      continue;
    }
    const inverse = 1 / dir;
    let near = (low - origin) * inverse;
    let far = (high - origin) * inverse;
    let nearSign = -1;
    if (near > far) {
      const swap = near;
      near = far;
      far = swap;
      nearSign = 1;
    }
    if (near > tMin) {
      tMin = near;
      axis = i;
      sign = nearSign;
    }
    if (far < tMax) tMax = far;
    if (tMin > tMax) return out;
  }
  const nx = axis === 0 ? sign : 0;
  const ny = axis === 1 ? sign : 0;
  const nz = axis === 2 ? sign : 0;
  return writeHit(out, tMin, ox, oy, oz, dx, dy, dz, nx, ny, nz);
}

/** 胶囊 = 线段 a→b 加半径；方向向量必须是单位向量。 */
export function rayVsCapsule(
  ox: number,
  oy: number,
  oz: number,
  dx: number,
  dy: number,
  dz: number,
  ax: number,
  ay: number,
  az: number,
  bx: number,
  by: number,
  bz: number,
  radius: number,
  maxDist: number,
  out: RayHit,
): RayHit {
  clearRayHit(out);
  const abx = bx - ax;
  const aby = by - ay;
  const abz = bz - az;
  const ab2 = abx * abx + aby * aby + abz * abz;
  let bestT = Number.POSITIVE_INFINITY;
  let bestK = 0;

  if (ab2 > epsilon) {
    const aox = ox - ax;
    const aoy = oy - ay;
    const aoz = oz - az;
    const abd = dx * abx + dy * aby + dz * abz;
    const aod = aox * abx + aoy * aby + aoz * abz;
    const a = ab2 * (dx * dx + dy * dy + dz * dz) - abd * abd;
    const b = 2 * (ab2 * (dx * aox + dy * aoy + dz * aoz) - abd * aod);
    const c = ab2 * (aox * aox + aoy * aoy + aoz * aoz) - aod * aod - radius * radius * ab2;
    if (Math.abs(a) > epsilon) {
      const disc = b * b - 4 * a * c;
      if (disc >= 0) {
        const root = Math.sqrt(disc);
        const t0 = (-b - root) / (2 * a);
        const t1 = (-b + root) / (2 * a);
        for (let i = 0; i < 2; i += 1) {
          const t = i === 0 ? t0 : t1;
          if (t < 0 || t > maxDist || t >= bestT) continue;
          const k = (aod + t * abd) / ab2;
          if (k < 0 || k > 1) continue;
          bestT = t;
          bestK = k;
        }
      }
    }
  }

  const capA = sphereT(ox, oy, oz, dx, dy, dz, ax, ay, az, radius, maxDist);
  if (capA >= 0 && capA < bestT) {
    bestT = capA;
    bestK = 0;
  }
  const capB = sphereT(ox, oy, oz, dx, dy, dz, bx, by, bz, radius, maxDist);
  if (capB >= 0 && capB < bestT) {
    bestT = capB;
    bestK = 1;
  }

  if (!Number.isFinite(bestT)) return out;
  const cx = ax + abx * bestK;
  const cy = ay + aby * bestK;
  const cz = az + abz * bestK;
  const inverse = radius > epsilon ? 1 / radius : 0;
  return writeHit(
    out,
    bestT,
    ox,
    oy,
    oz,
    dx,
    dy,
    dz,
    (ox + dx * bestT - cx) * inverse,
    (oy + dy * bestT - cy) * inverse,
    (oz + dz * bestT - cz) * inverse,
  );
}
