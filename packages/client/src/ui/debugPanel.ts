export const DEBUG_REFRESH_MS = 250;

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
}

export interface DebugPanel {
  update(sample: DebugSample): void;
  readonly visible: boolean;
  toggle(): boolean;
  dispose(): void;
}

export function createDebugPanel(root: HTMLElement, initialVisible: boolean): DebugPanel {
  let visible = initialVisible;
  let lastAtMs = 0;
  let lastText = '';
  root.classList.toggle('debug-hidden', !visible);

  function format(sample: DebugSample): string {
    const pos = sample.localPos;
    const lines = [
      'fps ' +
        sample.fps.toFixed(1) +
        '  p95 ' +
        sample.p95IntervalMs.toFixed(1) +
        'ms  work ' +
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
      'local ' +
        (pos === undefined
          ? 'n/a'
          : pos.x.toFixed(2) + ' ' + pos.y.toFixed(2) + ' ' + pos.z.toFixed(2)),
      'status ' + sample.status + '  room ' + sample.roomCode,
      sample.versionLine,
    ];
    return lines.join('\n');
  }

  return {
    update(sample: DebugSample): void {
      if (!visible) return;
      const atMs = performance.now();
      if (atMs - lastAtMs < DEBUG_REFRESH_MS) return;
      lastAtMs = atMs;
      const text = format(sample);
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
