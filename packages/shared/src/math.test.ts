import { describe, expect, it } from 'vitest';

import {
  addScaled,
  approxEq,
  clamp01,
  copyVec3,
  createVec3,
  forwardFromYaw,
  length,
  lengthSq,
  normalize,
  rightFromYaw,
  wrapAngle,
  yawPitchToDirection,
} from './math.ts';

describe('math', () => {
  it('wrapAngle 把角度折算到 (-PI, PI]', () => {
    expect(wrapAngle(0)).toBe(0);
    expect(wrapAngle(0.25)).toBeCloseTo(0.25, 12);
    expect(wrapAngle(Math.PI * 2 + 0.25)).toBeCloseTo(0.25, 12);
    expect(wrapAngle(-Math.PI * 2 - 0.25)).toBeCloseTo(-0.25, 12);
    expect(wrapAngle(Math.PI * 3)).toBeCloseTo(Math.PI, 12);
    expect(wrapAngle(Number.NaN)).toBe(0);
    expect(wrapAngle(Number.POSITIVE_INFINITY)).toBe(0);
  });

  it('yawPitchToDirection 是单位向量且约定为 yaw=0 指向 +Z', () => {
    const out = createVec3();
    yawPitchToDirection(out, 0, 0);
    expect(out.x).toBeCloseTo(0, 12);
    expect(out.y).toBeCloseTo(0, 12);
    expect(out.z).toBeCloseTo(1, 12);

    yawPitchToDirection(out, Math.PI / 2, 0);
    expect(out.x).toBeCloseTo(1, 12);
    expect(out.z).toBeCloseTo(0, 12);

    yawPitchToDirection(out, 0, Math.PI / 2);
    expect(out.y).toBeCloseTo(1, 12);

    for (const yaw of [0, 0.3, -1.1, 2.5]) {
      for (const pitch of [0, 0.7, -0.4]) {
        yawPitchToDirection(out, yaw, pitch);
        expect(length(out)).toBeCloseTo(1, 12);
      }
    }
  });

  it('forward 与 right 正交', () => {
    const forward = createVec3();
    const right = createVec3();
    for (const yaw of [0, 0.75, -2.2]) {
      forwardFromYaw(forward, yaw);
      rightFromYaw(right, yaw);
      expect(forward.x * right.x + forward.y * right.y + forward.z * right.z).toBeCloseTo(0, 12);
    }
  });

  it('addScaled 写回 out、不修改入参、不新建对象', () => {
    const out = createVec3(1, 2, 3);
    const base = createVec3(1, 1, 1);
    const dir = createVec3(0, 1, 0);
    const returned = addScaled(out, base, dir, 5);
    expect(returned).toBe(out);
    expect(out).toEqual({ x: 1, y: 6, z: 1 });
    expect(base).toEqual({ x: 1, y: 1, z: 1 });
    expect(dir).toEqual({ x: 0, y: 1, z: 0 });
  });

  it('normalize 对零向量安全', () => {
    const out = createVec3(9, 9, 9);
    normalize(out, createVec3(0, 0, 0));
    expect(out).toEqual({ x: 0, y: 0, z: 0 });

    normalize(out, createVec3(0, 0, 4));
    expect(out).toEqual({ x: 0, y: 0, z: 1 });
  });
});

describe('math 辅助函数', () => {
  it('createVec3 默认零向量；copyVec3 / lengthSq / clamp01 / approxEq 行为正确', () => {
    expect(createVec3()).toEqual({ x: 0, y: 0, z: 0 });

    const source = createVec3(3, 4, 12);
    const copy = createVec3();
    expect(copyVec3(copy, source)).toBe(copy);
    expect(copy).toEqual({ x: 3, y: 4, z: 12 });

    expect(lengthSq(source)).toBe(169);
    expect(length(source)).toBeCloseTo(13, 12);

    expect(clamp01(-1)).toBe(0);
    expect(clamp01(0.5)).toBe(0.5);
    expect(clamp01(2)).toBe(1);

    expect(approxEq(1, 1 + 1e-12)).toBe(true);
    expect(approxEq(1, 1.1)).toBe(false);
  });
});
