import type { MixBus, Mixer, Vec3Like } from './mixer.ts';

import {
  applyAdsr,
  connectChain,
  createDistortion,
  createFmVoice,
  createHarmonicVoice,
  getNoiseBuffer,
  type FilterSpec,
  type VoiceSource,
} from './synth.ts';

export const SFX_START_DELAY_S = 0.005;
export const SFX_TAIL_S = 0.03;
export const NOISE_TAIL_S = 0.02;
export const RIFLE_NOISE_ATTACK_S = 0.01;
export const RIFLE_NOISE_DECAY_S = 0.12;
export const RIFLE_LOWPASS_HZ = 3000;
export const RIFLE_BODY_HZ = 1500;

export const SFX_NAMES = Object.freeze([
  'rifle',
  'pistol',
  'shotgun',
  'reload',
  'hit',
  'headshot',
  'bleatGrunt',
  'bleatRam',
  'bleatElite',
  'bleatKing',
  'chargeWarn',
  'berserk',
  'uiClick',
  'victory',
  'defeat',
] as const);

export type SfxName = (typeof SFX_NAMES)[number];

export const SHEEP_BLEAT_NAMES: readonly SfxName[] = Object.freeze([
  'bleatGrunt',
  'bleatRam',
  'bleatElite',
  'bleatKing',
]);

export interface NoiseLayer {
  readonly kind: 'noise';
  readonly startS?: number | undefined;
  readonly attackS: number;
  readonly decayS: number;
  readonly peak: number;
  readonly filters: readonly FilterSpec[];
}

export interface ToneLayer {
  readonly kind: 'tone';
  readonly startS?: number | undefined;
  readonly waveform: OscillatorType;
  readonly frequencyHz: number;
  readonly frequencyEndHz?: number | undefined;
  readonly attackS: number;
  readonly decayS: number;
  readonly peak: number;
  readonly filters?: readonly FilterSpec[] | undefined;
  readonly distortion?: number | undefined;
  readonly vibratoHz?: number | undefined;
  readonly vibratoCents?: number | undefined;
}

export interface HarmonicLayer {
  readonly kind: 'harmonic';
  readonly startS?: number | undefined;
  readonly frequencyHz: number;
  readonly waveform: OscillatorType;
  readonly harmonicLevel: number;
  readonly bandpass?: FilterSpec | undefined;
  readonly vibratoHz?: number | undefined;
  readonly vibratoCents?: number | undefined;
  readonly attackS: number;
  readonly decayS: number;
  readonly peak: number;
}

export interface FmLayer {
  readonly kind: 'fm';
  readonly startS?: number | undefined;
  readonly carrierHz: number;
  readonly modulatorHz: number;
  readonly index: number;
  readonly attackS: number;
  readonly decayS: number;
  readonly peak: number;
}

export type SfxLayer = NoiseLayer | ToneLayer | HarmonicLayer | FmLayer;

export interface SfxRecipe {
  readonly bus: MixBus;
  readonly spatial: boolean;
  readonly gain: number;
  readonly durationMs: number;
  readonly layers: readonly SfxLayer[];
}

function layerStart(layer: SfxLayer): number {
  return layer.startS ?? 0;
}

