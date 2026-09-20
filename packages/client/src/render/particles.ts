import {
  CanvasTexture,
  type Camera,
  Color,
  InstancedMesh,
  Matrix4,
  PlaneGeometry,
  Quaternion,
  type Scene,
  SRGBColorSpace,
  Vector3,
} from 'three';

import type { RenderMaterials } from './materials.ts';

export const PARTICLE_CAPACITY = 256;
export const PARTICLE_DOT_SIZE_PX = 64;
export const REDUCED_MOTION_JITTER_SCALE = 0.35;
export const PARTICLE_EMITTER = Object.freeze({
  muzzle: 0,
  wool: 1,
  bullet: 2,
  spark: 3,
  debris: 4,
} as const);

export interface EmitterConfig {
  readonly name: string;
  readonly lifeMs: number;
  readonly maxActive: number;
  readonly sizeM: number;
  readonly speedMps: number;
  readonly speedJitter: number;
  readonly spread: number;
  readonly gravityMps2: number;
  readonly drag: number;
  readonly color: number;
  readonly lift: number;
}

export const EMITTERS: readonly EmitterConfig[] = Object.freeze([
  {
    name: 'muzzle',
    lifeMs: 55,
    maxActive: 32,
    sizeM: 0.16,
    speedMps: 2.4,
    speedJitter: 1.2,
    spread: 0.45,
    gravityMps2: 0,
    drag: 6,
    color: 0xffd479,
    lift: 0.02,
  },
  {
    name: 'wool',
    lifeMs: 900,
    maxActive: 96,
    sizeM: 0.08,
    speedMps: 3.2,
    speedJitter: 2.4,
    spread: 0.9,
    gravityMps2: -3.4,
    drag: 1.6,
    color: 0xf3efe4,
    lift: 1.2,
  },
  {
    name: 'bullet',
    lifeMs: 6000,
    maxActive: 64,
    sizeM: 0.05,
    speedMps: 0.35,
    speedJitter: 0.2,
    spread: 0.5,
    gravityMps2: 0,
    drag: 12,
    color: 0x2b2a2c,
    lift: 0,
  },
  {
    name: 'spark',
    lifeMs: 320,
    maxActive: 64,
    sizeM: 0.05,
    speedMps: 7.2,
    speedJitter: 3.4,
    spread: 1,
    gravityMps2: -6.5,
    drag: 3.2,
    color: 0xffd08a,
    lift: 0.3,
  },
  {
    name: 'debris',
    lifeMs: 1100,
    maxActive: 96,
    sizeM: 0.1,
    speedMps: 4.6,
    speedJitter: 3,
    spread: 1,
    gravityMps2: -9,
    drag: 1.2,
    color: 0x8f3b34,
    lift: 1.6,
  },
]);

interface ParticlePool {
  readonly position: Float32Array;
  readonly velocity: Float32Array;
  readonly lifeMs: Float32Array;
  readonly maxLifeMs: Float32Array;
  readonly sizeM: Float32Array;
  readonly color: Float32Array;
  readonly gravity: Float32Array;
  readonly drag: Float32Array;
  readonly kind: Int32Array;
  readonly kindActive: Int32Array;
  cursor: number;
  sequence: number;
}

export interface ParticleOptions {
  readonly isReduceMotion?: (() => boolean) | undefined;
}

export interface SpawnVelocity {
  x: number;
  y: number;
  z: number;
}

export interface ParticleSystem {
  readonly mesh: InstancedMesh;
  readonly capacity: number;
  readonly activeCount: number;
  emit(
    kind: number,
    x: number,
    y: number,
    z: number,
    count: number,
    dirX: number,
    dirY: number,
    dirZ: number,
  ): number;
  update(dtMs: number): void;
  dispose(): void;
}

export function hashFloat(seed: number): number {
  let h = Math.imul(seed ^ 0x9e3779b9, 0x85ebca6b);
  h = Math.imul(h ^ (h >>> 13), 0xc2b2ae35);
  return ((h ^ (h >>> 16)) >>> 0) / 4294967295;
}

export function particleSpawnVelocity(
  config: EmitterConfig,
  seed: number,
  dirX: number,
  dirY: number,
  dirZ: number,
  jitterScale: number,
  out: SpawnVelocity,
): SpawnVelocity {
  const r1 = hashFloat(seed);
  const r2 = hashFloat(seed + 1);
  const r3 = hashFloat(seed + 2);
  const r4 = hashFloat(seed + 3);
  const speed = config.speedMps + (r1 * 2 - 1) * config.speedJitter * jitterScale;
  const yaw = r2 * Math.PI * 2;
  const spread = r3 * config.spread * jitterScale;
  const jitterX = Math.cos(yaw) * spread;
  const jitterY = Math.sin(yaw) * spread;
  out.x = (dirX + jitterX) * speed;
  out.y = (dirY + jitterY) * speed + config.lift * r4 * jitterScale;
  out.z = (dirZ + jitterX * 0.5) * speed;
  return out;
}

