import { percentileOf } from './renderer.ts';

/**
 * 每帧分阶段计时（真人试玩反馈「画面巨卡」的定位工具）。
 *
 * 只做分位数统计，不创建对象、不在热路径上取字符串；结果由 F3 面板展示，
 * 也可由 `?debug=1` 下的 `window.__ac.profiler.summary()` 读取（tools/e2e-cdp.mjs 用）。
 */
export const FRAME_STAGE_NAMES = Object.freeze([
  'input',
  'sync',
  'predict',
  'fx',
  'audio',
  'draw',
  'overlay',
  'hud',
] as const);

export type FrameStage = (typeof FRAME_STAGE_NAMES)[number];

export const FRAME_STAGE_COUNT = FRAME_STAGE_NAMES.length;
export const FRAME_PROFILE_SAMPLES = 240;

export interface FrameProfiler {
  /** 帧开始：清零本帧各段累计。 */
  begin(): void;
  /** 结束上一段并把它记到 `name` 名下。 */
  mark(name: FrameStage): void;
  /** 把本帧各段耗时写入环形缓冲。 */
  end(): void;
  /** 某段的 p95 耗时（ms）。 */
  p95Ms(index: number): number;
  /** 一行摘要，形如 `input 0.1 sync 0.5 …`。 */
  summary(): string;
  reset(): void;
}

export function createFrameProfiler(): FrameProfiler {
  const samples: Float32Array[] = [];
  for (let i = 0; i < FRAME_STAGE_COUNT; i += 1) {
    samples.push(new Float32Array(FRAME_PROFILE_SAMPLES));
  }
  const pending = new Float64Array(FRAME_STAGE_COUNT);
  let cursor = 0;
  let filled = 0;
  let lastMs = 0;

  function p95Of(index: number): number {
    const buffer = samples[index];
    return buffer === undefined ? 0 : percentileOf(buffer, filled, 0.95);
  }

  return {
    begin(): void {
      pending.fill(0);
      lastMs = performance.now();
    },
    mark(name: FrameStage): void {
      const at = performance.now();
      const index = FRAME_STAGE_NAMES.indexOf(name);
      if (index >= 0) pending[index] = (pending[index] ?? 0) + (at - lastMs);
      lastMs = at;
    },
    end(): void {
      for (let i = 0; i < FRAME_STAGE_COUNT; i += 1) {
        const buffer = samples[i];
        if (buffer === undefined) continue;
        buffer[cursor] = pending[i] ?? 0;
      }
      cursor = (cursor + 1) % FRAME_PROFILE_SAMPLES;
      filled = Math.min(FRAME_PROFILE_SAMPLES, filled + 1);
    },
    p95Ms(index: number): number {
      return p95Of(index);
    },
    summary(): string {
      const parts: string[] = [];
      for (let i = 0; i < FRAME_STAGE_COUNT; i += 1) {
        parts.push(String(FRAME_STAGE_NAMES[i]) + ' ' + p95Of(i).toFixed(1));
      }
      return parts.join(' ');
    },
    reset(): void {
      for (const buffer of samples) buffer.fill(0);
      pending.fill(0);
      cursor = 0;
      filled = 0;
    },
  };
}
