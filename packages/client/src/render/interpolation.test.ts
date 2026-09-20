import { describe, expect, it } from 'vitest';

import {
  findBracket,
  interpolationAlpha,
  lerp,
  lerpAngle,
  shortestAngleDelta,
} from './interpolation.ts';

describe('插值数学', () => {
  it('两点之间线性插值', () => {
    expect(lerp(0, 10, 0)).toBe(0);
    expect(lerp(0, 10, 0.5)).toBe(5);
    expect(lerp(0, 10, 1)).toBe(10);
    expect(lerp(-3, 3, 0.25)).toBeCloseTo(-1.5, 6);
  });

  it('角度差走最短弧', () => {
    expect(shortestAngleDelta(0, Math.PI / 2)).toBeCloseTo(Math.PI / 2, 6);
    expect(shortestAngleDelta(3, -3)).toBeCloseTo(0.283185, 5);
    expect(shortestAngleDelta(-3, 3)).toBeCloseTo(-0.283185, 5);
    expect(Math.abs(shortestAngleDelta(0, Math.PI * 3))).toBeCloseTo(Math.PI, 5);
  });

  it('角度插值跨过 ±π 缝隙不绕远路', () => {
    const mid = lerpAngle(3.0, -3.0, 0.5);
    expect(Math.abs(mid) > 3.1).toBe(true);
    expect(shortestAngleDelta(3.0, mid)).toBeCloseTo(0.1415926, 5);
  });

  it('alpha 被夹在 0..1，跨度为零时取 1', () => {
    expect(interpolationAlpha(0, 100, -50)).toBe(0);
    expect(interpolationAlpha(0, 100, 50)).toBe(0.5);
    expect(interpolationAlpha(0, 100, 500)).toBe(1);
    expect(interpolationAlpha(10, 10, 10)).toBe(1);
  });

  it('bracket 定位：低于最早取 0，超出最新取末尾，命中区间取左端', () => {
    const times = [100, 200, 300, 400];
    expect(findBracket(times, 4, 50)).toBe(0);
    expect(findBracket(times, 4, 150)).toBe(0);
    expect(findBracket(times, 4, 250)).toBe(1);
    expect(findBracket(times, 4, 399)).toBe(2);
    expect(findBracket(times, 4, 999)).toBe(2);
    expect(findBracket(times, 1, 999)).toBe(0);
  });
});