let cachedDotTexture: CanvasTexture | undefined;

export function getSoftDotTexture(): CanvasTexture {
  if (cachedDotTexture !== undefined) return cachedDotTexture;
  const canvas = document.createElement('canvas');
  canvas.width = PARTICLE_DOT_SIZE_PX;
  canvas.height = PARTICLE_DOT_SIZE_PX;
  const context = canvas.getContext('2d');
  if (context !== null) {
    const half = PARTICLE_DOT_SIZE_PX / 2;
    const gradient = context.createRadialGradient(half, half, 0, half, half, half);
    gradient.addColorStop(0, 'rgba(255,255,255,1)');
    gradient.addColorStop(0.45, 'rgba(255,255,255,0.85)');
    gradient.addColorStop(1, 'rgba(255,255,255,0)');
    context.fillStyle = gradient;
    context.fillRect(0, 0, PARTICLE_DOT_SIZE_PX, PARTICLE_DOT_SIZE_PX);
  }
  const texture = new CanvasTexture(canvas);
  texture.colorSpace = SRGBColorSpace;
  texture.needsUpdate = true;
  cachedDotTexture = texture;
  return texture;
}

function createPool(): ParticlePool {
  return {
    position: new Float32Array(PARTICLE_CAPACITY * 3),
    velocity: new Float32Array(PARTICLE_CAPACITY * 3),
    lifeMs: new Float32Array(PARTICLE_CAPACITY),
    maxLifeMs: new Float32Array(PARTICLE_CAPACITY),
    sizeM: new Float32Array(PARTICLE_CAPACITY),
    color: new Float32Array(PARTICLE_CAPACITY * 3),
    gravity: new Float32Array(PARTICLE_CAPACITY),
    drag: new Float32Array(PARTICLE_CAPACITY),
    kind: new Int32Array(PARTICLE_CAPACITY).fill(-1),
    kindActive: new Int32Array(EMITTERS.length),
    cursor: 0,
    sequence: 0,
  };
}

