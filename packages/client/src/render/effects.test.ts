import { PerspectiveCamera, Scene } from 'three';
import { describe, expect, it } from 'vitest';

import type { ViewEntity } from '../net/state.ts';
import { ELITE_BOLT_CAPACITY, createEffects } from './effects.ts';
import { createMaterials } from './materials.ts';
import { createParticles } from './particles.ts';
import type { ChargeWarning } from './sheepModel.ts';

const WINDUP_WARNING: ChargeWarning = { id: 7, x: 3, z: -4, radiusM: 1.1 };

function entity(id: number, kind: ViewEntity['kind'], x: number): ViewEntity {
  return {
    id,
    kind,
    pos: { x, y: 1.2, z: -6 },
    yaw: 0.4,
    pitch: 0,
    hpRatio: 1,
    state: 0,
    flags: 0,
  };
}

function sourceOf(entities: readonly ViewEntity[]): {
  forEachVisible(cb: (e: ViewEntity) => void): void;
} {
  return {
    forEachVisible(cb: (e: ViewEntity) => void): void {
      for (const item of entities) cb(item);
    },
  };
}

function setup(isReduceMotion: () => boolean) {
  const scene = new Scene();
  const camera = new PerspectiveCamera(60, 1, 0.1, 200);
  const materials = createMaterials();
  const particles = createParticles(scene, camera, materials, {
    isReduceMotion,
  });
  const windups: number[] = [];
  const effects = createEffects(scene, particles, materials, document.createElement('div'), {
    isReduceMotion,
    onChargeWindup: (id) => windups.push(id),
  });
  return { materials, particles, effects, windups };
}

describe('屏幕特效层', () => {
  it('冲锋预警只在环出现时触发一次回调，超时后回收', () => {
    const { materials, particles, effects, windups } = setup(() => false);
    try {
      expect(effects.activeChargeRings).toBe(0);
      effects.setChargeWarnings([WINDUP_WARNING]);
      effects.setChargeWarnings([WINDUP_WARNING]);
      expect(effects.activeChargeRings).toBe(1);
      expect(windups).toEqual([WINDUP_WARNING.id]);
      effects.update(1400);
      expect(effects.activeChargeRings).toBe(0);
      effects.setChargeWarnings([WINDUP_WARNING]);
      expect(windups).toEqual([WINDUP_WARNING.id, WINDUP_WARNING.id]);
    } finally {
      effects.dispose();
      particles.dispose();
      materials.dispose();
    }
  });

  it('CSS 覆盖层全部挂在 #hud 宿主上', () => {
    const { materials, particles, effects } = setup(() => false);
    const host = document.createElement('div');
    const fresh = createEffects(new Scene(), particles, materials, host, {
      isReduceMotion: () => false,
    });
    try {
      expect(host.children.length).toBe(5);
    } finally {
      fresh.dispose();
      effects.dispose();
      particles.dispose();
      materials.dispose();
    }
  });

  it('减少动态关闭屏幕抖动，正常模式保留衰减抖动', () => {
    const calm = setup(() => true);
    const normal = setup(() => false);
    try {
      calm.effects.triggerShake(0.2);
      calm.effects.update(16.7);
      expect(calm.effects.shakeX).toBe(0);
      expect(calm.effects.shakeY).toBe(0);
      normal.effects.triggerShake(0.2);
      normal.effects.update(16.7);
      expect(Math.abs(normal.effects.shakeX)).toBeGreaterThan(0);
      for (let i = 0; i < 120; i += 1) normal.effects.update(16.7);
      expect(normal.effects.shakeX).toBeCloseTo(0, 6);
    } finally {
      for (const group of [calm, normal]) {
        group.effects.dispose();
        group.particles.dispose();
        group.materials.dispose();
      }
    }
  });

  it('问界羊能量弹按快照驱动并遵守容量上限', () => {
    const { materials, particles, effects } = setup(() => false);
    try {
      const bolts: ViewEntity[] = [];
      bolts.push(entity(1, 3, 0));
      for (let i = 0; i < ELITE_BOLT_CAPACITY + 8; i += 1) {
        bolts.push(entity(100 + i, 2, i - 6));
      }
      effects.syncEliteBolts(sourceOf(bolts));
      expect(effects.activeEliteBolts).toBe(ELITE_BOLT_CAPACITY);
      effects.update(16.7);
      effects.syncEliteBolts(sourceOf([]));
      expect(effects.activeEliteBolts).toBe(0);
    } finally {
      effects.dispose();
      particles.dispose();
      materials.dispose();
    }
  });

  it('曳光持续到期限即回收，血屏与狂暴只改覆盖层状态', () => {
    const { materials, particles, effects } = setup(() => false);
    try {
      effects.spawnTracer(0, 1.6, 0, 0, 1.6, -30, true);
      effects.update(10);
      effects.setBlood(0.6);
      effects.setBerserk(true);
      effects.pulseHitFlash();
      effects.showHitMarker(true);
      effects.spawnImpact(0, 1, -4);
      effects.spawnMuzzleFlash(0, 1.6, 0, 0, 0, -1);
      effects.spawnDeathDebris(2, 0.6, -3);
      expect(particles.activeCount).toBeGreaterThan(0);
      effects.update(200);
      expect(particles.activeCount).toBeGreaterThan(0);
    } finally {
      effects.dispose();
      particles.dispose();
      materials.dispose();
    }
  });
});
