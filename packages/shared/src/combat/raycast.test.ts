import { describe, expect, it } from 'vitest';
import { clearRayHit, createRayHit, rayVsAabb, rayVsCapsule, rayVsSphere } from './raycast.ts';

const hit = createRayHit();

describe('rayVsCapsule', () => {
  it('命中原柱面（玩家胶囊尺寸）', () => {
    rayVsCapsule(-5, 0.85, 0, 1, 0, 0, 0, 0.4, 0, 0, 1.3, 0, 0.4, 100, hit);
    expect(hit.hit).toBe(true);
    expect(hit.t).toBeCloseTo(4.6, 6);
    expect(hit.x).toBeCloseTo(-0.4, 6);
    expect(hit.nx).toBeCloseTo(-1, 6);
  });

  it('上下都越过时未命中', () => {
    rayVsCapsule(-5, 3, 0, 1, 0, 0, 0, 0.4, 0, 0, 1.3, 0, 0.4, 100, hit);
    expect(hit.hit).toBe(false);
    rayVsCapsule(-5, -3, 0, 1, 0, 0, 0, 0.4, 0, 0, 1.3, 0, 0.4, 100, hit);
    expect(hit.hit).toBe(false);
  });

  it('命中半球端帽', () => {
    rayVsCapsule(-5, 1.6, 0, 1, 0, 0, 0, 0.4, 0, 0, 1.3, 0, 0.4, 100, hit);
    expect(hit.hit).toBe(true);
    expect(hit.t).toBeCloseTo(5 - Math.sqrt(0.4 * 0.4 - 0.3 * 0.3), 6);
  });

  it('零长度线段退化为球', () => {
    rayVsCapsule(-5, 1, 0, 1, 0, 0, 0, 1, 0, 0, 1, 0, 0.5, 100, hit);
    expect(hit.hit).toBe(true);
    expect(hit.t).toBeCloseTo(4.5, 6);
  });

  it('超出 maxDist 视为未命中，且可重复复用同一个 out', () => {
    rayVsCapsule(-5, 0.85, 0, 1, 0, 0, 0, 0.4, 0, 0, 1.3, 0, 0.4, 4, hit);
    expect(hit.hit).toBe(false);
    rayVsCapsule(-5, 0.85, 0, 1, 0, 0, 0, 0.4, 0, 0, 1.3, 0, 0.4, 100, hit);
    expect(hit.hit).toBe(true);
    clearRayHit(hit);
    expect(hit.hit).toBe(false);
    expect(hit.t).toBe(0);
  });
});

describe('rayVsAabb', () => {
  it('从外侧命中并给出轴法线', () => {
    rayVsAabb(-2, 0.5, 0.5, 1, 0, 0, 0, 0, 0, 1, 1, 1, 100, hit);
    expect(hit.hit).toBe(true);
    expect(hit.t).toBeCloseTo(2, 6);
    expect([hit.nx, hit.ny, hit.nz]).toEqual([-1, 0, 0]);

    rayVsAabb(0.5, -1, 0.5, 0, 1, 0, 0, 0, 0, 1, 1, 1, 100, hit);
    expect(hit.hit).toBe(true);
    expect(hit.t).toBeCloseTo(1, 6);
    expect([hit.nx, hit.ny, hit.nz]).toEqual([0, -1, 0]);
  });

  it('起点在盒内时 t = 0', () => {
    rayVsAabb(0.5, 0.5, 0.5, 1, 0, 0, 0, 0, 0, 1, 1, 1, 100, hit);
    expect(hit.hit).toBe(true);
    expect(hit.t).toBe(0);
  });

  it('平行且在外侧时未命中', () => {
    rayVsAabb(-2, 2, 0.5, 1, 0, 0, 0, 0, 0, 1, 1, 1, 100, hit);
    expect(hit.hit).toBe(false);
  });

  it('谷仓盒体可用于遮挡判定', () => {
    const barn = { minX: -4, maxX: 4, minY: 0, maxY: 5, minZ: -4, maxZ: 4 };
    rayVsAabb(
      -10,
      1,
      0,
      1,
      0,
      0,
      barn.minX,
      barn.minY,
      barn.minZ,
      barn.maxX,
      barn.maxY,
      barn.maxZ,
      100,
      hit,
    );
    expect(hit.hit).toBe(true);
    expect(hit.t).toBeCloseTo(6, 6);
  });
});

describe('rayVsSphere', () => {
  it('正对球心命中，背向未命中', () => {
    rayVsSphere(-5, 0, 0, 1, 0, 0, 0, 0, 0, 1, 100, hit);
    expect(hit.hit).toBe(true);
    expect(hit.t).toBeCloseTo(4, 6);
    expect(hit.nx).toBeCloseTo(-1, 6);

    rayVsSphere(-5, 0, 0, -1, 0, 0, 0, 0, 0, 1, 100, hit);
    expect(hit.hit).toBe(false);
  });
});
