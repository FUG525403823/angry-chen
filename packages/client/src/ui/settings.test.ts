import { describe, expect, it } from 'vitest';

import {
  DEFAULT_SETTINGS,
  SETTINGS_STORAGE_KEY,
  createSettingsStore,
  decodeSettings,
  sanitizeSettings,
  type SettingsStorage,
} from '../settings/store.ts';
import {
  COLORBLIND_SAFE_CLASS,
  REDUCE_MOTION_CLASS,
  SETTINGS_CONTROLS,
  SETTINGS_PANEL_CLASS,
  applyAccessibilityClasses,
  applyControlValue,
  controlValueOf,
  createSettingsPanel,
} from './settings.ts';

function createFakeStorage(): SettingsStorage & { readonly data: Map<string, string> } {
  const data = new Map<string, string>();
  return {
    data,
    getItem: (key: string): string | null => data.get(key) ?? null,
    setItem: (key: string, value: string): void => void data.set(key, value),
  };
}

function storedSettings(storage: SettingsStorage): unknown {
  const raw = storage.getItem(SETTINGS_STORAGE_KEY);
  if (raw === null) return null;
  return JSON.parse(raw);
}

interface StubNode {
  className: string;
  type: string;
  textContent: string;
  value: string;
  checked: boolean;
  min: string;
  max: string;
  step: string;
  readonly style: Record<string, string>;
  readonly classList: {
    add(...names: string[]): void;
    remove(...names: string[]): void;
    toggle(name: string, force?: boolean): boolean;
    contains(name: string): boolean;
  };
  addEventListener(type: string, handler: () => void): void;
  fire(type: string): void;
  append(...nodes: StubNode[]): void;
  remove(): void;
}

interface StubDocument {
  readonly nodes: StubNode[];
  uninstall(): void;
}

function createStubNode(): StubNode {
  const classes = new Set<string>();
  const handlers = new Map<string, (() => void)[]>();
  const children: StubNode[] = [];
  const node: StubNode = {
    className: '',
    type: '',
    textContent: '',
    value: '',
    checked: false,
    min: '',
    max: '',
    step: '',
    style: {},
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
    addEventListener(type: string, handler: () => void): void {
      const list = handlers.get(type) ?? [];
      list.push(handler);
      handlers.set(type, list);
    },
    fire(type: string): void {
      for (const handler of handlers.get(type) ?? []) handler();
    },
    append(...added: StubNode[]): void {
      for (const child of added) children.push(child);
    },
    remove(): void {
      children.length = 0;
    },
  };
  return node;
}

function installStubDocument(): StubDocument {
  const nodes: StubNode[] = [];
  const previous = Object.getOwnPropertyDescriptor(globalThis, 'document');
  Object.defineProperty(globalThis, 'document', {
    configurable: true,
    writable: true,
    value: {
      createElement(): unknown {
        const node = createStubNode();
        nodes.push(node);
        return node;
      },
      body: createStubNode(),
    },
  });
  return {
    nodes,
    uninstall(): void {
      if (previous === undefined) {
        Reflect.deleteProperty(globalThis, 'document');
        return;
      }
      Object.defineProperty(globalThis, 'document', previous);
    },
  };
}

