import { describe, expect, it } from 'vitest';

import { FRAME_STAGE_NAMES, createFrameProfiler } from './frameProfiler.ts';

describe('每帧分阶段计时', () => {
  it('按段累计耗时并给出 p95 摘要', () => {
    const profiler = createFrameProfiler();
    for (let frame = 0; frame < 40; frame += 1) {
      profiler.begin();
      const until = performance.now() + 1;
      while (performance.now() < until) {
        // 忙等 1ms，确保 input 段有可测耗时
      }
      for (const name of FRAME_STAGE_NAMES) profiler.mark(name);
      profiler.end();
    }
    expect(profiler.p95Ms(0)).toBeGreaterThan(0.5);
    expect(profiler.p95Ms(1)).toBeGreaterThanOrEqual(0);
    const summary = profiler.summary();
    for (const name of FRAME_STAGE_NAMES) expect(summary).toContain(name + ' ');
  });

  it('reset 之后 p95 归零', () => {
    const profiler = createFrameProfiler();
    profiler.begin();
    profiler.mark('input');
    profiler.end();
    profiler.reset();
    expect(profiler.p95Ms(0)).toBe(0);
  });
});
