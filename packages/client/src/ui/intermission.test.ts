import { describe, expect, it } from 'vitest';

import { formatCountdownMs, intermissionTitleOf, readySummaryText } from './intermission.ts';

describe('波间备战展示', () => {
  it('倒计时按秒保留一位小数，负数归零', () => {
    expect(formatCountdownMs(20000)).toBe('20.0s');
    expect(formatCountdownMs(1500)).toBe('1.5s');
    expect(formatCountdownMs(0)).toBe('0.0s');
    expect(formatCountdownMs(-500)).toBe('0.0s');
  });

  it('准备人数与标题文案', () => {
    expect(readySummaryText(2, 3)).toBe('已准备 2/3');
    expect(intermissionTitleOf(3)).toBe('第 3 波已清空 · 波间备战');
    expect(intermissionTitleOf(0)).toBe('波间备战');
  });
});