export const SFX_RECIPES: Readonly<Record<SfxName, SfxRecipe>> = Object.freeze({
  rifle: {
    bus: 'sfx',
    spatial: true,
    gain: 0.9,
    durationMs: 200,
    layers: [
      {
        kind: 'noise',
        attackS: RIFLE_NOISE_ATTACK_S,
        decayS: RIFLE_NOISE_DECAY_S,
        peak: 0.9,
        filters: [{ kind: 'lowpass', frequencyHz: RIFLE_LOWPASS_HZ, q: 0.7 }],
      },
      {
        kind: 'harmonic',
        frequencyHz: RIFLE_BODY_HZ,
        waveform: 'triangle',
        harmonicLevel: 0.5,
        bandpass: { kind: 'bandpass', frequencyHz: 1500, q: 1.1 },
        attackS: 0.005,
        decayS: 0.09,
        peak: 0.35,
      },
    ],
  },
  pistol: {
    bus: 'sfx',
    spatial: true,
    gain: 0.85,
    durationMs: 130,
    layers: [
      {
        kind: 'noise',
        attackS: 0.004,
        decayS: 0.065,
        peak: 0.8,
        filters: [
          { kind: 'highpass', frequencyHz: 900, q: 0.7 },
          { kind: 'lowpass', frequencyHz: 6500, q: 0.9 },
        ],
      },
      {
        kind: 'harmonic',
        frequencyHz: 2200,
        waveform: 'triangle',
        harmonicLevel: 0.35,
        bandpass: { kind: 'bandpass', frequencyHz: 2400, q: 1.3 },
        attackS: 0.003,
        decayS: 0.05,
        peak: 0.3,
      },
    ],
  },
  shotgun: {
    bus: 'sfx',
    spatial: true,
    gain: 1,
    durationMs: 380,
    layers: [
      {
        kind: 'noise',
        attackS: 0.012,
        decayS: 0.28,
        peak: 1,
        filters: [{ kind: 'lowpass', frequencyHz: 1200, q: 0.6 }],
      },
      {
        kind: 'noise',
        attackS: 0.008,
        decayS: 0.18,
        peak: 0.7,
        filters: [{ kind: 'lowpass', frequencyHz: 400, q: 0.8 }],
      },
      {
        kind: 'fm',
        carrierHz: 160,
        modulatorHz: 55,
        index: 3,
        attackS: 0.01,
        decayS: 0.3,
        peak: 0.4,
      },
    ],
  },
  reload: {
    bus: 'sfx',
    spatial: false,
    gain: 0.6,
    durationMs: 700,
    layers: [
      {
        kind: 'tone',
        waveform: 'square',
        frequencyHz: 1700,
        attackS: 0.002,
        decayS: 0.035,
        peak: 0.25,
      },
      {
        kind: 'tone',
        startS: 0.16,
        waveform: 'square',
        frequencyHz: 1150,
        attackS: 0.002,
        decayS: 0.04,
        peak: 0.22,
      },
      {
        kind: 'tone',
        startS: 0.34,
        waveform: 'square',
        frequencyHz: 900,
        attackS: 0.003,
        decayS: 0.07,
        peak: 0.28,
      },
      {
        kind: 'noise',
        startS: 0.34,
        attackS: 0.002,
        decayS: 0.05,
        peak: 0.3,
        filters: [{ kind: 'bandpass', frequencyHz: 2200, q: 1.5 }],
      },
    ],
  },
  hit: {
    bus: 'sfx',
    spatial: true,
    gain: 0.8,
    durationMs: 150,
    layers: [
      {
        kind: 'noise',
        attackS: 0.004,
        decayS: 0.06,
        peak: 0.8,
        filters: [{ kind: 'bandpass', frequencyHz: 900, q: 0.8 }],
      },
      { kind: 'tone', waveform: 'sine', frequencyHz: 210, attackS: 0.003, decayS: 0.08, peak: 0.5 },
    ],
  },
  headshot: {
    bus: 'sfx',
    spatial: true,
    gain: 0.9,
    durationMs: 170,
    layers: [
      {
        kind: 'noise',
        attackS: 0.003,
        decayS: 0.05,
        peak: 0.7,
        filters: [{ kind: 'highpass', frequencyHz: 2000, q: 0.8 }],
      },
      {
        kind: 'tone',
        waveform: 'sine',
        frequencyHz: 1400,
        frequencyEndHz: 700,
        attackS: 0.003,
        decayS: 0.09,
        peak: 0.6,
        filters: [{ kind: 'bandpass', frequencyHz: 1600, q: 1.2 }],
      },
    ],
  },
  bleatGrunt: {
    bus: 'sfx',
    spatial: true,
    gain: 0.55,
    durationMs: 320,
    layers: [
      {
        kind: 'harmonic',
        frequencyHz: 300,
        waveform: 'sawtooth',
        harmonicLevel: 0.6,
        bandpass: { kind: 'bandpass', frequencyHz: 900, q: 1.1 },
        vibratoHz: 7,
        vibratoCents: 25,
        attackS: 0.02,
        decayS: 0.22,
        peak: 0.5,
      },
    ],
  },
  bleatRam: {
    bus: 'sfx',
    spatial: true,
    gain: 0.6,
    durationMs: 560,
    layers: [
      {
        kind: 'harmonic',
        frequencyHz: 240,
        waveform: 'sawtooth',
        harmonicLevel: 0.55,
        bandpass: { kind: 'bandpass', frequencyHz: 650, q: 1 },
        vibratoHz: 5.5,
        vibratoCents: 30,
        attackS: 0.03,
        decayS: 0.42,
        peak: 0.6,
      },
    ],
  },
  bleatElite: {
    bus: 'sfx',
    spatial: true,
    gain: 0.5,
    durationMs: 460,
    layers: [
      {
        kind: 'harmonic',
        frequencyHz: 280,
        waveform: 'triangle',
        harmonicLevel: 0.7,
        bandpass: { kind: 'bandpass', frequencyHz: 1100, q: 1.4 },
        vibratoHz: 8,
        vibratoCents: 20,
        attackS: 0.02,
        decayS: 0.35,
        peak: 0.45,
      },
    ],
  },
  bleatKing: {
    bus: 'sfx',
    spatial: true,
    gain: 0.7,
    durationMs: 1000,
    layers: [
      {
        kind: 'harmonic',
        frequencyHz: 220,
        waveform: 'sawtooth',
        harmonicLevel: 0.6,
        bandpass: { kind: 'bandpass', frequencyHz: 500, q: 0.9 },
        vibratoHz: 5,
        vibratoCents: 45,
        attackS: 0.04,
        decayS: 0.8,
        peak: 0.75,
      },
    ],
  },
  chargeWarn: {
    bus: 'sfx',
    spatial: true,
    gain: 0.8,
    durationMs: 950,
    layers: [
      {
        kind: 'tone',
        waveform: 'sine',
        frequencyHz: 320,
        frequencyEndHz: 900,
        attackS: 0.03,
        decayS: 0.5,
        peak: 0.6,
      },
      {
        kind: 'noise',
        attackS: 0.03,
        decayS: 0.45,
        peak: 0.4,
        filters: [{ kind: 'bandpass', frequencyHz: 1400, q: 1.2 }],
      },
    ],
  },
  berserk: {
    bus: 'sfx',
    spatial: true,
    gain: 0.9,
    durationMs: 1400,
    layers: [
      {
        kind: 'tone',
        waveform: 'sawtooth',
        frequencyHz: 62,
        attackS: 0.04,
        decayS: 1,
        peak: 0.8,
        distortion: 0.85,
      },
      {
        kind: 'noise',
        attackS: 0.05,
        decayS: 1.1,
        peak: 0.5,
        filters: [{ kind: 'lowpass', frequencyHz: 240, q: 0.7 }],
      },
    ],
  },
  uiClick: {
    bus: 'ui',
    spatial: false,
    gain: 0.5,
    durationMs: 90,
    layers: [
      {
        kind: 'tone',
        waveform: 'square',
        frequencyHz: 880,
        attackS: 0.002,
        decayS: 0.03,
        peak: 0.3,
      },
    ],
  },
  victory: {
    bus: 'ui',
    spatial: false,
    gain: 0.6,
    durationMs: 1200,
    layers: [
      {
        kind: 'tone',
        waveform: 'triangle',
        frequencyHz: 523,
        attackS: 0.01,
        decayS: 0.3,
        peak: 0.5,
      },
      {
        kind: 'tone',
        startS: 0.12,
        waveform: 'triangle',
        frequencyHz: 659,
        attackS: 0.01,
        decayS: 0.3,
        peak: 0.5,
      },
      {
        kind: 'tone',
        startS: 0.24,
        waveform: 'triangle',
        frequencyHz: 784,
        attackS: 0.01,
        decayS: 0.45,
        peak: 0.55,
      },
    ],
  },
  defeat: {
    bus: 'ui',
    spatial: false,
    gain: 0.6,
    durationMs: 1500,
    layers: [
      {
        kind: 'tone',
        waveform: 'sawtooth',
        frequencyHz: 392,
        frequencyEndHz: 330,
        attackS: 0.02,
        decayS: 0.5,
        peak: 0.45,
      },
      {
        kind: 'tone',
        startS: 0.3,
        waveform: 'sawtooth',
        frequencyHz: 311,
        frequencyEndHz: 262,
        attackS: 0.02,
        decayS: 0.7,
        peak: 0.5,
      },
    ],
  },
});

