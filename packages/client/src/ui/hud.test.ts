import { describe, expect, it } from 'vitest';

import {
  AMMO_LOW_RATIO,
  BOSS_WAVE_INTERVAL,
  CROSSHAIR_MAX_DEG,
  CROSSHAIR_MAX_PX,
  CROSSHAIR_MIN_DEG,
  CROSSHAIR_MIN_PX,
  FEED_ICON_CLASS,
  KILL_FEED_FADE_MS,
  SUBTITLE_LINE_VISIBLE_CLASS,
  WAVE_SEGMENTS,
  afterimageStep,
  ammoState,
  createCombatHud,
  createHud,
  crosshairSpreadPx,
  feedOpacity,
  transitionCue,
  waveSegmentBoss,
  type CombatHudSample,
} from './hud.ts';

type MutableSample = { -readonly [K in keyof CombatHudSample]: CombatHudSample[K] };

function sample(): MutableSample {
  return {
    spreadDeg: 0,
    hpRatio: 1,
    armorRatio: undefined,
    mag: 10,
    magSize: 10,
    reserve: 60,
    reloadRatio: 0,
    rage: 0,
    rageLeftMs: 0,
    downed: false,
    reviveRatio: undefined,
    weaponName: '手枪',
    wave: 0,
    bossHpRatio: undefined,
  };
}

interface FakeStyle {
  setProperty(name: string, value: string): void;
  getPropertyValue(name: string): string;
  [name: string]: unknown;
}

interface FakeNode {
  className: string;
  textContent: string;
  id: string;
  readonly style: FakeStyle;
  readonly children: FakeNode[];
  readonly classList: {
    add(...names: string[]): void;
    remove(...names: string[]): void;
    toggle(name: string, force?: boolean): boolean;
    contains(name: string): boolean;
  };
  parent: FakeNode | null;
  append(...nodes: FakeNode[]): void;
  remove(): void;
  setAttribute(name: string, value: string): void;
}

function createFakeStyle(): FakeStyle {
  const values: Record<string, string> = {};
  return {
    setProperty(name: string, value: string): void {
      values[name] = value;
    },
    getPropertyValue(name: string): string {
      return values[name] ?? '';
    },
  };
}

function createFakeNode(): FakeNode {
  const classes = new Set<string>();
  const element: FakeNode = {
    get className(): string {
      return [...classes].join(' ');
    },
    set className(value: string) {
      classes.clear();
      for (const name of value.split(' ')) if (name !== '') classes.add(name);
    },
    textContent: '',
    id: '',
    style: createFakeStyle(),
    children: [],
    classList: {
      add(...names: string[]): void {
        for (const name of names) classes.add(name);
      },
      remove(...names: string[]): void {
        for (const name of names) classes.delete(name);
      },
      toggle(name: string, force?: boolean): boolean {
        const next = force ?? !classes.has(name);
        if (next) classes.add(name);
        else classes.delete(name);
        return next;
      },
      contains(name: string): boolean {
        return classes.has(name);
      },
    },
    parent: null,
    append(...nodes: FakeNode[]): void {
      for (const node of nodes) {
        node.parent = element;
        element.children.push(node);
      }
    },
    remove(): void {
      const parent = element.parent;
      if (parent === null) return;
      const index = parent.children.indexOf(element);
      if (index >= 0) parent.children.splice(index, 1);
      element.parent = null;
    },
    setAttribute(): void {
      return;
    },
  };
  return element;
}

function installDomStub(): () => void {
  const previous = Object.getOwnPropertyDescriptor(globalThis, 'document');
  Object.defineProperty(globalThis, 'document', {
    configurable: true,
    writable: true,
    value: {
      createElement(): unknown {
        return createFakeNode();
      },
      body: createFakeNode(),
    },
  });
  return (): void => {
    if (previous === undefined) {
      Reflect.deleteProperty(globalThis, 'document');
      return;
    }
    Object.defineProperty(globalThis, 'document', previous);
  };
}

function collect(node: Element, className: string, out: Element[] = []): Element[] {
  if (node.classList.contains(className) || node.className.split(' ').includes(className)) {
    out.push(node);
  }
  const children = node.children;
  for (let i = 0; i < children.length; i += 1) {
    const child = children[i];
    if (child !== undefined) collect(child, className, out);
  }
  return out;
}