describe('设置面板写入（P09 §4）', () => {
  it('色盲安全与减少动态经面板写入并持久化到 ac.settings.v1', () => {
    const storage = createFakeStorage();
    const store = createSettingsStore(storage);
    applyControlValue(store, 'colorblindSafe', true);
    applyControlValue(store, 'reduceMotion', true);
    expect(storedSettings(storage)).toMatchObject({ colorblindSafe: true, reduceMotion: true });
    expect(store.get().colorblindSafe).toBe(true);
    expect(store.get().reduceMotion).toBe(true);
    expect(createSettingsStore(storage).get().colorblindSafe).toBe(true);
    applyControlValue(store, 'colorblindSafe', false);
    expect(storedSettings(storage)).toMatchObject({ colorblindSafe: false, reduceMotion: true });
  });

  it('灵敏度、FOV 与音量经面板写入并夹在合法区间', () => {
    const storage = createFakeStorage();
    const store = createSettingsStore(storage);
    expect(applyControlValue(store, 'sensitivity', 99).sensitivity).toBe(3);
    expect(applyControlValue(store, 'fov', 10).fov).toBe(60);
    expect(applyControlValue(store, 'masterVolume', -1).masterVolume).toBe(0);
    expect(applyControlValue(store, 'sfxVolume', 5).sfxVolume).toBe(1);
    expect(applyControlValue(store, 'sfxVolume', 0.25).sfxVolume).toBe(0.25);
    expect(storedSettings(storage)).toMatchObject({ sfxVolume: 0.25, masterVolume: 0 });
  });

  it('控制项覆盖灵敏度、FOV、主/音效音量与两项可访问性开关', () => {
    expect(SETTINGS_CONTROLS.map((control) => control.name)).toEqual([
      'sensitivity',
      'fov',
      'masterVolume',
      'sfxVolume',
      'colorblindSafe',
      'reduceMotion',
    ]);
    const settings = { ...DEFAULT_SETTINGS, fov: 90, reduceMotion: true };
    expect(controlValueOf(settings, 'fov')).toBe(90);
    expect(controlValueOf(settings, 'reduceMotion')).toBe(true);
    for (const control of SETTINGS_CONTROLS) {
      if (control.kind !== 'range') continue;
      expect(control.min).toBeLessThan(control.max);
      expect(control.step).toBeGreaterThan(0);
      expect(control.format(control.min).length).toBeGreaterThan(0);
    }
  });

  it('旧版本设置缺少 sfxVolume 时回落默认值并保持其它字段', () => {
    const legacy = JSON.stringify({
      sensitivity: 1.5,
      fov: 80,
      masterVolume: 0.5,
      colorblindSafe: true,
      reduceMotion: true,
    });
    const decoded = decodeSettings(legacy);
    expect(decoded).toEqual({
      sensitivity: 1.5,
      fov: 80,
      masterVolume: 0.5,
      sfxVolume: DEFAULT_SETTINGS.sfxVolume,
      colorblindSafe: true,
      reduceMotion: true,
    });
    expect(DEFAULT_SETTINGS.sfxVolume).toBe(0.8);
    expect(sanitizeSettings({ sfxVolume: 'x' }).sfxVolume).toBe(DEFAULT_SETTINGS.sfxVolume);
    expect(Object.keys(sanitizeSettings({})).sort()).toEqual([
      'colorblindSafe',
      'fov',
      'masterVolume',
      'reduceMotion',
      'sensitivity',
      'sfxVolume',
    ]);
  });

  it('面板 DOM 勾选后写回 ac.settings.v1，滑块越界经 store 夹紧', () => {
    const dom = installStubDocument();
    try {
      const storage = createFakeStorage();
      const store = createSettingsStore(storage);
      const host = document.createElement('div');
      const panel = createSettingsPanel(host, { store });
      expect(dom.nodes.some((node) => node.className === SETTINGS_PANEL_CLASS)).toBe(true);
      const toggles = dom.nodes.filter((node) => node.type === 'checkbox');
      expect(toggles.length).toBe(2);
      const colorblind = toggles[0];
      const reduce = toggles[1];
      if (colorblind === undefined || reduce === undefined) throw new Error('缺少可访问性开关');
      colorblind.checked = true;
      colorblind.fire('input');
      reduce.checked = true;
      reduce.fire('input');
      expect(storedSettings(storage)).toMatchObject({ colorblindSafe: true, reduceMotion: true });
      expect(createSettingsStore(storage).get().reduceMotion).toBe(true);
      const ranges = dom.nodes.filter((node) => node.type === 'range');
      expect(ranges.length).toBe(4);
      const fov = ranges[1];
      if (fov === undefined) throw new Error('缺少 FOV 滑块');
      fov.value = '140';
      fov.fire('input');
      expect(store.get().fov).toBe(100);
      expect(fov.value).toBe('100');
      expect(panel.isOpen).toBe(false);
      panel.open();
      expect(panel.isOpen).toBe(true);
      expect(panel.toggle()).toBe(false);
      panel.dispose();
      expect(store.get().fov).toBe(100);
    } finally {
      dom.uninstall();
    }
  });

  it('可访问性类在指定根节点上切换（HUD 根）', () => {
    const dom = installStubDocument();
    try {
      const root = document.createElement('div');
      applyAccessibilityClasses(root, { ...DEFAULT_SETTINGS, colorblindSafe: true });
      expect(root.classList.contains(COLORBLIND_SAFE_CLASS)).toBe(true);
      expect(root.classList.contains(REDUCE_MOTION_CLASS)).toBe(false);
      applyAccessibilityClasses(root, {
        ...DEFAULT_SETTINGS,
        colorblindSafe: false,
        reduceMotion: true,
      });
      expect(root.classList.contains(COLORBLIND_SAFE_CLASS)).toBe(false);
      expect(root.classList.contains(REDUCE_MOTION_CLASS)).toBe(true);
    } finally {
      dom.uninstall();
    }
  });

  it('未指定根节点时默认切换 document.body 上的可访问性类', () => {
    const dom = installStubDocument();
    try {
      const store = createSettingsStore(createFakeStorage());
      const panel = createSettingsPanel(document.createElement('div'), { store });
      expect(document.body.classList.contains(COLORBLIND_SAFE_CLASS)).toBe(false);
      store.set({ colorblindSafe: true, reduceMotion: true });
      expect(document.body.classList.contains(COLORBLIND_SAFE_CLASS)).toBe(true);
      expect(document.body.classList.contains(REDUCE_MOTION_CLASS)).toBe(true);
      store.reset();
      expect(document.body.classList.contains(COLORBLIND_SAFE_CLASS)).toBe(false);
      expect(document.body.classList.contains(REDUCE_MOTION_CLASS)).toBe(false);
      panel.dispose();
    } finally {
      dom.uninstall();
    }
  });

  it('面板勾选同步应用到指定的可访问性根节点', () => {
    const dom = installStubDocument();
    try {
      const storage = createFakeStorage();
      const store = createSettingsStore(storage);
      const root = document.createElement('div');
      const panel = createSettingsPanel(document.createElement('div'), {
        store,
        accessibilityRoot: root,
      });
      const toggles = dom.nodes.filter((node) => node.type === 'checkbox');
      const colorblind = toggles[0];
      if (colorblind === undefined) throw new Error('缺少色盲安全开关');
      colorblind.checked = true;
      colorblind.fire('input');
      expect(root.classList.contains(COLORBLIND_SAFE_CLASS)).toBe(true);
      expect(storedSettings(storage)).toMatchObject({ colorblindSafe: true });
      store.set({ reduceMotion: true });
      expect(root.classList.contains(REDUCE_MOTION_CLASS)).toBe(true);
      panel.dispose();
    } finally {
      dom.uninstall();
    }
  });
});