interface VoiceTarget {
  readonly input: GainNode;
  readonly sources: VoiceSource[];
}

export interface SfxPlayOptions {
  readonly position?: Vec3Like | undefined;
  readonly gain?: number | undefined;
}

export function bleatNameForForm(form: number): SfxName {
  const name = SHEEP_BLEAT_NAMES[form];
  return name ?? 'bleatGrunt';
}

export function sfxNameForWeaponSlot(slot: number): SfxName {
  if (slot === 1) return 'rifle';
  if (slot === 2) return 'shotgun';
  return 'pistol';
}

function renderNoiseLayer(
  ctx: BaseAudioContext,
  voice: VoiceTarget,
  layer: NoiseLayer,
  startS: number,
): void {
  const bodyS = layer.attackS + layer.decayS;
  const source = ctx.createBufferSource();
  source.buffer = getNoiseBuffer(ctx);
  source.loop = false;
  const filtered = connectChain(ctx, source, layer.filters);
  const gain = ctx.createGain();
  gain.gain.value = 0;
  filtered.connect(gain);
  gain.connect(voice.input);
  applyAdsr(gain.gain, startS, {
    attackS: layer.attackS,
    decayS: layer.decayS,
    durationS: bodyS + NOISE_TAIL_S,
    peak: layer.peak,
  });
  source.start(startS, 0, bodyS + NOISE_TAIL_S);
  source.stop(startS + bodyS + NOISE_TAIL_S + SFX_TAIL_S);
  voice.sources.push(source);
}

