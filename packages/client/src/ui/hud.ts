import {
  SUBTITLE_MAX_LINES,
  createSubtitleLog,
  subtitleTextFor,
  type SubtitleCue,
  type SubtitleLog,
} from '../audio/subtitles.ts';
import type { ViewEntity, ViewStats } from '../net/state.ts';

export const BANNER_TIMEOUT_MS = 4500;
export const MAX_BANNER_EVENTS = 3;
export const STATS_REFRESH_MS = 250;
export const CROSSHAIR_MIN_DEG = 0.5;
export const CROSSHAIR_MAX_DEG = 5;
export const CROSSHAIR_MIN_PX = 4;
export const CROSSHAIR_MAX_PX = 24;
export const HEALTH_AFTERIMAGE_MS = 800;
export const HEALTH_AFTERIMAGE_TIME_CONSTANTS = 3;
export const HEALTH_AFTERIMAGE_TAU_S =
  HEALTH_AFTERIMAGE_MS / 1000 / HEALTH_AFTERIMAGE_TIME_CONSTANTS;
export const AMMO_LOW_RATIO = 0.3;
export const KILL_FEED_MS = 3000;
export const KILL_FEED_MAX = 6;
export const DAMAGE_NUMBER_MS = 900;
export const WAVE_SEGMENTS = 10;
export const BOSS_WAVE_INTERVAL = 5;
export const RAGE_FULL = 100;
export const CHARGE_WARNING_MS = 1600;
export const STARTUP_ROOM_CODE = '----';
export const KILL_FEED_FADE_MS = 600;
export const SUBTITLE_LAYER_ID = 'hud-subtitles';
export const SUBTITLE_LAYER_CLASS = 'subtitle-layer';
export const SUBTITLE_LAYER_ACTIVE_CLASS = 'subtitle-layer-active';
export const SUBTITLE_LINE_CLASS = 'subtitle-line';
export const SUBTITLE_LINE_VISIBLE_CLASS = 'subtitle-line-visible';
export const FEED_ICON_CLASS = 'hud-feed-icon';
export const FEED_ENTRY_CLASS = 'hud-feed-entry';
export const FEED_ENTRY_HEADSHOT_CLASS = 'hud-feed-entry-headshot';

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

export function crosshairSpreadPx(spreadDeg: number): number {
  if (!Number.isFinite(spreadDeg)) return CROSSHAIR_MIN_PX;
  const ratio = (spreadDeg - CROSSHAIR_MIN_DEG) / (CROSSHAIR_MAX_DEG - CROSSHAIR_MIN_DEG);
  const clamped = ratio < 0 ? 0 : ratio > 1 ? 1 : ratio;
  return CROSSHAIR_MIN_PX + (CROSSHAIR_MAX_PX - CROSSHAIR_MIN_PX) * clamped;
}

export type AmmoState = 'ok' | 'low' | 'empty';

export function ammoState(mag: number, magSize: number): AmmoState {
  if (mag <= 0) return 'empty';
  if (magSize <= 0) return 'low';
  return mag / magSize < AMMO_LOW_RATIO ? 'low' : 'ok';
}

export function afterimageStep(current: number, target: number, dtMs: number): number {
  if (!(dtMs > 0)) return current;
  const alpha = 1 - Math.exp(-(dtMs / 1000) / HEALTH_AFTERIMAGE_TAU_S);
  return current + (target - current) * alpha;
}

export function waveSegmentBoss(segmentIndex: number): boolean {
  return (segmentIndex + 1) % BOSS_WAVE_INTERVAL === 0;
}

export interface HudCueInput {
  readonly wave: number;
  readonly downed: boolean;
}

export interface HudCue {
  readonly cue: SubtitleCue;
  readonly wave: number;
}

export function transitionCue(previous: HudCueInput, next: HudCueInput): HudCue | undefined {
  if (next.downed && !previous.downed) return { cue: 'downed', wave: 0 };
  if (next.wave > previous.wave && next.wave > 0) return { cue: 'waveStart', wave: next.wave };
  return undefined;
}

export function feedOpacity(leftMs: number): number {
  if (!Number.isFinite(leftMs) || leftMs <= 0) return 0;
  const ratio = leftMs / KILL_FEED_FADE_MS;
  return ratio > 1 ? 1 : ratio;
}

function addElement(parent: HTMLElement, className: string): HTMLDivElement {
  const node = document.createElement('div');
  node.className = className;
  parent.append(node);
  return node;
}

