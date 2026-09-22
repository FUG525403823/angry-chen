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
  /** O06：脏检查——先比较（按 UI 精度取整）再拼字符串。 */
  let lastHpRounded = Number.NaN;
  let lineDirty = false;
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
    const hpRounded = Math.round(hp);
    if (!lineDirty && hpRounded === lastHpRounded) return;
    lineDirty = false;
    lastHpRounded = hpRounded;
    line.textContent = roomCode + ' · ' + phase + ' · ' + status + ' · HP ' + String(hpRounded);
  }

  return {
    setStatus(text: string): void {
      status = text;
      lineDirty = true;
      renderLine();
    },
    setRoomCode(code: string): void {
      roomCode = code;
      lineDirty = true;
      renderLine();
    },
    setPhase(text: string): void {
      phase = text;
      lineDirty = true;
      renderLine();
    },
    pushEvents(names: readonly string[]): void {
      const count = names.length < MAX_BANNER_EVENTS ? names.length : MAX_BANNER_EVENTS;
      let text = '';
      for (let i = 0; i < count; i += 1) {
        const name = names[i];
        if (name === undefined) continue;
        text = text === '' ? name : text + ' · ' + name;
      }
      showBanner(text);
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
  // O06：DOM 脏检查缓存（数值按 UI 显示精度取整后比较，布尔/字符串按变更比较）。
  let healthWidthPct = Number.NaN;
  let afterWidthPct = Number.NaN;
  let healthTextPct = Number.NaN;
  let armorVisible: boolean | undefined;
  let armorWidthPct = Number.NaN;
  let ammoMag = Number.NaN;
  let ammoReserveCount = Number.NaN;
  let rageWidthPct = Number.NaN;
  let rageTextKey = '';
  let reloadVisible: boolean | undefined;
  let reloadTurnDeg = Number.NaN;
  let crosshairPx = Number.NaN;
  let bossVisible: boolean | undefined;
  let bossWidthPct = Number.NaN;
  let bossHpAttr = Number.NaN;
  let downedTextShown = false;
  let reviveVisible: boolean | undefined;
  let reviveTurnDeg = Number.NaN;
  let reviveTextPct = Number.NaN;

  function round1(value: number): number {
    return Math.round(value * 10) / 10;
  }

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
      const healthPct = round1(hpRatio * 100);
      if (healthPct !== healthWidthPct) {
        healthWidthPct = healthPct;
        healthFill.style.width = healthPct.toFixed(1) + '%';
      }
      const afterPct = round1(afterRatio * 100);
      if (afterPct !== afterWidthPct) {
        afterWidthPct = afterPct;
        healthAfter.style.width = afterPct.toFixed(1) + '%';
      }
      const healthTextValue = Math.round(hpRatio * 100);
      if (healthTextValue !== healthTextPct) {
        healthTextPct = healthTextValue;
        healthText.textContent = String(healthTextValue);
      }
      const armorOn = sample.armorRatio !== undefined;
      if (armorOn !== armorVisible) {
        armorVisible = armorOn;
        armorFill.style.display = armorOn ? 'block' : 'none';
      }
      if (armorOn) {
        const armorPct = round1(clamp01(sample.armorRatio ?? 0) * 100);
        if (armorPct !== armorWidthPct) {
          armorWidthPct = armorPct;
          armorFill.style.width = armorPct.toFixed(1) + '%';
        }
      }

      const state = ammoState(sample.mag, sample.magSize);
      if (weaponName.textContent !== sample.weaponName) {
        weaponName.textContent = sample.weaponName;
      }
      const magValue = Math.max(0, Math.round(sample.mag));
      if (magValue !== ammoMag) {
        ammoMag = magValue;
        ammoText.textContent = String(magValue);
      }
      const reserveValue = Math.max(0, Math.round(sample.reserve));
      if (reserveValue !== ammoReserveCount) {
        ammoReserveCount = reserveValue;
        ammoReserve.textContent = '备弹 ' + String(reserveValue);
      }
      ammoBox.classList.toggle('hud-ammo-low', state === 'low');
      ammoBox.classList.toggle('hud-ammo-empty', state === 'empty');

      const rage = sample.rage < 0 ? 0 : sample.rage > RAGE_FULL ? RAGE_FULL : sample.rage;
      const ragePct = round1((rage / RAGE_FULL) * 100);
      if (ragePct !== rageWidthPct) {
        rageWidthPct = ragePct;
        rageFill.style.width = ragePct.toFixed(1) + '%';
      }
      const berserk = sample.rageLeftMs > 0;
      rageBox.classList.toggle('hud-rage-full', rage >= RAGE_FULL);
      rageBox.classList.toggle('hud-rage-berserk', berserk);
      const rageRounded = Math.round(rage);
      const rageKey = berserk
        ? 'b' + (sample.rageLeftMs / 1000).toFixed(1)
        : rage >= RAGE_FULL
          ? 'f'
          : 'r' + String(rageRounded);
      if (rageKey !== rageTextKey) {
        rageTextKey = rageKey;
        rageText.textContent = berserk
          ? '狂暴 ' + (sample.rageLeftMs / 1000).toFixed(1) + 's'
          : rage >= RAGE_FULL
            ? '按 F 释放狂暴'
            : '怒气 ' + String(rageRounded);
      }

      const reload = clamp01(sample.reloadRatio);
      const reloadOn = reload > 0;
      if (reloadOn !== reloadVisible) {
        reloadVisible = reloadOn;
        reloadRing.style.opacity = reloadOn ? '1' : '0';
      }
      const reloadDeg = Math.round(reload * 360);
      if (reloadDeg !== reloadTurnDeg) {
        reloadTurnDeg = reloadDeg;
        reloadRing.style.setProperty('--reload-turn', String(reloadDeg) + 'deg');
      }

      const spreadPx = round1(crosshairSpreadPx(sample.spreadDeg));
      if (spreadPx !== crosshairPx) {
        crosshairPx = spreadPx;
        crosshair.style.setProperty('--spread-px', spreadPx.toFixed(1));
      }

      if (sample.wave !== lastWave) {
        lastWave = sample.wave;
        for (let i = 0; i < waveSegments.length; i += 1) {
          const segment = waveSegments[i];
          if (segment === undefined) continue;
          segment.classList.toggle('hud-wave-active', i < sample.wave);
        }
      }

      const bossOn = sample.bossHpRatio !== undefined;
      if (bossOn !== bossVisible) {
        bossVisible = bossOn;
        if (bossOn) bossBox.classList.add('hud-boss-visible');
        else bossBox.classList.remove('hud-boss-visible');
      }
      if (bossOn) {
        const bossPct = round1(clamp01(sample.bossHpRatio ?? 0) * 100);
        if (bossPct !== bossWidthPct) {
          bossWidthPct = bossPct;
          bossFill.style.width = bossPct.toFixed(1) + '%';
        }
        const bossHp = Math.round(clamp01(sample.bossHpRatio ?? 0) * 100);
        if (bossHp !== bossHpAttr) {
          bossHpAttr = bossHp;
          bossBar.setAttribute('data-hp', String(bossHp));
        }
      }

      downedOverlay.classList.toggle('hud-downed-visible', sample.downed);
      if (sample.downed && !downedTextShown) {
        downedTextShown = true;
        downedText.textContent = '倒地中：等待队友按 E 救援';
      }
      const reviving = sample.reviveRatio !== undefined;
      if (reviving !== reviveVisible) {
        reviveVisible = reviving;
        reviveRing.style.display = reviving ? 'block' : 'none';
        reviveLabel.style.display = reviving ? 'block' : 'none';
      }
      if (reviving) {
        const ratio = clamp01(sample.reviveRatio ?? 0);
        const reviveDeg = Math.round(ratio * 360);
        if (reviveDeg !== reviveTurnDeg) {
          reviveTurnDeg = reviveDeg;
          reviveRing.style.setProperty('--revive-turn', String(reviveDeg) + 'deg');
        }
        const revivePct = Math.round(ratio * 100);
        if (revivePct !== reviveTextPct) {
          reviveTextPct = revivePct;
          reviveLabel.textContent = '救援 ' + String(revivePct) + '%';
        }
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