describe('HUD 呈现规则（P09 §5.2）', () => {
  it('准星扩散把 0.5°–5° 映射到 4px–24px', () => {
    expect(crosshairSpreadPx(CROSSHAIR_MIN_DEG)).toBe(CROSSHAIR_MIN_PX);
    expect(crosshairSpreadPx(CROSSHAIR_MAX_DEG)).toBe(CROSSHAIR_MAX_PX);
    expect(crosshairSpreadPx(0)).toBe(CROSSHAIR_MIN_PX);
    expect(crosshairSpreadPx(9)).toBe(CROSSHAIR_MAX_PX);
    expect(crosshairSpreadPx(Number.NaN)).toBe(CROSSHAIR_MIN_PX);
    const mid = crosshairSpreadPx((CROSSHAIR_MIN_DEG + CROSSHAIR_MAX_DEG) / 2);
    expect(mid).toBeCloseTo((CROSSHAIR_MIN_PX + CROSSHAIR_MAX_PX) / 2, 6);
  });

  it('弹药低于 30% 变黄、空弹匣变红', () => {
    expect(ammoState(8, 10)).toBe('ok');
    expect(ammoState(Number(AMMO_LOW_RATIO.toFixed(2)) * 10 - 1, 10)).toBe('low');
    expect(ammoState(0, 10)).toBe('empty');
    expect(ammoState(5, 0)).toBe('low');
  });

  it('血条残影只追赶不回填，且在 0.8s 量级内收敛', () => {
    expect(afterimageStep(1, 0.2, 0)).toBe(1);
    const after = afterimageStep(1, 0.2, 100);
    expect(after).toBeGreaterThan(0.2);
    expect(after).toBeLessThan(1);
    let current = 1;
    for (let i = 0; i < 200; i += 1) current = afterimageStep(current, 0.2, 16.7);
    expect(current).toBeCloseTo(0.2, 4);
    let eightHundred = 1;
    for (let i = 0; i < 48; i += 1) eightHundred = afterimageStep(eightHundred, 0.2, 16.7);
    expect(Math.abs(eightHundred - 0.2)).toBeLessThan(0.05);
  });

  it('波次条 10 段且每 5 段标注 Boss', () => {
    expect(WAVE_SEGMENTS).toBe(10);
    const bosses: number[] = [];
    for (let i = 0; i < WAVE_SEGMENTS; i += 1) if (waveSegmentBoss(i)) bosses.push(i + 1);
    expect(bosses).toEqual([BOSS_WAVE_INTERVAL, BOSS_WAVE_INTERVAL * 2]);
  });

  it('击杀提示 3 秒后在最后 0.6 秒内淡出', () => {
    expect(feedOpacity(KILL_FEED_FADE_MS + 100)).toBe(1);
    expect(feedOpacity(KILL_FEED_FADE_MS / 2)).toBeCloseTo(0.5, 6);
    expect(feedOpacity(0)).toBe(0);
    expect(feedOpacity(Number.NaN)).toBe(0);
  });
});

describe('HUD 字幕触发（P09 §5.4）', () => {
  it('新波次与倒地才触发字幕，重复状态不重复触发', () => {
    expect(transitionCue({ wave: 0, downed: false }, { wave: 0, downed: false })).toBeUndefined();
    expect(transitionCue({ wave: 1, downed: false }, { wave: 1, downed: false })).toBeUndefined();
    expect(transitionCue({ wave: 1, downed: false }, { wave: 2, downed: false })).toEqual({
      cue: 'waveStart',
      wave: 2,
    });
    expect(transitionCue({ wave: -1, downed: false }, { wave: 0, downed: false })).toBeUndefined();
    expect(transitionCue({ wave: 1, downed: false }, { wave: 1, downed: true })).toEqual({
      cue: 'downed',
      wave: 0,
    });
    expect(transitionCue({ wave: 1, downed: false }, { wave: 2, downed: true })).toEqual({
      cue: 'downed',
      wave: 0,
    });
    expect(transitionCue({ wave: 1, downed: true }, { wave: 2, downed: true })).toEqual({
      cue: 'waveStart',
      wave: 2,
    });
  });

  it('波次推进、倒地与胜负都会渲染到字幕层，最多保留三条', () => {
    const uninstall = installDomStub();
    try {
      const root = document.createElement('div');
      const hud = createCombatHud(root);
      const lines = collect(root, 'subtitle-line');
      expect(lines.length).toBe(3);

      const wave = sample();
      wave.wave = 2;
      hud.set(wave);
      expect(lines[0]?.textContent).toBe('第 2 波开始');
      expect(lines[0]?.classList.contains(SUBTITLE_LINE_VISIBLE_CLASS)).toBe(true);

      const downed = sample();
      downed.wave = 2;
      downed.downed = true;
      hud.set(downed);
      expect(lines[0]?.textContent).toBe('你已倒地，等待救援');
      expect(lines[1]?.textContent).toBe('第 2 波开始');

      hud.pushSubtitleCue('victory');
      expect(lines[0]?.textContent).toBe('胜利：守住牧场');
      hud.pushSubtitleCue('defeat');
      expect(lines[0]?.textContent).toBe('失败：羊群获胜');
      hud.pushSubtitle('额外提示');
      expect(lines.map((line) => line.textContent)).toEqual([
        '额外提示',
        '失败：羊群获胜',
        '胜利：守住牧场',
      ]);
      hud.dispose();
      expect(collect(root, 'subtitle-line').length).toBe(0);
    } finally {
      uninstall();
    }
  });
});

