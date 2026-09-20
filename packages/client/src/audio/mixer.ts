import { createGain, type VoiceSource } from './synth.ts';

export const MIXER_VOICE_LIMIT = 24;
export const MASTER_BUS_GAIN = 1;
export const SFX_BUS_GAIN = 0.8;
export const UI_BUS_GAIN = 0.5;
export const PANNER_REF_DISTANCE = 3;
export const PANNER_MAX_DISTANCE = 40;
export const PANNER_ROLLOFF_FACTOR = 1.4;
export const PANNER_MODEL: PanningModelType = 'equalpower';
export const PANNER_DISTANCE_MODEL: DistanceModelType = 'inverse';
export const MIN_VOLUME = 0;
export const MAX_VOLUME = 1;
export const DEFAULT_VOICE_DURATION_MS = 250;

export type MixBus = 'sfx' | 'ui';
export type MixVolumeName = 'master' | MixBus;

export interface Vec3Like {
  readonly x: number;
  readonly y: number;
  readonly z: number;
}

export interface ListenerPose extends Vec3Like {
  readonly forwardX: number;
  readonly forwardY: number;
  readonly forwardZ: number;
  readonly upX: number;
  readonly upY: number;
  readonly upZ: number;
}

export const DEFAULT_LISTENER: ListenerPose = Object.freeze({
  x: 0,
  y: 0,
  z: 0,
  forwardX: 0,
  forwardY: 0,
  forwardZ: -1,
  upX: 0,
  upY: 1,
  upZ: 0,
});

export interface MixerVolumes {
  master: number;
  sfx: number;
  ui: number;
}

export interface VoiceSlot {
  readonly id: number;
  readonly bus: MixBus;
  readonly distanceM: number;
  readonly startedAtMs: number;
  readonly endsAtMs: number;
}

export interface Voice {
  readonly slot: VoiceSlot;
  readonly input: GainNode;
  readonly sources: VoiceSource[];
  stop(): void;
}

export interface Mixer {
  readonly context: AudioContext | undefined;
  readonly attached: boolean;
  attachContext(context: AudioContext): void;
  openVoice(bus: MixBus, position: Vec3Like | undefined, durationMs: number): Voice | undefined;
  releaseVoice(voice: Voice): void;
  voices(): readonly VoiceSlot[];
  distanceTo(position: Vec3Like | undefined): number;
  updateListener(pose: ListenerPose): void;
  update(dtMs: number): void;
  setVolume(name: MixVolumeName, value: number): void;
  getVolume(name: MixVolumeName): number;
  getBusGain(name: MixVolumeName): number;
  dispose(): void;
}

export function clampVolume(value: number): number {
  if (!Number.isFinite(value)) return MIN_VOLUME;
  if (value < MIN_VOLUME) return MIN_VOLUME;
  if (value > MAX_VOLUME) return MAX_VOLUME;
  return value;
}

export function busGainFor(name: MixVolumeName, volume: number): number {
  const clamped = clampVolume(volume);
  if (name === 'master') return MASTER_BUS_GAIN * clamped;
  if (name === 'sfx') return SFX_BUS_GAIN * clamped;
  return UI_BUS_GAIN * clamped;
}

function stopSources(sources: VoiceSource[]): void {
  for (let i = 0; i < sources.length; i += 1) {
    const source = sources[i];
    if (source === undefined) continue;
    try {
      source.stop();
    } catch {
      continue;
    }
  }
  sources.length = 0;
}

