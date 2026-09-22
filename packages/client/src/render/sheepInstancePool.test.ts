import { describe, expect, it, vi } from 'vitest';

import type { ViewEntity } from '../net/state.ts';
import { fillSheepInstance } from './entityViews.ts';
import { createChargeWarningPool, createSheepInstancePool } from './sheepInstancePool.ts';
import type { SheepInstance } from './sheepModel.ts';

function fakeEntity(id: number): ViewEntity {
  return {
    id,
    kind: 1,
    pos: { x: id, y: 0.5, z: -id },
    yaw: id / 10,
    hpRatio: 0.75,
    state: 0,
  } as unknown as ViewEntity;
}

const VISUAL = { form: 1, windup: true } as const;

describe('O06 羊群实例环形池', () => {
  it('acquire(0) 连续调用给出不同实例；reset() 后回到同一批引用', () => {
    const pool = createSheepInstancePool(4);
    const a = pool.acquire(0);
    const b = pool.acquire(0);
    const c = pool.acquire(0);
    const d = pool.acquire(0);
    expect(new Set([a, b, c, d]).size).toBe(4);
    expect(pool.size).toBe(4);
    pool.reset();
    expect(pool.acquire(0)).toBe(a);
    expect(pool.acquire(0)).toBe(b);
    expect(pool.acquire(0)).toBe(c);
    expect(pool.acquire(0)).toBe(d);
  });

  it('越界时返回池内最后一个实例，并且只告警一次', () => {
    const warn = vi.spyOn(console, 'warn').mockImplementation(() => {});
    try {
      const pool = createSheepInstancePool(2);
      const first = pool.acquire(0);
      const second = pool.acquire(1);
      expect(first).not.toBe(second);
      expect(pool.acquire(2)).toBe(second);
      expect(pool.acquire(3)).toBe(second);
      expect(warn).toHaveBeenCalledTimes(1);
    } finally {
      warn.mockRestore();
    }
  });

  it('连续 3 帧、同一可见集合下拿到的实例引用集合恒等', () => {
    const pool = createSheepInstancePool(64);
    const entities = [0, 1, 2, 3, 4].map((index) => fakeEntity(index + 10));
    const frames: SheepInstance[][] = [];
    for (let frame = 0; frame < 3; frame += 1) {
      pool.reset();
      const seen: SheepInstance[] = [];
      for (let i = 0; i < entities.length; i += 1) {
        const entity = entities[i];
        if (entity === undefined) continue;
        seen.push(fillSheepInstance(pool.acquire(i), entity, VISUAL));
      }
      frames.push(seen);
    }
    const first = frames[0] ?? [];
    expect(first.length).toBe(entities.length);
    expect(new Set(first).size).toBe(entities.length);
    for (let frame = 1; frame < frames.length; frame += 1) {
      const current = frames[frame] ?? [];
      expect(current.length).toBe(first.length);
      for (let i = 0; i < first.length; i += 1) expect(current[i]).toBe(first[i]);
    }
  });

  it('fillSheepInstance 完整覆写字段（复用不残留上一帧的值）', () => {
    const pool = createSheepInstancePool(2);
    const target = pool.acquire(0);
    fillSheepInstance(target, fakeEntity(3), { form: 0, windup: false });
    expect(target).toEqual({
      id: 3,
      form: 0,
      x: 3,
      y: 0.5,
      z: -3,
      yaw: 0.3,
      hpRatio: 0.75,
      windup: false,
      state: 0,
    });
    fillSheepInstance(target, fakeEntity(9), { form: 3, windup: true });
    expect(target.form).toBe(3);
    expect(target.windup).toBe(true);
    expect(target.id).toBe(9);
  });

  it('蓄力警告池同样复用引用、reset() 后回到同一批', () => {
    const pool = createChargeWarningPool(3);
    const a = pool.acquire(0);
    const b = pool.acquire(1);
    expect(a).not.toBe(b);
    a.id = 7;
    a.radiusM = 1.1;
    pool.reset();
    expect(pool.acquire(0)).toBe(a);
    expect(pool.acquire(1)).toBe(b);
  });
});
