import { describe, expect, it } from 'vitest';

import { FRAME_SAMPLE_COUNT, percentileOf } from './renderer.ts';

/** O06 之前的实现：拷贝到 scratch 后对 subarray 排序。 */
function legacyPercentile(source: Float32Array, count: number, ratio: number): number {
  if (count === 0) return 0;
  const scratch = new Float32Array(FRAME_SAMPLE_COUNT);
  for (let i = 0; i < count; i += 1) scratch[i] = source[i] ?? 0;
  const view = scratch.subarray(0, count);
  view.sort();
  const index = Math.min(count - 1, Math.max(0, Math.ceil(ratio * count) - 1));
  return Number((view[index] ?? 0).toFixed(2));
}

describe('O06 帧分位统计', () => {
  it('原地插入排序与旧实现（拷贝 + sort）在 200 组随机样本上逐值一致', () => {
    let seed = 20260922;
    const random = (): number => {
      seed = (seed * 1103515245 + 12345) % 2147483648;
      return seed / 2147483648;
    };
    for (let round = 0; round < 200; round += 1) {
      const count = 1 + Math.floor(random() * FRAME_SAMPLE_COUNT);
      const source = new Float32Array(FRAME_SAMPLE_COUNT);
      const copy = new Float32Array(FRAME_SAMPLE_COUNT);
      for (let i = 0; i < FRAME_SAMPLE_COUNT; i += 1) {
        const value = i < count ? Math.round(random() * 100000) / 1000 : 9999;
        source[i] = value;
        copy[i] = value;
      }
      for (const ratio of [0.5, 0.9, 0.95, 0.99]) {
        expect(percentileOf(source, count, ratio)).toBe(legacyPercentile(copy, count, ratio));
      }
    }
  });

  it('空样本返回 0；单样本即该样本', () => {
    const source = new Float32Array(FRAME_SAMPLE_COUNT);
    expect(percentileOf(source, 0, 0.95)).toBe(0);
    source[0] = 12.345;
    expect(percentileOf(source, 1, 0.95)).toBe(12.35);
  });
});
