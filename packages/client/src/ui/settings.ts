import { SETTINGS_LIMITS, type Settings, type SettingsStore } from '../settings/store.ts';

export const SETTINGS_PANEL_CLASS = 'settings-panel';
export const SETTINGS_PANEL_OPEN_CLASS = 'settings-panel-open';
export const SETTINGS_TITLE_CLASS = 'settings-title';
export const SETTINGS_ROW_CLASS = 'settings-row';
export const SETTINGS_LABEL_CLASS = 'settings-label';
export const SETTINGS_RANGE_CLASS = 'settings-range';
export const SETTINGS_VALUE_CLASS = 'settings-value';
export const SETTINGS_TOGGLE_CLASS = 'settings-toggle';
export const SETTINGS_CLOSE_CLASS = 'settings-close';
export const SETTINGS_HINT_CLASS = 'settings-hint';
export const SETTINGS_PANEL_TITLE = '设置';
export const SETTINGS_PANEL_HINT = '按 O 关闭 · 设置保存在本地';

export type SettingsControlName =
  'sensitivity' | 'fov' | 'masterVolume' | 'sfxVolume' | 'colorblindSafe' | 'reduceMotion';

export interface SettingsRangeControl {
  readonly kind: 'range';
  readonly name: SettingsControlName;
  readonly label: string;
  readonly min: number;
  readonly max: number;
  readonly step: number;
  readonly format: (value: number) => string;
}

export interface SettingsToggleControl {
  readonly kind: 'toggle';
  readonly name: SettingsControlName;
  readonly label: string;
}

export type SettingsControl = SettingsRangeControl | SettingsToggleControl;

function percent(value: number): string {
  return String(Math.round(value * 100)) + '%';
}

function degrees(value: number): string {
  return String(Math.round(value)) + '°';
}

function multiplier(value: number): string {
  return value.toFixed(2) + '×';
}

export const SETTINGS_CONTROLS: readonly SettingsControl[] = Object.freeze([
  {
    kind: 'range',
    name: 'sensitivity',
    label: '鼠标灵敏度',
    min: SETTINGS_LIMITS.sensitivityMin,
    max: SETTINGS_LIMITS.sensitivityMax,
    step: 0.05,
    format: multiplier,
  },
  {
    kind: 'range',
    name: 'fov',
    label: '视野 FOV',
    min: SETTINGS_LIMITS.fovMin,
    max: SETTINGS_LIMITS.fovMax,
    step: 1,
    format: degrees,
  },
  {
    kind: 'range',
    name: 'masterVolume',
    label: '主音量',
    min: 0,
    max: 1,
    step: 0.05,
    format: percent,
  },
  {
    kind: 'range',
    name: 'sfxVolume',
    label: '音效音量',
    min: 0,
    max: 1,
    step: 0.05,
    format: percent,
  },
  { kind: 'toggle', name: 'colorblindSafe', label: '色盲安全（蓝橙对比）' },
  { kind: 'toggle', name: 'reduceMotion', label: '减少动态' },
]);

export function controlValueOf(settings: Settings, name: SettingsControlName): number | boolean {
  if (name === 'colorblindSafe') return settings.colorblindSafe;
  if (name === 'reduceMotion') return settings.reduceMotion;
  return settings[name];
}

export function applyControlValue(
  store: SettingsStore,
  name: SettingsControlName,
  value: number | boolean,
): Settings {
  if (name === 'colorblindSafe') return store.set({ colorblindSafe: value === true });
  if (name === 'reduceMotion') return store.set({ reduceMotion: value === true });
  const numeric = typeof value === 'number' ? value : Number(value);
  if (name === 'sensitivity') return store.set({ sensitivity: numeric });
  if (name === 'fov') return store.set({ fov: numeric });
  if (name === 'masterVolume') return store.set({ masterVolume: numeric });
  return store.set({ sfxVolume: numeric });
}

export interface SettingsPanelOptions {
  readonly store: SettingsStore;
  readonly onOpenChange?: ((open: boolean) => void) | undefined;
  readonly onUiSound?: (() => void) | undefined;
  readonly accessibilityRoot?: HTMLElement | undefined;
}

export interface SettingsPanel {
  readonly element: HTMLDivElement;
  readonly isOpen: boolean;
  open(): void;
  close(): void;
  toggle(): boolean;
  refresh(): void;
  dispose(): void;
}

export const COLORBLIND_SAFE_CLASS = 'colorblind-safe';
export const REDUCE_MOTION_CLASS = 'reduce-motion';

export function applyAccessibilityClasses(root: HTMLElement, settings: Settings): void {
  root.classList.toggle(COLORBLIND_SAFE_CLASS, settings.colorblindSafe);
  root.classList.toggle(REDUCE_MOTION_CLASS, settings.reduceMotion);
}

function defaultAccessibilityRoot(): HTMLElement | undefined {
  if (typeof document === 'undefined') return undefined;
  return document.body ?? undefined;
}

