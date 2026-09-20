export interface Settings {
  sensitivity: number;
  fov: number;
  masterVolume: number;
  colorblindSafe: boolean;
  reduceMotion: boolean;
}

export const SETTINGS_STORAGE_KEY = 'ac.settings.v1';

export const SETTINGS_LIMITS = Object.freeze({
  sensitivityMin: 0.2,
  sensitivityMax: 3,
  fovMin: 60,
  fovMax: 100,
});

export const DEFAULT_SETTINGS: Settings = Object.freeze({
  sensitivity: 1,
  fov: 75,
  masterVolume: 0.8,
  colorblindSafe: false,
  reduceMotion: false,
});

export interface SettingsStorage {
  getItem(key: string): string | null;
  setItem(key: string, value: string): void;
}

export interface SettingsStore {
  get(): Settings;
  set(patch: Partial<Settings>): Settings;
  reset(): Settings;
  subscribe(listener: (settings: Settings) => void): () => void;
}

function asNumber(value: unknown, min: number, max: number, fallback: number): number {
  if (typeof value !== 'number' || !Number.isFinite(value)) return fallback;
  if (value < min) return min;
  if (value > max) return max;
  return value;
}

function asBoolean(value: unknown, fallback: boolean): boolean {
  return typeof value === 'boolean' ? value : fallback;
}

export function sanitizeSettings(raw: unknown): Settings {
  if (raw === null || typeof raw !== 'object') return { ...DEFAULT_SETTINGS };
  const source = raw as Record<string, unknown>;
  return {
    sensitivity: asNumber(
      source.sensitivity,
      SETTINGS_LIMITS.sensitivityMin,
      SETTINGS_LIMITS.sensitivityMax,
      DEFAULT_SETTINGS.sensitivity,
    ),
    fov: asNumber(source.fov, SETTINGS_LIMITS.fovMin, SETTINGS_LIMITS.fovMax, DEFAULT_SETTINGS.fov),
    masterVolume: asNumber(source.masterVolume, 0, 1, DEFAULT_SETTINGS.masterVolume),
    colorblindSafe: asBoolean(source.colorblindSafe, DEFAULT_SETTINGS.colorblindSafe),
    reduceMotion: asBoolean(source.reduceMotion, DEFAULT_SETTINGS.reduceMotion),
  };
}

export function decodeSettings(text: string | null): Settings {
  if (text === null) return { ...DEFAULT_SETTINGS };
  try {
    return sanitizeSettings(JSON.parse(text));
  } catch {
    return { ...DEFAULT_SETTINGS };
  }
}

export function createSettingsStore(storage: SettingsStorage | undefined): SettingsStore {
  const listeners = new Set<(settings: Settings) => void>();
  let current =
    storage === undefined
      ? { ...DEFAULT_SETTINGS }
      : decodeSettings(storage.getItem(SETTINGS_STORAGE_KEY));

  function persist(): void {
    if (storage === undefined) return;
    try {
      storage.setItem(SETTINGS_STORAGE_KEY, JSON.stringify(current));
    } catch {
      return;
    }
  }

  function emit(): void {
    for (const listener of listeners) listener(current);
  }

  return {
    get(): Settings {
      return current;
    },
    set(patch: Partial<Settings>): Settings {
      current = sanitizeSettings({ ...current, ...patch });
      persist();
      emit();
      return current;
    },
    reset(): Settings {
      current = { ...DEFAULT_SETTINGS };
      persist();
      emit();
      return current;
    },
    subscribe(listener: (settings: Settings) => void): () => void {
      listeners.add(listener);
      return () => {
        listeners.delete(listener);
      };
    },
  };
}
