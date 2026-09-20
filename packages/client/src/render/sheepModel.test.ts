import { InstancedMesh, Scene } from 'three';
import { describe, expect, it } from 'vitest';

import { createMaterials } from './materials.ts';
import {
  CORPSE_FADE_MS,
  CORPSE_SHRINK,
  CORPSE_SINK_M,
  SHEEP_DEAD_STATE,
  WOOL_CLUSTERS_GRUNT,
  WOOL_CLUSTERS_PER_SHEEP,
  type SheepInstance,
  corpseFadeProgress,
  createSheepFlock,
} from './sheepModel.ts';

function sheepInstance(id: number, form: number, dead: boolean): SheepInstance {
  const base = {
    id,
    form,
    x: 0,
    y: 0,
    z: 0,
    yaw: 0,
    hpRatio: dead ? 0 : 1,
    windup: false,
  };
  return dead ? { ...base, state: SHEEP_DEAD_STATE } : base;
}

function meshNamed(scene: Scene, name: string): InstancedMesh {
  const mesh = scene.getObjectByName(name);
  if (!(mesh instanceof InstancedMesh)) throw new Error('missing mesh ' + name);
  return mesh;
}

describe('羊尸体淡出曲线', () => {
  it('从 0 单调升到 1 并夹紧', () => {
    expect(corpseFadeProgress(0)).toBe(0);
    expect(corpseFadeProgress(-50)).toBe(0);
    expect(corpseFadeProgress(Number.NaN)).toBe(0);
    expect(corpseFadeProgress(CORPSE_FADE_MS)).toBe(1);
    expect(corpseFadeProgress(CORPSE_FADE_MS * 3)).toBe(1);
    let previous = -1;
    for (let step = 0; step <= 10; step += 1) {
      const value = corpseFadeProgress((CORPSE_FADE_MS * step) / 10);
      expect(value).toBeGreaterThan(previous);
      previous = value;
    }
  });
});

describe('羊群实例化视图', () => {
  it('grunt 用 8 簇羊毛、其余羊形用 12 簇', () => {
    const scene = new Scene();
    const materials = createMaterials();
    const flock = createSheepFlock(scene, materials);
    flock.begin();
    flock.push(sheepInstance(1, 0, false));
    flock.push(sheepInstance(2, 1, false));
    flock.end(16.7);
    expect(meshNamed(scene, 'sheep-wool-0').count).toBe(WOOL_CLUSTERS_GRUNT);
    expect(meshNamed(scene, 'sheep-wool-1').count).toBe(WOOL_CLUSTERS_PER_SHEEP);
    expect(meshNamed(scene, 'sheep-body-1').count).toBe(1);
    expect(meshNamed(scene, 'sheep-horn-1').count).toBe(1);
    expect(flock.visibleCount).toBe(2);
    expect(flock.corpseCount).toBe(0);
    flock.dispose();
    materials.dispose();
  });

  it('死亡状态 8 的羊逐帧缩小下沉并在离场后回收', () => {
    const scene = new Scene();
    const materials = createMaterials();
    const flock = createSheepFlock(scene, materials);
    const body = () => meshNamed(scene, 'sheep-body-0').instanceMatrix.array;

    flock.begin();
    flock.push(sheepInstance(7, 0, false));
    flock.end(CORPSE_FADE_MS / 2);
    expect(body()[0] ?? 0).toBeCloseTo(1, 5);
    expect(flock.corpseCount).toBe(0);

    flock.begin();
    flock.push(sheepInstance(7, 0, true));
    flock.end(CORPSE_FADE_MS / 2);
    const half = body()[0] ?? 0;
    expect(half).toBeCloseTo(1 - CORPSE_SHRINK * 0.5, 5);
    expect(body()[13] ?? 0).toBeCloseTo(-CORPSE_SINK_M * 0.5, 5);
    expect(flock.corpseCount).toBe(1);

    flock.begin();
    flock.push(sheepInstance(7, 0, true));
    flock.end(CORPSE_FADE_MS);
    expect(body()[0] ?? 0).toBeCloseTo(1 - CORPSE_SHRINK, 5);

    flock.begin();
    flock.end(16.7);
    expect(flock.corpseCount).toBe(0);
    expect(meshNamed(scene, 'sheep-body-0').count).toBe(0);
    flock.dispose();
    materials.dispose();
  });
});