export function createSettingsPanel(
  container: HTMLElement,
  options: SettingsPanelOptions,
): SettingsPanel {
  const store = options.store;
  const accessibilityRoot = options.accessibilityRoot ?? defaultAccessibilityRoot();
  const element = document.createElement('div');
  element.className = SETTINGS_PANEL_CLASS;
  element.style.display = 'none';

  const title = document.createElement('div');
  title.className = SETTINGS_TITLE_CLASS;
  title.textContent = SETTINGS_PANEL_TITLE;
  element.append(title);

  const rangeInputs = new Map<SettingsControlName, HTMLInputElement>();
  const rangeValues = new Map<SettingsControlName, HTMLSpanElement>();
  const toggleInputs = new Map<SettingsControlName, HTMLInputElement>();

  function bindRange(control: SettingsRangeControl): void {
    const row = document.createElement('div');
    row.className = SETTINGS_ROW_CLASS;
    const label = document.createElement('label');
    label.className = SETTINGS_LABEL_CLASS;
    label.textContent = control.label;
    const input = document.createElement('input');
    input.className = SETTINGS_RANGE_CLASS;
    input.type = 'range';
    input.min = String(control.min);
    input.max = String(control.max);
    input.step = String(control.step);
    const value = document.createElement('span');
    value.className = SETTINGS_VALUE_CLASS;
    row.append(label, input, value);
    element.append(row);
    rangeInputs.set(control.name, input);
    rangeValues.set(control.name, value);
    input.addEventListener('input', () => {
      applyControlValue(store, control.name, Number(input.value));
    });
  }

  function bindToggle(control: SettingsToggleControl): void {
    const row = document.createElement('div');
    row.className = SETTINGS_ROW_CLASS;
    const label = document.createElement('label');
    label.className = SETTINGS_LABEL_CLASS;
    label.textContent = control.label;
    const input = document.createElement('input');
    input.className = SETTINGS_TOGGLE_CLASS;
    input.type = 'checkbox';
    row.append(label, input);
    element.append(row);
    toggleInputs.set(control.name, input);
    input.addEventListener('input', () => {
      applyControlValue(store, control.name, input.checked);
      if (options.onUiSound !== undefined) options.onUiSound();
    });
  }

  for (let i = 0; i < SETTINGS_CONTROLS.length; i += 1) {
    const control = SETTINGS_CONTROLS[i];
    if (control === undefined) continue;
    if (control.kind === 'range') bindRange(control);
    else bindToggle(control);
  }

  const hint = document.createElement('div');
  hint.className = SETTINGS_HINT_CLASS;
  hint.textContent = SETTINGS_PANEL_HINT;
  const closeButton = document.createElement('button');
  closeButton.className = SETTINGS_CLOSE_CLASS;
  closeButton.type = 'button';
  closeButton.textContent = '关闭';
  element.append(hint, closeButton);
  container.append(element);

  let isOpen = false;

  function refresh(): void {
    const settings = store.get();
    for (let i = 0; i < SETTINGS_CONTROLS.length; i += 1) {
      const control = SETTINGS_CONTROLS[i];
      if (control === undefined) continue;
      const current = controlValueOf(settings, control.name);
      if (control.kind === 'range') {
        const input = rangeInputs.get(control.name);
        const value = rangeValues.get(control.name);
        if (input === undefined || value === undefined || typeof current !== 'number') continue;
        const text = String(current);
        if (input.value !== text) input.value = text;
        value.textContent = control.format(current);
        continue;
      }
      const input = toggleInputs.get(control.name);
      if (input === undefined || typeof current !== 'boolean') continue;
      if (input.checked !== current) input.checked = current;
    }
  }

  function open(): void {
    if (isOpen) return;
    isOpen = true;
    element.style.display = '';
    element.classList.add(SETTINGS_PANEL_OPEN_CLASS);
    refresh();
    if (options.onOpenChange !== undefined) options.onOpenChange(true);
  }

  function close(): void {
    if (!isOpen) return;
    isOpen = false;
    element.style.display = 'none';
    element.classList.remove(SETTINGS_PANEL_OPEN_CLASS);
    if (options.onOpenChange !== undefined) options.onOpenChange(false);
  }

  function sync(): void {
    refresh();
    if (accessibilityRoot === undefined) return;
    applyAccessibilityClasses(accessibilityRoot, store.get());
  }

  const unsubscribe = store.subscribe(sync);
  sync();
  closeButton.addEventListener('click', () => {
    close();
    if (options.onUiSound !== undefined) options.onUiSound();
  });

  return {
    element,
    get isOpen(): boolean {
      return isOpen;
    },
    open,
    close,
    toggle(): boolean {
      if (isOpen) close();
      else open();
      return isOpen;
    },
    refresh,
    dispose(): void {
      unsubscribe();
      element.remove();
    },
  };
}
