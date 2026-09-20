import type { ViewEntity, ViewStats } from '../net/state.ts';

export const BANNER_TIMEOUT_MS = 4500;
export const MAX_BANNER_EVENTS = 3;
export const STATS_REFRESH_MS = 250;

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
  showBanner(text: string): void;
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
  let lastStatsAtMs = 0;
  let status = 'idle';
  let roomCode = '----';
  let phase = '大厅';
  let hp = 0;

  function showBanner(text: string): void {
    if (text === '') return;
    banner.textContent = text;
    banner.classList.add('banner-visible');
    if (bannerTimer !== undefined) clearTimeout(bannerTimer);
    bannerTimer = setTimeout(() => {
      banner.textContent = '';
      banner.classList.remove('banner-visible');
    }, BANNER_TIMEOUT_MS);
  }

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
      showBanner(names.slice(0, MAX_BANNER_EVENTS).join(' · '));
    },
    showBanner,
    update(data: HudUpdate): void {
      hp = data.local === undefined ? 0 : data.local.hpRatio * 100;
      const statsAtMs = performance.now();
      if (statsAtMs - lastStatsAtMs < STATS_REFRESH_MS) {
        renderLine();
        return;
      }
      lastStatsAtMs = statsAtMs;
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

export interface CombatHudSample {
  readonly mag: number;
  readonly magSize: number;
  readonly reserve: number;
  readonly reloadRatio: number;
  readonly rage: number;
  readonly rageLeftMs: number;
  readonly downed: boolean;
  readonly reviveRatio: number;
  readonly weaponName: string;
}

export interface CombatHud {
  set(sample: CombatHudSample): void;
  pushKill(text: string): void;
  dispose(): void;
}

export function createCombatHud(root: HTMLElement): CombatHud {
  const box = document.createElement('div');
  box.style.cssText =
    'position:absolute;left:14px;bottom:14px;font:12px/1.5 monospace;color:#e9eef5;text-shadow:0 1px 2px #000;pointer-events:none;';
  const rage = document.createElement('div');
  rage.style.cssText =
    'width:180px;height:6px;margin-top:6px;background:#2a2f38;border:1px solid #4a5666;';
  const rageFill = document.createElement('div');
  rageFill.style.cssText = 'height:100%;width:0%;background:#ff6a4d;';
  rage.append(rageFill);
  const overlay = document.createElement('div');
  overlay.style.cssText =
    'position:absolute;inset:0;display:none;align-items:center;justify-content:center;flex-direction:column;gap:10px;background:rgba(120,10,10,0.28);color:#ffd9d0;font:bold 18px/1.4 monospace;text-shadow:0 2px 4px #000;pointer-events:none;';
  const feed = document.createElement('div');
  feed.style.cssText =
    'position:absolute;right:12px;top:150px;display:flex;flex-direction:column;gap:4px;align-items:flex-end;font:12px/1.4 monospace;color:#cfe6ff;text-shadow:0 1px 2px #000;pointer-events:none;';
  root.append(box, rage, overlay, feed);

  let kills: string[] = [];
  function renderKills(): void {
    feed.textContent = kills.join(String.fromCharCode(10));
  }

  return {
    set(sample: CombatHudSample): void {
      const lines = [
        sample.weaponName +
          '  ' +
          String(sample.mag) +
          '/' +
          String(sample.magSize) +
          '  备弹 ' +
          String(sample.reserve),
      ];
      if (sample.reloadRatio > 0)
        lines.push('换弹 ' + String(Math.round(sample.reloadRatio * 100)) + '%');
      lines.push(
        '怒气 ' +
          String(Math.round(sample.rage)) +
          (sample.rageLeftMs > 0 ? '  狂暴 ' + (sample.rageLeftMs / 1000).toFixed(1) + 's' : ''),
      );
      box.textContent = lines.join(String.fromCharCode(10));
      rageFill.style.width = String(Math.max(0, Math.min(100, sample.rage))) + '%';
      overlay.style.display = sample.downed ? 'flex' : 'none';
      if (sample.downed) {
        overlay.textContent =
          sample.reviveRatio > 0
            ? '倒地中：队友正在救援 ' + String(Math.round(sample.reviveRatio * 100)) + '%'
            : '倒地中：等待队友按 E 救援';
      }
    },
    pushKill(text: string): void {
      kills = [text, ...kills].slice(0, 5);
      renderKills();
      window.setTimeout(() => {
        kills = kills.filter((line) => line !== text);
        renderKills();
      }, 5000);
    },
    dispose(): void {
      box.remove();
      rage.remove();
      overlay.remove();
      feed.remove();
    },
  };
}