function renderToneLayer(
  ctx: BaseAudioContext,
  voice: VoiceTarget,
  layer: ToneLayer,
  startS: number,
): void {
  const bodyS = layer.attackS + layer.decayS;
  const oscillator = ctx.createOscillator();
  oscillator.type = layer.waveform;
  oscillator.frequency.setValueAtTime(layer.frequencyHz, startS);
  if (layer.frequencyEndHz !== undefined) {
    oscillator.frequency.linearRampToValueAtTime(layer.frequencyEndHz, startS + bodyS);
  }
  let node: AudioNode = oscillator;
  node = connectChain(ctx, node, layer.filters ?? []);
  if (layer.distortion !== undefined && layer.distortion > 0) {
    const shaper = createDistortion(ctx, layer.distortion);
    node.connect(shaper);
    node = shaper;
  }
  const gain = ctx.createGain();
  gain.gain.value = 0;
  node.connect(gain);
  gain.connect(voice.input);
  applyAdsr(gain.gain, startS, {
    attackS: layer.attackS,
    decayS: layer.decayS,
    durationS: bodyS + SFX_TAIL_S,
    peak: layer.peak,
  });
  oscillator.start(startS);
  oscillator.stop(startS + bodyS + SFX_TAIL_S);
  voice.sources.push(oscillator);
  const vibratoHz = layer.vibratoHz ?? 0;
  const vibratoCents = layer.vibratoCents ?? 0;
  if (vibratoHz > 0 && vibratoCents > 0) {
    const lfo = ctx.createOscillator();
    lfo.type = 'sine';
    lfo.frequency.value = vibratoHz;
    const depth = ctx.createGain();
    depth.gain.value = vibratoCents;
    lfo.connect(depth);
    depth.connect(oscillator.detune);
    lfo.start(startS);
    lfo.stop(startS + bodyS + SFX_TAIL_S);
    voice.sources.push(lfo);
  }
}

