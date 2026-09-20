import { describe, expect, it } from 'vitest';

import {
  DEFAULT_SETTINGS,
  SETTINGS_STORAGE_KEY,
  createSettingsStore,
  decodeSettings,
  sanitizeSettings,
  type SettingsStorage,
} from './store.ts';

function createFakeStorage(): SettingsStorage & { readonly data: Map<string, string> } {
  const data = new Map<string, string>();
  return {
    data,
    getItem: (key: string): string | null => data.get(key) ?? null,
    setItem: (key: string, value: string): void => void data.set(key, value),
  };
}

describe('设置存储', () => {
  it('非法 JSON 与缺失键都回落默认值', () => {
    expect(decodeSettings(null)).toEqual(DEFAULT_SETTINGS);
    expect(decodeSettings('{ not json')).toEqual(DEFAULT_SETTINGS);
    expect(decodeSettings('null')).toEqual(DEFAULT_SETTINGS);
    expect(decodeSettings('[1,2,3]')).toEqual(DEFAULT_SETTINGS);
  });

  it('越界数值被夹到区间内，未知字段被丢弃', () => {
    const settings = sanitizeSettings({
      sensitivity: 99,
      fov: 10,
      masterVolume: -5,
      colorblindSafe: 'yes',
      reduceMotion: true,
      extra: 1,
    });
    expect(settings.sensitivity).toBe(3);
    expect(settings.fov).toBe(60);
    expect(settings.masterVolume).toBe(0);
    expect(settings.colorblindSafe).toBe(false);
    expect(settings.reduceMotion).toBe(true);
    expect(Object.keys(settings).sort()).toEqual([
      'colorblindSafe',
      'fov',
      'masterVolume',
      'reduceMotion',
      'sensitivity',
    ]);
  });

  it('NaN 与 Infinity 回落默认值', () => {
    expect(sanitizeSettings({ sensitivity: Number.NaN }).sensitivity).toBe(
      DEFAULT_SETTINGS.sensitivity,
    );
    expect(sanitizeSettings({ fov: Number.POSITIVE_INFINITY }).fov).toBe(DEFAULT_SETTINGS.fov);
  });

  it('写入后重新读取保持数值，刷新等价于新建 store', () => {
    const storage = createFakeStorage();
    const store = createSettingsStore(storage);
    store.set({ sensitivity: 2.4, fov: 96 });
    expect(storage.data.has(SETTINGS_STORAGE_KEY)).toBe(true);

    const reloaded = createSettingsStore(storage);
    expect(reloaded.get().sensitivity).toBe(2.4);
    expect(reloaded.get().fov).toBe(96);
    expect(reloaded.get().masterVolume).toBe(DEFAULT_SETTINGS.masterVolume);
  });

  it('订阅者在 set/reset 时收到通知，退订后不再收到', () => {
    const store = createSettingsStore(undefined);
    const seen: number[] = [];
    const unsubscribe = store.subscribe((settings) => seen.push(settings.sensitivity));
    store.set({ sensitivity: 1.5 });
    store.reset();
    unsubscribe();
    store.set({ sensitivity: 0.5 });
    expect(seen).toEqual([1.5, DEFAULT_SETTINGS.sensitivity]);
  });

  it('storage 抛异常时不中断调用方', () => {
    const broken: SettingsStorage = {
      getItem: () => null,
      setItem: () => {
        throw new Error('quota');
      },
    };
    const store = createSettingsStore(broken);
    expect(() => store.set({ fov: 80 })).not.toThrow();
    expect(store.get().fov).toBe(80);
  });
});
