export const DEBUG_REFRESH_MS = 250;
export const DRAW_CALL_BUDGET = 120;
export const TRIANGLE_BUDGET = 180000;
export const PARTICLE_BUDGET = 256;
export const MATERIAL_BUDGET = 24;
export const FRAME_TIME_BUDGET_MS = 20;

export interface DebugSample {
  readonly fps: number;
  readonly p95IntervalMs: number;
  readonly p95WorkMs: number;
  readonly tick: number;
  readonly snapshotsPerSec: number;
  readonly inboundBytesPerSec: number;
  readonly rttMs: number;
  readonly serverRateX10: number;
  readonly liveEntities: number;
  readonly pooledEntities: number;
  readonly localPos: { x: number; y: number; z: number } | undefined;
  readonly status: string;
  readonly roomCode: string;
  readonly versionLine: string;
  readonly predictionErrorM: number;
  readonly predictionMaxErrorM: number;
  readonly hardCorrects: number;
  readonly pendingCommands: number;
  readonly drawCalls: number;
  readonly triangles: number;
  readonly particles: number;
  readonly materialCount: number;
  /** O07：账本未确认的本地开火数。 */
  readonly pendingShots: number;
  /** O07：累计被服务器拒绝的本地开火数。 */
  readonly rejectedShots: number;
  /** O07：本地预测显示值与权威值之差（正常为 `-pendingShots`）。 */
  readonly ammoDivergence: number;
  /** O07：权威值与本地倒计时的重同步次数（换弹 + 狂暴）。 */
  readonly resyncCount: number;
}

export interface DebugPanel {
  update(sample: DebugSample): void;
  readonly visible: boolean;
  toggle(): boolean;
  dispose(): void;
}

function budgetMark(actual: number, budget: number): string {
  return actual > budget ? ' !' : '';
}

export function withinBudget(sample: DebugSample): boolean {
  return (
    sample.p95IntervalMs <= FRAME_TIME_BUDGET_MS &&
    sample.drawCalls <= DRAW_CALL_BUDGET &&
    sample.triangles <= TRIANGLE_BUDGET &&
    sample.particles <= PARTICLE_BUDGET &&
    sample.materialCount <= MATERIAL_BUDGET
  );
}

export function debugLines(sample: DebugSample): readonly string[] {
  const pos = sample.localPos;
  return [
    'fps ' +
      sample.fps.toFixed(1) +
      '  p95 ' +
      sample.p95IntervalMs.toFixed(1) +
      '/' +
      String(FRAME_TIME_BUDGET_MS) +
      'ms' +
      budgetMark(sample.p95IntervalMs, FRAME_TIME_BUDGET_MS) +
      '  work ' +
      sample.p95WorkMs.toFixed(1) +
      'ms',
    'tick ' +
      String(sample.tick) +
      '  snap ' +
      sample.snapshotsPerSec.toFixed(1) +
      '/s  in ' +
      sample.inboundBytesPerSec.toFixed(0) +
      ' B/s',
    'rtt ' + sample.rttMs.toFixed(1) + 'ms  srv ' + (sample.serverRateX10 / 10).toFixed(1) + '/s',
    'entities ' + String(sample.liveEntities) + '/' + String(sample.pooledEntities),
    'draw ' +
      String(sample.drawCalls) +
      '/' +
      String(DRAW_CALL_BUDGET) +
      budgetMark(sample.drawCalls, DRAW_CALL_BUDGET) +
      '  tri ' +
      String(sample.triangles) +
      '/' +
      String(TRIANGLE_BUDGET) +
      budgetMark(sample.triangles, TRIANGLE_BUDGET),
    'particles ' +
      String(sample.particles) +
      '/' +
      String(PARTICLE_BUDGET) +
      budgetMark(sample.particles, PARTICLE_BUDGET) +
      '  mats ' +
      String(sample.materialCount) +
      '/' +
      String(MATERIAL_BUDGET) +
      budgetMark(sample.materialCount, MATERIAL_BUDGET),
    'local ' +
      (pos === undefined
        ? 'n/a'
        : pos.x.toFixed(2) + ' ' + pos.y.toFixed(2) + ' ' + pos.z.toFixed(2)),
    'pred ' +
      sample.predictionErrorM.toFixed(3) +
      'm  max ' +
      sample.predictionMaxErrorM.toFixed(3) +
      'm  hard ' +
      String(sample.hardCorrects) +
      '  pending ' +
      String(sample.pendingCommands),
    'ammo pending ' +
      String(sample.pendingShots) +
      '  rejected ' +
      String(sample.rejectedShots) +
      '  div ' +
      sample.ammoDivergence.toFixed(1) +
      '  resync ' +
      String(sample.resyncCount),
    'status ' + sample.status + '  room ' + sample.roomCode,
    sample.versionLine,
    'budget ' + (withinBudget(sample) ? 'OK' : 'OVER'),
  ];
}

export function createDebugPanel(root: HTMLElement, initialVisible: boolean): DebugPanel {
  let visible = initialVisible;
  let lastAtMs = 0;
  let lastText = '';
  root.classList.toggle('debug-hidden', !visible);

  return {
    update(sample: DebugSample): void {
      if (!visible) return;
      const atMs = performance.now();
      if (atMs - lastAtMs < DEBUG_REFRESH_MS) return;
      lastAtMs = atMs;
      const text = debugLines(sample).join('\n');
      if (text === lastText) return;
      lastText = text;
      root.textContent = text;
    },
    get visible(): boolean {
      return visible;
    },
    toggle(): boolean {
      visible = !visible;
      root.classList.toggle('debug-hidden', !visible);
      if (!visible) root.textContent = '';
      return visible;
    },
    dispose(): void {
      root.textContent = '';
    },
  };
}