function addSpan(parent: HTMLElement, className: string): HTMLSpanElement {
  const node = document.createElement('span');
  node.className = className;
  parent.append(node);
  return node;
}

export function createHud(root: HTMLElement, banner: HTMLElement): Hud {
  const line = addElement(root, 'hud-line');
  const stats = addElement(root, 'hud-stats');

  let bannerTimer: ReturnType<typeof setTimeout> | undefined;
  let lastLine = '';
  let lastStats = '';
  let lastStatsAtMs = 0;
  let status = 'idle';
  let roomCode = STARTUP_ROOM_CODE;
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
      line.remove();
      stats.remove();
    },
  };
}

export interface CombatHudSample {
  readonly spreadDeg: number;
  readonly hpRatio: number;
  readonly armorRatio: number | undefined;
  readonly mag: number;
  readonly magSize: number;
  readonly reserve: number;
  readonly reloadRatio: number;
  readonly rage: number;
  readonly rageLeftMs: number;
  readonly downed: boolean;
  readonly reviveRatio: number | undefined;
  readonly weaponName: string;
  readonly wave: number;
  readonly bossHpRatio: number | undefined;
}

export interface DamageAnchor {
  readonly x: number;
  readonly y: number;
}

export interface CombatHud {
  set(sample: CombatHudSample): void;
  pushKill(text: string, headshot: boolean): void;
  pushDamage(x: number, y: number, z: number, value: number, headshot: boolean): void;
  setProjector(projector: (x: number, y: number, z: number) => DamageAnchor | undefined): void;
  showChargeWarning(): void;
  pushSubtitle(text: string): void;
  pushSubtitleCue(cue: SubtitleCue, wave?: number): void;
  dispose(): void;
}

interface FeedEntry {
  readonly node: HTMLDivElement;
  leftMs: number;
}

interface FloatingEntry {
  readonly node: HTMLDivElement;
  readonly x: number;
  readonly y: number;
  readonly z: number;
  leftMs: number;
}

function formatDamage(value: number): string {
  return value >= 10 ? String(Math.round(value)) : value.toFixed(1);
}

function clamp01(value: number): number {
  if (!Number.isFinite(value)) return 0;
  if (value < 0) return 0;
  if (value > 1) return 1;
  return value;
}

