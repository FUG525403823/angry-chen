import { describe, expect, it } from 'vitest';

import {
  SUBTITLE_CUES,
  SUBTITLE_MAX_LINES,
  SUBTITLE_MS,
  SUBTITLE_TEXT,
  createSubtitleLog,
  subtitleTextFor,
} from './subtitles.ts';

describe('字幕队列（P09 §5.4）', () => {
  it('只保留最近三条，超时后清除', () => {
    const log = createSubtitleLog();
    log.pushCue('waveStart', 3);
    log.pushCue('chargeWarn');
    log.pushCue('downed');
    log.pushCue('victory');
    expect(SUBTITLE_MAX_LINES).toBe(3);
    expect(log.lines.length).toBe(SUBTITLE_MAX_LINES);
    expect(log.lines[0]).toBe(SUBTITLE_TEXT.victory);
    expect(log.lines[1]).toBe(SUBTITLE_TEXT.downed);
    log.update(SUBTITLE_MS + 1);
    expect(log.lines.length).toBe(0);
    log.update(Number.NaN);
    expect(log.lines.length).toBe(0);
  });

  it('重复字幕只刷新存活时间，不会重复堆叠', () => {
    const log = createSubtitleLog();
    log.pushCue('downed');
    log.update(SUBTITLE_MS - 100);
    log.pushCue('downed');
    log.update(200);
    expect(log.lines).toEqual([SUBTITLE_TEXT.downed]);
    log.update(SUBTITLE_MS);
    expect(log.lines.length).toBe(0);
  });

  it('冲锋预警、波次开始、倒地、胜负四类提示都有文案', () => {
    expect(SUBTITLE_CUES).toContain('chargeWarn');
    expect(SUBTITLE_CUES).toContain('waveStart');
    expect(SUBTITLE_CUES).toContain('downed');
    expect(SUBTITLE_CUES).toContain('victory');
    expect(SUBTITLE_CUES).toContain('defeat');
    for (const cue of SUBTITLE_CUES) expect(SUBTITLE_TEXT[cue].length).toBeGreaterThan(0);
    expect(subtitleTextFor('waveStart', 0)).toBe(SUBTITLE_TEXT.waveStart);
    expect(subtitleTextFor('waveStart', 2)).toBe('第 2 波开始');
    expect(subtitleTextFor('waveStart', 2.4)).toBe('第 2 波开始');
    expect(subtitleTextFor('downed')).toBe(SUBTITLE_TEXT.downed);
  });
});