describe('HUD 战斗层渲染（P09 §5.2）', () => {
  it('准星扩散、弹药阈值、波次分段与 Boss 血条写入视图', () => {
    const uninstall = installDomStub();
    try {
      const root = document.createElement('div');
      const hud = createCombatHud(root);
      const empty = sample();
      empty.spreadDeg = CROSSHAIR_MAX_DEG;
      empty.mag = 0;
      empty.wave = 5;
      empty.bossHpRatio = 0.5;
      hud.set(empty);

      const crosshair = collect(root, 'hud-crosshair');
      expect(crosshair.length).toBe(1);
      expect(collect(root, 'hud-crosshair-arm').length).toBe(4);
      // 扩散值必须以长度（px）写进自定义属性：CSS 侧用 calc(长度 + 长度) 算臂位，
      // 裸数字会让整条 calc 失效、上/左两臂堆在中心（"不是十字准星"）。
      const crosshairNode = crosshair[0] as unknown as FakeNode;
      expect(crosshairNode.style.getPropertyValue('--spread-px')).toBe(
        String(CROSSHAIR_MAX_PX) + 'px',
      );
      const ammo = collect(root, 'hud-ammo')[0];
      expect(ammo?.classList.contains('hud-ammo-empty')).toBe(true);
      expect(ammo?.classList.contains('hud-ammo-low')).toBe(false);
      expect(collect(root, 'hud-wave-seg').length).toBe(WAVE_SEGMENTS);
      expect(collect(root, 'hud-wave-active').length).toBe(5);
      expect(collect(root, 'hud-wave-boss').length).toBe(2);

      const boss = collect(root, 'hud-boss')[0];
      expect(boss?.classList.contains('hud-boss-visible')).toBe(true);

      const low = sample();
      low.mag = 2;
      low.wave = 5;
      hud.set(low);
      expect(ammo?.classList.contains('hud-ammo-low')).toBe(true);
      expect(ammo?.classList.contains('hud-ammo-empty')).toBe(false);
      expect(boss?.classList.contains('hud-boss-visible')).toBe(false);
      hud.dispose();
    } finally {
      uninstall();
    }
  });

  it('爆头击杀条目带图标，普通击杀不带', () => {
    const uninstall = installDomStub();
    try {
      const root = document.createElement('div');
      const hud = createCombatHud(root);
      hud.pushKill('你击杀 羊王', true);
      expect(collect(root, 'hud-feed-entry').length).toBe(1);
      expect(collect(root, FEED_ICON_CLASS).length).toBe(1);
      hud.pushKill('队友击杀 咩咩兵', false);
      expect(collect(root, 'hud-feed-entry').length).toBe(2);
      expect(collect(root, FEED_ICON_CLASS).length).toBe(1);
      hud.dispose();
      expect(collect(root, 'hud-feed-entry').length).toBe(0);
    } finally {
      uninstall();
    }
  });
});

