import { describe, expect, it } from 'vitest';

import { createRng, mulberry32, rngInt, rngRange, type Rng } from './rng.ts';

const AI_16 = [
  0.399809091818, 0.259929052554, 0.635906121461, 0.625139248325, 0.741090158466, 0.505425854819,
  0.030046477914, 0.704443771625, 0.024240954313, 0.988475965802, 0.852434412809, 0.148436452961,
  0.527484830935, 0.500485451659, 0.070353675634, 0.032281862572,
];

const SPAWN_16 = [
  0.741331517231, 0.852969721425, 0.28321096627, 0.876597300638, 0.521247137338, 0.255398312816,
  0.170464602066, 0.560125120915, 0.381560787093, 0.531719918828, 0.066443210933, 0.770622634329,
  0.811038648244, 0.650680826511, 0.509014368057, 0.993988572387,
];

const FX_16 = [
  0.985901473556, 0.199748249957, 0.412884169025, 0.11960497289, 0.323493162636, 0.682248520898,
  0.290773040149, 0.210402516183, 0.642015676247, 0.728429794079, 0.727120561292, 0.134041848127,
  0.250705006067, 0.981994230766, 0.728071568301, 0.08531328314,
];

function take(rng: Rng, count: number): number[] {
  const out: number[] = [];
  for (let i = 0; i < count; i += 1) out.push(Number(rng().toFixed(12)));
  return out;
}

describe('rng', () => {
  it('mulberry32 同种子序列固定', () => {
    expect(take(mulberry32(1234), 8)).toEqual(take(mulberry32(1234), 8));
    expect(take(mulberry32(1234), 8)).not.toEqual(take(mulberry32(1235), 8));
  });

  it('三条流在种子 42 下的前 16 个值（回归快照）', () => {
    expect(take(createRng(42, 'ai'), 16)).toEqual(AI_16);
    expect(take(createRng(42, 'spawn'), 16)).toEqual(SPAWN_16);
    expect(take(createRng(42, 'fx'), 16)).toEqual(FX_16);
  });

  it('三条流互不相同', () => {
    expect(AI_16[0]).not.toBe(SPAWN_16[0]);
    expect(SPAWN_16[0]).not.toBe(FX_16[0]);
    expect(AI_16[0]).not.toBe(FX_16[0]);
  });

  it('取值落在 [0, 1)', () => {
    const rng = createRng(7, 'ai');
    for (let i = 0; i < 5000; i += 1) {
      const value = rng();
      expect(value).toBeGreaterThanOrEqual(0);
      expect(value).toBeLessThan(1);
    }
  });

  it('rngInt / rngRange 落在闭区间内且可以取到两端', () => {
    const ints = new Set<number>();
    const rngA = createRng(9, 'spawn');
    for (let i = 0; i < 400; i += 1) {
      const value = rngInt(rngA, 3, 5);
      expect(Number.isInteger(value)).toBe(true);
      expect(value).toBeGreaterThanOrEqual(3);
      expect(value).toBeLessThanOrEqual(5);
      ints.add(value);
    }
    expect(Array.from(ints).sort()).toEqual([3, 4, 5]);

    const rngB = createRng(11, 'fx');
    for (let i = 0; i < 400; i += 1) {
      const value = rngRange(rngB, -2, 2);
      expect(value).toBeGreaterThanOrEqual(-2);
      expect(value).toBeLessThanOrEqual(2);
    }
  });
});

describe('rng 边界输入', () => {
  it('rngInt 区间非法时返回下界；单点区间恒等于该点', () => {
    const direct = createRng(13, 'ai');
    expect(rngInt(direct, 5, 4)).toBe(5);
    expect(rngInt(direct, 7, 7)).toBe(7);

    const a = createRng(13, 'ai');
    const b = createRng(13, 'ai');
    const other = createRng(13, 'spawn');
    expect(a()).toBeCloseTo(b(), 12);
    expect(other()).not.toBeCloseTo(b(), 12);
  });
});
