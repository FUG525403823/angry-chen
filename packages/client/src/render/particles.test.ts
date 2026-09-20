import { PerspectiveCamera, Scene } from 'three';
import { describe, expect, it } from 'vitest';

import { createMaterials } from './materials.ts';
import {
  EMITTERS,
  PARTICLE_CAPACITY,
  PARTICLE_EMITTER,
  REDUCED_MOTION_JITTER_SCALE,
  createParticles,
  particleSpawnVelocity,
  type SpawnVelocity,
} from './particles.ts';

function setup(isReduceMotion = false) {
  const scene = new Scene();
  const camera = new PerspectiveCamera(60, 1, 0.1, 200);
  const materials = createMaterials();
  const particles = createParticles(scene, camera, materials, {
    isReduceMotion: () => isReduceMotion,
  });
  return { scene, camera, materials, particles };
}

describe('粒子池', () => {
  it('预分配 256 个槽位，1000 次发射后并发数不超上限', () => {
    const { materials, particles } = setup();
    try {
      expect(particles.capacity).toBe(PARTICLE_CAPACITY);
      for (let i = 0; i < 1000; i += 1) {
        particles.emit(PARTICLE_EMITTER.spark, i % 7, 1, i % 11, 4, 0, 1, 0);
        particles.update(4);
      }
      expect(particles.activeCount).toBeLessThanOrEqual(PARTICLE_CAPACITY);
      expect(particles.mesh.count).toBeLessThanOrEqual(PARTICLE_CAPACITY);
    } finally {
      particles.dispose();
      materials.dispose();
    }
  });

  it('发射器并发上限生效', () => {
    const { materials, particles } = setup();
    try {
      const spawned = particles.emit(PARTICLE_EMITTER.spark, 0, 0, 0, 1000, 0, 1, 0);
      const cap = EMITTERS[PARTICLE_EMITTER.spark]?.maxActive ?? 0;
      expect(spawned).toBe(cap);
      expect(particles.activeCount).toBe(cap);
    } finally {
      particles.dispose();
      materials.dispose();
    }
  });

  it('生命周期结束后槽位被回收', () => {
    const { materials, particles } = setup();
    try {
      particles.emit(PARTICLE_EMITTER.muzzle, 0, 1, 0, 8, 0, 0, -1);
      expect(particles.activeCount).toBe(8);
      particles.update(200);
      expect(particles.activeCount).toBe(0);
    } finally {
      particles.dispose();
      materials.dispose();
    }
  });

  it('减少动态时抖动幅度显著收敛但不改变发射数量', () => {
    expect(REDUCED_MOTION_JITTER_SCALE).toBeGreaterThan(0);
    expect(REDUCED_MOTION_JITTER_SCALE).toBeLessThan(1);
    const config = EMITTERS[PARTICLE_EMITTER.spark];
    if (config === undefined) throw new Error('缺少 spark 发射器');
    const out: SpawnVelocity = { x: 0, y: 0, z: 0 };
    for (let seed = 0; seed < 64; seed += 1) {
      const full = particleSpawnVelocity(config, seed, 0, 1, 0, 1, { ...out });
      const calm = particleSpawnVelocity(config, seed, 0, 1, 0, REDUCED_MOTION_JITTER_SCALE, out);
      const fullAngle = Math.abs(Math.atan2(full.x, full.y));
      const calmAngle = Math.abs(Math.atan2(calm.x, calm.y));
      expect(calmAngle).toBeLessThan(fullAngle);
    }
    const calm = setup(true);
    const normal = setup(false);
    try {
      for (let i = 0; i < 10; i += 1) {
        calm.particles.emit(PARTICLE_EMITTER.wool, 0, 1, 0, 3, 0, 1, 0);
        normal.particles.emit(PARTICLE_EMITTER.wool, 0, 1, 0, 3, 0, 1, 0);
      }
      expect(calm.particles.activeCount).toBe(normal.particles.activeCount);
    } finally {
      calm.particles.dispose();
      calm.materials.dispose();
      normal.particles.dispose();
      normal.materials.dispose();
    }
  });
});