function countWrites(target: FakeNode, property: string): { reads: () => number } {
  let count = 0;
  let value = '';
  Object.defineProperty(target, property, {
    configurable: true,
    get: () => value,
    set: (next: string) => {
      count += 1;
      value = next;
    },
  });
  return { reads: () => count };
}

function countStyleWrites(target: FakeNode, property: string): { reads: () => number } {
  let count = 0;
  let value = '';
  Object.defineProperty(target.style, property, {
    configurable: true,
    get: () => value,
    set: (next: string) => {
      count += 1;
      value = next;
    },
  });
  return { reads: () => count };
}

describe('O06 HUD 脏检查（同值不触达 DOM）', () => {
  it('同值两次 set() 只写一次 style.width / textContent，值变化时必须写', () => {
    const uninstall = installDomStub();
    try {
      const root = document.createElement('div');
      const hud = createCombatHud(root);
      const fill = collect(root, 'hud-health-fill')[0] as unknown as FakeNode;
      const text = collect(root, 'hud-health-text')[0] as unknown as FakeNode;
      const width = countStyleWrites(fill, 'width');
      const label = countWrites(text, 'textContent');
      const same = sample();

      hud.set(same);
      expect(width.reads()).toBe(1);
      expect(label.reads()).toBe(1);
      expect(fill.style.width).toBe('100.0%');
      expect(text.textContent).toBe('100');

      hud.set(same);
      expect(width.reads()).toBe(1);
      expect(label.reads()).toBe(1);

      const changed = sample();
      changed.hpRatio = 0.4;
      hud.set(changed);
      expect(width.reads()).toBe(2);
      expect(fill.style.width).toBe('40.0%');
      expect(label.reads()).toBe(2);
      expect(text.textContent).toBe('40');
      hud.dispose();
    } finally {
      uninstall();
    }
  });

  it('护甲/弹药/怒气同值不重写，变化后按显示精度写一次', () => {
    const uninstall = installDomStub();
    try {
      const root = document.createElement('div');
      const hud = createCombatHud(root);
      const armor = collect(root, 'hud-armor-fill')[0] as unknown as FakeNode;
      const magText = collect(root, 'hud-ammo-text')[0] as unknown as FakeNode;
      const rage = collect(root, 'hud-rage-fill')[0] as unknown as FakeNode;
      const armorWidth = countStyleWrites(armor, 'width');
      const armorDisplay = countStyleWrites(armor, 'display');
      const magWrites = countWrites(magText, 'textContent');
      const rageWidth = countStyleWrites(rage, 'width');

      const first = sample();
      first.armorRatio = 0.5;
      first.mag = 7;
      first.rage = 50;
      hud.set(first);
      expect(armorDisplay.reads()).toBe(1);
      expect(armorWidth.reads()).toBe(1);
      expect(magWrites.reads()).toBe(1);
      expect(rageWidth.reads()).toBe(1);

      hud.set(first);
      expect(armorDisplay.reads()).toBe(1);
      expect(armorWidth.reads()).toBe(1);
      expect(magWrites.reads()).toBe(1);
      expect(rageWidth.reads()).toBe(1);

      first.mag = 6;
      hud.set(first);
      expect(magWrites.reads()).toBe(2);
      expect(armorWidth.reads()).toBe(1);
      hud.dispose();
    } finally {
      uninstall();
    }
  });

  it('状态行：血量与状态都没变时不再拼接/写 textContent，状态变化必须写', () => {
    const uninstall = installDomStub();
    try {
      const root = document.createElement('div');
      const banner = document.createElement('div');
      const hud = createHud(root, banner);
      const line = collect(root, 'hud-line')[0] as unknown as FakeNode;
      const writes = countWrites(line, 'textContent');
      const update = {
        local: undefined,
        stats: { snapshotsPerSec: 19.9, inboundBytesPerSec: 100, rttMs: 40 },
        fps: 60,
        liveEntities: 3,
        localPos: undefined,
      };

      hud.update(update as unknown as Parameters<typeof hud.update>[0]);
      expect(writes.reads()).toBe(1);
      expect(line.textContent).toContain('HP 0');

      hud.update(update as unknown as Parameters<typeof hud.update>[0]);
      expect(writes.reads()).toBe(1);

      hud.setStatus('playing');
      expect(writes.reads()).toBe(2);
      expect(line.textContent).toContain('playing');
      hud.dispose();
    } finally {
      uninstall();
    }
  });
});
