import type { ViewEntity, ViewStats } from '../net/state.ts';

export const BANNER_TIMEOUT_MS = 4500;
export const MAX_BANNER_EVENTS = 3;

export interface HudUpdate {
  readonly local: ViewEntity | undefined;
  readonly stats: ViewStats;
  readonly fps: number;
  readonly liveEntities: number;
  readonly localPos: { x: number; y: number; z: number } | undefined;
}

export interface Hud {
  setStatus(text: string): void;
  setRoomCode(code: string): void;
  setPhase(text: string): void;
  pushEvents(names: readonly string[]): void;
  update(data: HudUpdate): void;
  dispose(): void;
}

export function createHud(root: HTMLElement, banner: HTMLElement): Hud {
  const line = document.createElement('div');
  line.className = 'hud-line';
  const stats = document.createElement('div');
  stats.className = 'hud-stats';
  root.replaceChildren(line, stats);

  let bannerTimer: ReturnType<typeof setTimeout> | undefined;
  let lastLine = '';
  let lastStats = '';
  let status = 'idle';
  let roomCode = '----';
  let phase = '大厅';
  let hp = 0;

  function renderLine(): void {
    const text = roomCode + ' · ' + phase + ' · ' + status + ' · HP ' + String(Math.round(hp));
    if (text === lastLine) return;
    lastLine = text;
    line.textContent = text;
  }

  return {
    setStatus(text: string): void {
      status = text;
      renderLine();
    },
    setRoomCode(code: string): void {
      roomCode = code;
      renderLine();
    },
    setPhase(text: string): void {
      phase = text;
      renderLine();
    },
    pushEvents(names: readonly string[]): void {
      if (names.length === 0) return;
      banner.textContent = names.slice(0, MAX_BANNER_EVENTS).join(' · ');
      banner.classList.add('banner-visible');
      if (bannerTimer !== undefined) clearTimeout(bannerTimer);
      bannerTimer = setTimeout(() => {
        banner.textContent = '';
        banner.classList.remove('banner-visible');
      }, BANNER_TIMEOUT_MS);
    },
    update(data: HudUpdate): void {
      hp = data.local === undefined ? 0 : data.local.hpRatio * 100;
      const pos = data.localPos;
      const text =
        'FPS ' +
        data.fps.toFixed(0) +
        ' · 实体 ' +
        String(data.liveEntities) +
        ' · 快照 ' +
        data.stats.snapshotsPerSec.toFixed(1) +
        '/s · RTT ' +
        data.stats.rttMs.toFixed(0) +
        'ms' +
        (pos === undefined ? '' : ' · ' + pos.x.toFixed(1) + ',' + pos.z.toFixed(1));
      if (text !== lastStats) {
        lastStats = text;
        stats.textContent = text;
      }
      renderLine();
    },
    dispose(): void {
      if (bannerTimer !== undefined) clearTimeout(bannerTimer);
      banner.textContent = '';
    },
  };
}