export function createCombatHud(root: HTMLElement): CombatHud {
  const layer = addElement(root, 'hud-combat');
  layer.id = 'hud-combat';

  const healthBox = addElement(layer, 'hud-health');
  const healthAfter = addElement(healthBox, 'hud-health-after');
  const healthFill = addElement(healthBox, 'hud-health-fill');
  const armorFill = addElement(healthBox, 'hud-armor-fill');
  const healthText = addElement(healthBox, 'hud-health-text');

  const ammoBox = addElement(layer, 'hud-ammo');
  const weaponName = addElement(ammoBox, 'hud-weapon-name');
  const ammoText = addElement(ammoBox, 'hud-ammo-text');
  const ammoReserve = addElement(ammoBox, 'hud-ammo-reserve');

  const rageBox = addElement(layer, 'hud-rage');
  const rageFill = addElement(rageBox, 'hud-rage-fill');
  const rageText = addElement(rageBox, 'hud-rage-text');

  const waveBox = addElement(layer, 'hud-wave');
  const waveSegments: HTMLDivElement[] = [];
  for (let i = 0; i < WAVE_SEGMENTS; i += 1) {
    const segment = addElement(waveBox, 'hud-wave-seg');
    if (waveSegmentBoss(i)) segment.classList.add('hud-wave-boss');
    waveSegments.push(segment);
  }

  const crosshair = addElement(layer, 'hud-crosshair');
  for (const side of ['top', 'bottom', 'left', 'right']) {
    addElement(crosshair, 'hud-crosshair-arm hud-crosshair-' + side);
  }
  const reloadRing = addElement(layer, 'hud-reload-ring');
  const chargeWarning = addElement(layer, 'hud-charge-warning');
  chargeWarning.textContent = subtitleTextFor('chargeWarn');

  const feed = addElement(layer, 'hud-feed');
  const damageLayer = addElement(layer, 'hud-damage');
  const bossBox = addElement(layer, 'hud-boss');
  addElement(bossBox, 'hud-boss-name').textContent = '羊王';
  const bossBar = addElement(bossBox, 'hud-boss-bar');
  const bossFill = addElement(bossBar, 'hud-boss-fill');

  const downedOverlay = addElement(layer, 'hud-downed');
  const downedText = addElement(downedOverlay, 'hud-downed-text');
  const reviveRing = addElement(layer, 'hud-revive-ring');
  const reviveLabel = addElement(layer, 'hud-revive-label');
  reviveRing.style.display = 'none';
  reviveLabel.style.display = 'none';

  const feedEntries: FeedEntry[] = [];
  const floating: FloatingEntry[] = [];
  const subtitleLog: SubtitleLog = createSubtitleLog();
  const subtitleLayer = addElement(root, SUBTITLE_LAYER_CLASS);
  subtitleLayer.id = SUBTITLE_LAYER_ID;
  const subtitleLines: HTMLDivElement[] = [];
  for (let i = 0; i < SUBTITLE_MAX_LINES; i += 1) {
    subtitleLines.push(addElement(subtitleLayer, SUBTITLE_LINE_CLASS));
  }
  let projector: ((x: number, y: number, z: number) => DamageAnchor | undefined) | undefined;
  let afterRatio = 1;
  let lastAtMs = 0;
  let chargeLeftMs = 0;
  let lastWave = -1;
  let lastDowned = false;

  function renderSubtitles(): void {
    const lines = subtitleLog.lines;
    let visible = false;
    for (let i = 0; i < subtitleLines.length; i += 1) {
      const node = subtitleLines[i];
      if (node === undefined) continue;
      const text = lines[i] ?? '';
      if (node.textContent !== text) {
        node.textContent = text;
        node.classList.toggle(SUBTITLE_LINE_VISIBLE_CLASS, text !== '');
      }
      if (text !== '') visible = true;
    }
    subtitleLayer.classList.toggle(SUBTITLE_LAYER_ACTIVE_CLASS, visible);
  }

  function pushSubtitle(text: string): void {
    if (text === '') return;
    subtitleLog.push(text);
    renderSubtitles();
  }

  function trimFeed(): void {
    while (feedEntries.length > KILL_FEED_MAX) {
      const entry = feedEntries.pop();
      entry?.node.remove();
    }
  }

  return {
    set(sample: CombatHudSample): void {
      const atMs = performance.now();
      const dtMs = lastAtMs === 0 ? 0 : atMs - lastAtMs;
      lastAtMs = atMs;
      subtitleLog.update(dtMs);
      const cue = transitionCue(
        { wave: lastWave, downed: lastDowned },
        { wave: sample.wave, downed: sample.downed },
      );
      lastDowned = sample.downed;
      if (cue !== undefined) pushSubtitle(subtitleTextFor(cue.cue, cue.wave));
      const hpRatio = clamp01(sample.hpRatio);
      afterRatio = Math.max(hpRatio, afterimageStep(afterRatio, hpRatio, dtMs));
      healthFill.style.width = (hpRatio * 100).toFixed(1) + '%';
      healthAfter.style.width = (afterRatio * 100).toFixed(1) + '%';
      healthText.textContent = String(Math.round(hpRatio * 100));
      if (sample.armorRatio === undefined) {
        armorFill.style.display = 'none';
      } else {
        armorFill.style.display = 'block';
        armorFill.style.width = (clamp01(sample.armorRatio) * 100).toFixed(1) + '%';
      }

      const state = ammoState(sample.mag, sample.magSize);
      if (weaponName.textContent !== sample.weaponName) {
        weaponName.textContent = sample.weaponName;
      }
      ammoText.textContent = String(Math.max(0, Math.round(sample.mag)));
      ammoReserve.textContent = '备弹 ' + String(Math.max(0, Math.round(sample.reserve)));
      ammoBox.classList.toggle('hud-ammo-low', state === 'low');
      ammoBox.classList.toggle('hud-ammo-empty', state === 'empty');

      const rage = sample.rage < 0 ? 0 : sample.rage > RAGE_FULL ? RAGE_FULL : sample.rage;
      rageFill.style.width = ((rage / RAGE_FULL) * 100).toFixed(1) + '%';
      const berserk = sample.rageLeftMs > 0;
      rageBox.classList.toggle('hud-rage-full', rage >= RAGE_FULL);
      rageBox.classList.toggle('hud-rage-berserk', berserk);
      rageText.textContent = berserk
        ? '狂暴 ' + (sample.rageLeftMs / 1000).toFixed(1) + 's'
        : rage >= RAGE_FULL
          ? '按 F 释放狂暴'
          : '怒气 ' + String(Math.round(rage));

      const reload = clamp01(sample.reloadRatio);
      reloadRing.style.opacity = reload > 0 ? '1' : '0';
      reloadRing.style.setProperty('--reload-turn', (reload * 360).toFixed(0) + 'deg');

      crosshair.style.setProperty('--spread-px', crosshairSpreadPx(sample.spreadDeg).toFixed(1));

      if (sample.wave !== lastWave) {
        lastWave = sample.wave;
        for (let i = 0; i < waveSegments.length; i += 1) {
          const segment = waveSegments[i];
          if (segment === undefined) continue;
          segment.classList.toggle('hud-wave-active', i < sample.wave);
        }
      }

      if (sample.bossHpRatio === undefined) {
        bossBox.classList.remove('hud-boss-visible');
      } else {
        bossBox.classList.add('hud-boss-visible');
        bossFill.style.width = (clamp01(sample.bossHpRatio) * 100).toFixed(1) + '%';
        bossBar.setAttribute('data-hp', String(Math.round(clamp01(sample.bossHpRatio) * 100)));
      }

      downedOverlay.classList.toggle('hud-downed-visible', sample.downed);
      if (sample.downed) downedText.textContent = '倒地中：等待队友按 E 救援';
      const reviving = sample.reviveRatio !== undefined;
      reviveRing.style.display = reviving ? 'block' : 'none';
      reviveLabel.style.display = reviving ? 'block' : 'none';
      if (reviving) {
        const ratio = clamp01(sample.reviveRatio ?? 0);
        reviveRing.style.setProperty('--revive-turn', (ratio * 360).toFixed(0) + 'deg');
        reviveLabel.textContent = '救援 ' + Math.round(ratio * 100) + '%';
      }

      if (chargeLeftMs > 0) {
        chargeLeftMs = Math.max(0, chargeLeftMs - dtMs);
        if (chargeLeftMs === 0) chargeWarning.classList.remove('hud-charge-visible');
      }

      for (let i = feedEntries.length - 1; i >= 0; i -= 1) {
        const entry = feedEntries[i];
        if (entry === undefined) continue;
        entry.leftMs -= dtMs;
        if (entry.leftMs > 0) {
          entry.node.style.opacity = feedOpacity(entry.leftMs).toFixed(2);
          continue;
        }
        entry.node.remove();
        feedEntries.splice(i, 1);
      }
      renderSubtitles();

      for (let i = floating.length - 1; i >= 0; i -= 1) {
        const entry = floating[i];
        if (entry === undefined) continue;
        entry.leftMs -= dtMs;
        if (entry.leftMs <= 0) {
          entry.node.remove();
          floating.splice(i, 1);
          continue;
        }
        if (projector === undefined) continue;
        const anchor = projector(entry.x, entry.y, entry.z);
        if (anchor === undefined) continue;
        entry.node.style.transform =
          'translate(-50%,-50%) translate(' +
          anchor.x.toFixed(0) +
          'px,' +
          anchor.y.toFixed(0) +
          'px)';
        entry.node.style.opacity = Math.min(1, entry.leftMs / (DAMAGE_NUMBER_MS * 0.5)).toFixed(2);
      }
    },
    pushKill(text: string, headshot: boolean): void {
      const node = addElement(
        feed,
        FEED_ENTRY_CLASS + (headshot ? ' ' + FEED_ENTRY_HEADSHOT_CLASS : ''),
      );
      if (headshot) addSpan(node, FEED_ICON_CLASS);
      const label = addSpan(node, 'hud-feed-text');
      label.textContent = text;
      node.style.opacity = '1';
      feedEntries.unshift({ node, leftMs: KILL_FEED_MS });
      trimFeed();
    },
    pushDamage(x, y, z, value, headshot): void {
      const node = addElement(
        damageLayer,
        'hud-damage-number' + (headshot ? ' hud-damage-headshot' : ''),
      );
      node.textContent = formatDamage(value);
      floating.push({ node, x, y, z, leftMs: DAMAGE_NUMBER_MS });
    },
    setProjector(next): void {
      projector = next;
    },
    showChargeWarning(): void {
      chargeLeftMs = CHARGE_WARNING_MS;
      chargeWarning.classList.add('hud-charge-visible');
      pushSubtitle(subtitleTextFor('chargeWarn'));
    },
    pushSubtitle,
    pushSubtitleCue(cue: SubtitleCue, wave = 0): void {
      pushSubtitle(subtitleTextFor(cue, wave));
    },
    dispose(): void {
      for (const entry of feedEntries) entry.node.remove();
      for (const entry of floating) entry.node.remove();
      feedEntries.length = 0;
      floating.length = 0;
      subtitleLog.clear();
      subtitleLayer.remove();
      layer.remove();
    },
  };
}