function renderHarmonicLayer(
  ctx: BaseAudioContext,
  voice: VoiceTarget,
  layer: HarmonicLayer,
  startS: number,
): void {
  const bodyS = layer.attackS + layer.decayS;
  const harmonic = createHarmonicVoice(ctx, {
    frequencyHz: layer.frequencyHz,
    waveform: layer.waveform,
    harmonicLevel: layer.harmonicLevel,
    bandpass: layer.bandpass,
    vibratoHz: layer.vibratoHz ?? 0,
    vibratoCents: layer.vibratoCents ?? 0,
  });
  const gain = ctx.createGain();
  gain.gain.value = 0;
  harmonic.output.connect(gain);
  gain.connect(voice.input);
  applyAdsr(gain.gain, startS, {
    attackS: layer.attackS,
    decayS: layer.decayS,
    durationS: bodyS + SFX_TAIL_S,
    peak: layer.peak,
  });
  harmonic.start(startS, startS + bodyS + SFX_TAIL_S);
  for (let i = 0; i < harmonic.sources.length; i += 1) {
    const source = harmonic.sources[i];
    if (source === undefined) continue;
    voice.sources.push(source);
  }
}

function renderFmLayer(
  ctx: BaseAudioContext,
  voice: VoiceTarget,
  layer: FmLayer,
  startS: number,
): void {
  const bodyS = layer.attackS + layer.decayS;
  const fm = createFmVoice(ctx, {
    carrierHz: layer.carrierHz,
    modulatorHz: layer.modulatorHz,
    index: layer.index,
    waveform: 'sine',
  });
  const gain = ctx.createGain();
  gain.gain.value = 0;
  fm.output.connect(gain);
  gain.connect(voice.input);
  applyAdsr(gain.gain, startS, {
    attackS: layer.attackS,
    decayS: layer.decayS,
    durationS: bodyS + SFX_TAIL_S,
    peak: layer.peak,
  });
  fm.start(startS, startS + bodyS + SFX_TAIL_S);
  for (let i = 0; i < fm.sources.length; i += 1) {
    const source = fm.sources[i];
    if (source === undefined) continue;
    voice.sources.push(source);
  }
}

export function renderSfxLayer(
  ctx: BaseAudioContext,
  target: VoiceTarget,
  layer: SfxLayer,
  baseStartS: number,
): void {
  const startS = baseStartS + layerStart(layer);
  if (layer.kind === 'noise') renderNoiseLayer(ctx, target, layer, startS);
  else if (layer.kind === 'tone') renderToneLayer(ctx, target, layer, startS);
  else if (layer.kind === 'harmonic') renderHarmonicLayer(ctx, target, layer, startS);
  else renderFmLayer(ctx, target, layer, startS);
}

export function playSfx(mixer: Mixer, name: SfxName, options?: SfxPlayOptions): boolean {
  const recipe = SFX_RECIPES[name];
  const ctx = mixer.context;
  if (ctx === undefined) return false;
  const position = recipe.spatial ? options?.position : undefined;
  const voice = mixer.openVoice(recipe.bus, position, recipe.durationMs);
  if (voice === undefined) return false;
  const scale = recipe.gain * (options?.gain ?? 1);
  voice.input.gain.value = scale > 0 ? scale : 0;
  const startS = ctx.currentTime + SFX_START_DELAY_S;
  for (let i = 0; i < recipe.layers.length; i += 1) {
    const layer = recipe.layers[i];
    if (layer === undefined) continue;
    renderSfxLayer(ctx, voice, layer, startS);
  }
  return true;
}
