import * as THREE from 'three';

export const MAX_PIXEL_RATIO = 1.5;
export const FRAME_SAMPLE_COUNT = 240;
export const FPS_WINDOW_MS = 1000;

export interface FrameStats {
  fps: number;
  p95IntervalMs: number;
  p95WorkMs: number;
  lastIntervalMs: number;
  frameCount: number;
}

export interface ClientRenderer {
  readonly domElement: HTMLCanvasElement;
  setSize(width: number, height: number): void;
  draw(scene: THREE.Scene, camera: THREE.Camera, nowMs: number): void;
  getStats(): FrameStats;
  dispose(): void;
}

export function createRenderer(options?: { pixelRatioCap?: number }): ClientRenderer {
  const cap = options?.pixelRatioCap ?? MAX_PIXEL_RATIO;
  const webgl = new THREE.WebGLRenderer({ antialias: true, powerPreference: 'high-performance' });
  const deviceRatio = typeof window === 'undefined' ? 1 : window.devicePixelRatio || 1;
  webgl.setPixelRatio(Math.min(deviceRatio, cap));
  webgl.shadowMap.enabled = false;

  const intervals = new Float32Array(FRAME_SAMPLE_COUNT);
  const works = new Float32Array(FRAME_SAMPLE_COUNT);
  const scratch = new Float32Array(FRAME_SAMPLE_COUNT);
  const stats: FrameStats = {
    fps: 0,
    p95IntervalMs: 0,
    p95WorkMs: 0,
    lastIntervalMs: 0,
    frameCount: 0,
  };
  let cursor = 0;
  let filled = 0;
  let lastNowMs = 0;
  let windowStartMs = 0;
  let windowFrames = 0;
  let fps = 0;

  function percentile(source: Float32Array, count: number, ratio: number): number {
    if (count === 0) return 0;
    for (let i = 0; i < count; i += 1) scratch[i] = source[i] ?? 0;
    const view = scratch.subarray(0, count);
    view.sort();
    const index = Math.min(count - 1, Math.max(0, Math.ceil(ratio * count) - 1));
    return Number((view[index] ?? 0).toFixed(2));
  }

  return {
    domElement: webgl.domElement,
    setSize(width: number, height: number): void {
      webgl.setSize(width, height, false);
    },
    draw(scene: THREE.Scene, camera: THREE.Camera, nowMs: number): void {
      const workStartMs = performance.now();
      webgl.render(scene, camera);
      const workMs = performance.now() - workStartMs;
      const intervalMs = lastNowMs === 0 ? 0 : nowMs - lastNowMs;
      lastNowMs = nowMs;
      if (windowStartMs === 0) windowStartMs = nowMs;
      windowFrames += 1;
      if (nowMs - windowStartMs >= FPS_WINDOW_MS) {
        fps = Number(((windowFrames * 1000) / (nowMs - windowStartMs)).toFixed(1));
        windowFrames = 0;
        windowStartMs = nowMs;
      }
      intervals[cursor] = intervalMs;
      works[cursor] = workMs;
      cursor = (cursor + 1) % FRAME_SAMPLE_COUNT;
      filled = Math.min(FRAME_SAMPLE_COUNT, filled + 1);
      stats.lastIntervalMs = Number(intervalMs.toFixed(2));
      stats.frameCount += 1;
      stats.fps = fps;
      if (filled > 0 && stats.frameCount % 30 === 0) {
        stats.p95IntervalMs = percentile(intervals, filled, 0.95);
        stats.p95WorkMs = percentile(works, filled, 0.95);
      }
    },
    getStats(): FrameStats {
      return stats;
    },
    dispose(): void {
      webgl.dispose();
    },
  };
}