export function createParticles(
  scene: Scene,
  camera: Camera,
  materials: RenderMaterials,
  options: ParticleOptions = {},
): ParticleSystem {
  const pool = createPool();
  materials.particle.map = getSoftDotTexture();
  materials.particle.needsUpdate = true;
  const geometry = new PlaneGeometry(1, 1);
  const mesh = new InstancedMesh(geometry, materials.particle, PARTICLE_CAPACITY);
  mesh.frustumCulled = false;
  mesh.name = 'particles';
  const identity = new Matrix4();
  const scratchColor = new Color();
  for (let i = 0; i < PARTICLE_CAPACITY; i += 1) {
    identity.makeScale(0, 0, 0);
    mesh.setMatrixAt(i, identity);
    mesh.setColorAt(i, scratchColor.setRGB(0, 0, 0));
  }
  mesh.instanceMatrix.needsUpdate = true;
  if (mesh.instanceColor !== null) mesh.instanceColor.needsUpdate = true;
  scene.add(mesh);

  const matrix = new Matrix4();
  const position = new Vector3();
  const scale = new Vector3();
  const orientation = new Quaternion();
  const spawnVelocity: SpawnVelocity = { x: 0, y: 0, z: 0 };

  function acquire(kind: number): number {
    const config = EMITTERS[kind];
    if (config === undefined) return -1;
    if ((pool.kindActive[kind] ?? 0) >= config.maxActive) return -1;
    for (let step = 0; step < PARTICLE_CAPACITY; step += 1) {
      const index = (pool.cursor + step) % PARTICLE_CAPACITY;
      if (pool.lifeMs[index] !== undefined && (pool.lifeMs[index] ?? 0) > 0) continue;
      pool.cursor = (index + 1) % PARTICLE_CAPACITY;
      return index;
    }
    return -1;
  }

  const system: ParticleSystem = {
    mesh,
    capacity: PARTICLE_CAPACITY,
    get activeCount(): number {
      let active = 0;
      for (let i = 0; i < PARTICLE_CAPACITY; i += 1) if ((pool.lifeMs[i] ?? 0) > 0) active += 1;
      return active;
    },
    emit(kind, x, y, z, count, dirX, dirY, dirZ): number {
      const config = EMITTERS[kind];
      if (config === undefined) return 0;
      const jitterScale = options.isReduceMotion?.() === true ? REDUCED_MOTION_JITTER_SCALE : 1;
      let spawned = 0;
      for (let i = 0; i < count; i += 1) {
        const index = acquire(kind);
        if (index < 0) break;
        pool.sequence += 1;
        const seed = pool.sequence * 2654435761;
        const r2 = hashFloat(seed + 1);
        const r4 = hashFloat(seed + 3);
        const base = pool.position;
        base[index * 3] = x;
        base[index * 3 + 1] = y;
        base[index * 3 + 2] = z;
        particleSpawnVelocity(config, seed, dirX, dirY, dirZ, jitterScale, spawnVelocity);
        const velocity = pool.velocity;
        velocity[index * 3] = spawnVelocity.x;
        velocity[index * 3 + 1] = spawnVelocity.y;
        velocity[index * 3 + 2] = spawnVelocity.z;
        const life = config.lifeMs * (0.75 + r4 * 0.5);
        pool.lifeMs[index] = life;
        pool.maxLifeMs[index] = life;
        pool.sizeM[index] = config.sizeM * (0.7 + r2 * 0.6);
        pool.gravity[index] = config.gravityMps2;
        pool.drag[index] = config.drag;
        pool.kind[index] = kind;
        pool.kindActive[kind] = (pool.kindActive[kind] ?? 0) + 1;
        scratchColor.setHex(config.color);
        pool.color[index * 3] = scratchColor.r;
        pool.color[index * 3 + 1] = scratchColor.g;
        pool.color[index * 3 + 2] = scratchColor.b;
        spawned += 1;
      }
      return spawned;
    },
    update(dtMs): void {
      const dtSeconds = dtMs <= 0 ? 0 : dtMs / 1000;
      orientation.copy(camera.quaternion);
      for (let i = 0; i < PARTICLE_CAPACITY; i += 1) {
        const life = pool.lifeMs[i] ?? 0;
        if (life <= 0) continue;
        const nextLife = life - dtMs;
        const kind = pool.kind[i] ?? -1;
        if (nextLife <= 0) {
          pool.lifeMs[i] = 0;
          if (kind >= 0) pool.kindActive[kind] = (pool.kindActive[kind] ?? 0) - 1;
          matrix.makeScale(0, 0, 0);
          mesh.setMatrixAt(i, matrix);
          continue;
        }
        pool.lifeMs[i] = nextLife;
        const drag = pool.drag[i] ?? 0;
        const decay = dtSeconds <= 0 ? 1 : Math.exp(-drag * dtSeconds);
        const velocity = pool.velocity;
        velocity[i * 3] = (velocity[i * 3] ?? 0) * decay;
        velocity[i * 3 + 1] =
          ((velocity[i * 3 + 1] ?? 0) + (pool.gravity[i] ?? 0) * dtSeconds) * decay;
        velocity[i * 3 + 2] = (velocity[i * 3 + 2] ?? 0) * decay;
        const base = pool.position;
        base[i * 3] = (base[i * 3] ?? 0) + (velocity[i * 3] ?? 0) * dtSeconds;
        base[i * 3 + 1] = (base[i * 3 + 1] ?? 0) + (velocity[i * 3 + 1] ?? 0) * dtSeconds;
        base[i * 3 + 2] = (base[i * 3 + 2] ?? 0) + (velocity[i * 3 + 2] ?? 0) * dtSeconds;
        const fade = nextLife / (pool.maxLifeMs[i] ?? 1);
        const size =
          (pool.sizeM[i] ?? 0) * (pool.kind[i] === PARTICLE_EMITTER.bullet ? 1 : 0.4 + fade * 0.6);
        position.set(base[i * 3] ?? 0, base[i * 3 + 1] ?? 0, base[i * 3 + 2] ?? 0);
        scale.set(size, size, size);
        matrix.compose(position, orientation, scale);
        mesh.setMatrixAt(i, matrix);
        const fadePower = pool.kind[i] === PARTICLE_EMITTER.bullet ? 1 : fade;
        scratchColor.setRGB(
          (pool.color[i * 3] ?? 0) * fadePower,
          (pool.color[i * 3 + 1] ?? 0) * fadePower,
          (pool.color[i * 3 + 2] ?? 0) * fadePower,
        );
        mesh.setColorAt(i, scratchColor);
      }
      mesh.instanceMatrix.needsUpdate = true;
      if (mesh.instanceColor !== null) mesh.instanceColor.needsUpdate = true;
    },
    dispose(): void {
      scene.remove(mesh);
      geometry.dispose();
      mesh.dispose();
    },
  };
  return system;
}