export function createMixer(context: AudioContext | undefined): Mixer {
  const volumes: MixerVolumes = { master: 1, sfx: 1, ui: 1 };
  const slots: VoiceSlot[] = [];
  const outputs = new Map<number, Voice>();
  let listener: ListenerPose = { ...DEFAULT_LISTENER };
  let clockMs = 0;
  let nextVoiceId = 1;
  let attached = false;
  let ctx: AudioContext | undefined;
  let masterGain: GainNode | undefined;
  let sfxGain: GainNode | undefined;
  let uiGain: GainNode | undefined;

  function busNodeOf(bus: MixBus): GainNode | undefined {
    return bus === 'sfx' ? sfxGain : uiGain;
  }

  function applyVolumes(): void {
    if (masterGain !== undefined) masterGain.gain.value = busGainFor('master', volumes.master);
    if (sfxGain !== undefined) sfxGain.gain.value = busGainFor('sfx', volumes.sfx);
    if (uiGain !== undefined) uiGain.gain.value = busGainFor('ui', volumes.ui);
  }

  function attach(next: AudioContext): void {
    if (attached) return;
    ctx = next;
    attached = true;
    masterGain = createGain(next, busGainFor('master', volumes.master));
    sfxGain = createGain(next, busGainFor('sfx', volumes.sfx));
    uiGain = createGain(next, busGainFor('ui', volumes.ui));
    sfxGain.connect(masterGain);
    uiGain.connect(masterGain);
    masterGain.connect(next.destination);
    updateListener(listener);
  }

  if (context !== undefined) attach(context);

  function distanceOf(position: Vec3Like | undefined): number {
    if (position === undefined) return 0;
    const dx = position.x - listener.x;
    const dy = position.y - listener.y;
    const dz = position.z - listener.z;
    const distance = Math.sqrt(dx * dx + dy * dy + dz * dz);
    return Number.isFinite(distance) ? distance : 0;
  }

  function removeSlot(slot: VoiceSlot): void {
    const index = slots.indexOf(slot);
    if (index >= 0) slots.splice(index, 1);
    const voice = outputs.get(slot.id);
    outputs.delete(slot.id);
    if (voice === undefined) return;
    stopSources(voice.sources);
    try {
      voice.input.disconnect();
    } catch {
      return;
    }
  }

  function farthestIndex(): number {
    let index = 0;
    let distance = -1;
    for (let i = 0; i < slots.length; i += 1) {
      const slot = slots[i];
      if (slot === undefined) continue;
      if (slot.distanceM > distance) {
        distance = slot.distanceM;
        index = i;
      }
    }
    return index;
  }

  function updateListener(pose: ListenerPose): void {
    listener = { ...pose };
    if (ctx === undefined) return;
    const now = ctx.currentTime;
    const audioListener = ctx.listener;
    audioListener.positionX.setValueAtTime(pose.x, now);
    audioListener.positionY.setValueAtTime(pose.y, now);
    audioListener.positionZ.setValueAtTime(pose.z, now);
    audioListener.forwardX.setValueAtTime(pose.forwardX, now);
    audioListener.forwardY.setValueAtTime(pose.forwardY, now);
    audioListener.forwardZ.setValueAtTime(pose.forwardZ, now);
    audioListener.upX.setValueAtTime(pose.upX, now);
    audioListener.upY.setValueAtTime(pose.upY, now);
    audioListener.upZ.setValueAtTime(pose.upZ, now);
  }

  function openVoice(
    bus: MixBus,
    position: Vec3Like | undefined,
    durationMs: number,
  ): Voice | undefined {
    if (ctx === undefined) return undefined;
    const busNode = busNodeOf(bus);
    if (busNode === undefined) return undefined;
    const distanceM = position === undefined ? 0 : distanceOf(position);
    if (slots.length >= MIXER_VOICE_LIMIT) {
      const victim = slots[farthestIndex()];
      if (victim === undefined) return undefined;
      removeSlot(victim);
    }
    const duration =
      Number.isFinite(durationMs) && durationMs > 0 ? durationMs : DEFAULT_VOICE_DURATION_MS;
    const slot: VoiceSlot = {
      id: nextVoiceId,
      bus,
      distanceM,
      startedAtMs: clockMs,
      endsAtMs: clockMs + duration,
    };
    nextVoiceId += 1;
    slots.push(slot);
    const input = createGain(ctx, 1);
    let tail: AudioNode = input;
    if (position !== undefined) {
      const panner = ctx.createPanner();
      panner.panningModel = PANNER_MODEL;
      panner.distanceModel = PANNER_DISTANCE_MODEL;
      panner.refDistance = PANNER_REF_DISTANCE;
      panner.maxDistance = PANNER_MAX_DISTANCE;
      panner.rolloffFactor = PANNER_ROLLOFF_FACTOR;
      const now = ctx.currentTime;
      panner.positionX.setValueAtTime(position.x, now);
      panner.positionY.setValueAtTime(position.y, now);
      panner.positionZ.setValueAtTime(position.z, now);
      input.connect(panner);
      tail = panner;
    }
    tail.connect(busNode);
    const voice: Voice = {
      slot,
      input,
      sources: [],
      stop(): void {
        removeSlot(slot);
      },
    };
    outputs.set(slot.id, voice);
    return voice;
  }

  return {
    get context(): AudioContext | undefined {
      return ctx;
    },
    get attached(): boolean {
      return attached;
    },
    attachContext(next: AudioContext): void {
      attach(next);
    },
    openVoice,
    releaseVoice(voice: Voice): void {
      removeSlot(voice.slot);
    },
    voices(): readonly VoiceSlot[] {
      return slots;
    },
    distanceTo: distanceOf,
    updateListener,
    update(dtMs: number): void {
      if (Number.isFinite(dtMs) && dtMs > 0) clockMs += dtMs;
      for (let i = slots.length - 1; i >= 0; i -= 1) {
        const slot = slots[i];
        if (slot === undefined) continue;
        if (slot.endsAtMs <= clockMs) removeSlot(slot);
      }
    },
    setVolume(name: MixVolumeName, value: number): void {
      volumes[name] = clampVolume(value);
      applyVolumes();
    },
    getVolume(name: MixVolumeName): number {
      return volumes[name];
    },
    getBusGain(name: MixVolumeName): number {
      if (name === 'master' && masterGain !== undefined) return masterGain.gain.value;
      if (name === 'sfx' && sfxGain !== undefined) return sfxGain.gain.value;
      if (name === 'ui' && uiGain !== undefined) return uiGain.gain.value;
      return busGainFor(name, volumes[name]);
    },
    dispose(): void {
      for (let i = slots.length - 1; i >= 0; i -= 1) {
        const slot = slots[i];
        if (slot === undefined) continue;
        removeSlot(slot);
      }
      outputs.clear();
      const closing = ctx;
      ctx = undefined;
      attached = false;
      masterGain = undefined;
      sfxGain = undefined;
      uiGain = undefined;
      if (closing === undefined) return;
      try {
        void closing.close();
      } catch {
        return;
      }
    },
  };
}
