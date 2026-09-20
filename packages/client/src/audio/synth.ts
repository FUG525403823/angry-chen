export const NOISE_BUFFER_SECONDS = 1.5;
export const NOISE_BUFFER_SEED = 0x9e3779b1;
export const ENVELOPE_FLOOR_GAIN = 0.0001;
export const MIN_RAMP_SECONDS = 0.001;
export const DEFAULT_FILTER_Q = 0.9;
export const DISTORTION_SAMPLES = 2048;
export const DISTORTION_MAX_DRIVE = 24;

export type VoiceSource = AudioScheduledSourceNode;

export interface AdsrSpec {
  readonly attackS: number;
  readonly decayS: number;
  readonly durationS: number;
  readonly peak: number;
}

export interface EnvelopeTarget {
  setValueAtTime(value: number, when: number): unknown;
  linearRampToValueAtTime(value: number, when: number): unknown;
  exponentialRampToValueAtTime(value: number, when: number): unknown;
}

export interface FilterSpec {
  readonly kind: BiquadFilterType;
  readonly frequencyHz: number;
  readonly q: number;
}

export interface Voice {
  readonly output: GainNode;
  readonly sources: readonly VoiceSource[];
  start(startS: number, stopS: number): void;
}

export interface HarmonicVoiceSpec {
  readonly frequencyHz: number;
  readonly waveform: OscillatorType;
  readonly harmonicLevel: number;
  readonly bandpass: FilterSpec | undefined;
  readonly vibratoHz: number;
  readonly vibratoCents: number;
}

export interface FmVoiceSpec {
  readonly carrierHz: number;
  readonly modulatorHz: number;
  readonly index: number;
  readonly waveform: OscillatorType;
}

const noiseBuffers = new WeakMap<BaseAudioContext, AudioBuffer>();

function safeSeconds(value: number, fallback: number): number {
  if (!Number.isFinite(value) || value <= 0) return fallback;
  return value;
}

export function createAudioContext(): AudioContext | undefined {
  if (typeof AudioContext === 'undefined') return undefined;
  try {
    const context = new AudioContext();
    if (context.state === 'suspended') void context.resume();
    return context;
  } catch {
    return undefined;
  }
}

export function applyAdsr(param: EnvelopeTarget, startS: number, spec: AdsrSpec): number {
  const attackS = Math.max(MIN_RAMP_SECONDS, safeSeconds(spec.attackS, MIN_RAMP_SECONDS));
  const decayS = Math.max(MIN_RAMP_SECONDS, safeSeconds(spec.decayS, MIN_RAMP_SECONDS));
  const peak = spec.peak > ENVELOPE_FLOOR_GAIN ? spec.peak : ENVELOPE_FLOOR_GAIN;
  const releasesAtS =
    startS + Math.max(attackS + decayS, safeSeconds(spec.durationS, attackS + decayS));
  param.setValueAtTime(ENVELOPE_FLOOR_GAIN, startS);
  param.linearRampToValueAtTime(peak, startS + attackS);
  param.exponentialRampToValueAtTime(ENVELOPE_FLOOR_GAIN, startS + attackS + decayS);
  param.setValueAtTime(0, releasesAtS);
  return releasesAtS;
}

export function createNoiseBuffer(ctx: BaseAudioContext, durationS: number): AudioBuffer {
  const seconds = safeSeconds(durationS, NOISE_BUFFER_SECONDS);
  const length = Math.max(1, Math.floor(ctx.sampleRate * seconds));
  const buffer = ctx.createBuffer(1, length, ctx.sampleRate);
  const data = buffer.getChannelData(0);
  let state = NOISE_BUFFER_SEED;
  for (let i = 0; i < length; i += 1) {
    state = (Math.imul(state, 1664525) + 1013904223) >>> 0;
    data[i] = (state / 2147483648 - 1) * 0.9;
  }
  return buffer;
}

export function getNoiseBuffer(ctx: BaseAudioContext): AudioBuffer {
  const cached = noiseBuffers.get(ctx);
  if (cached !== undefined) return cached;
  const buffer = createNoiseBuffer(ctx, NOISE_BUFFER_SECONDS);
  noiseBuffers.set(ctx, buffer);
  return buffer;
}

export function createGain(ctx: BaseAudioContext, value: number): GainNode {
  const gain = ctx.createGain();
  gain.gain.value = value;
  return gain;
}

export function createFilter(ctx: BaseAudioContext, spec: FilterSpec): BiquadFilterNode {
  const filter = ctx.createBiquadFilter();
  filter.type = spec.kind;
  filter.frequency.value = spec.frequencyHz;
  filter.Q.value = spec.q;
  return filter;
}

export function createNoiseSource(ctx: BaseAudioContext): AudioBufferSourceNode {
  const source = ctx.createBufferSource();
  source.buffer = getNoiseBuffer(ctx);
  source.loop = false;
  return source;
}

export function createDistortionCurve(amount: number): Float32Array<ArrayBuffer> {
  const drive = 1 + Math.min(1, Math.max(0, amount)) * DISTORTION_MAX_DRIVE;
  const normaliser = Math.tanh(drive);
  const curve = new Float32Array(DISTORTION_SAMPLES);
  for (let i = 0; i < DISTORTION_SAMPLES; i += 1) {
    const x = (i / (DISTORTION_SAMPLES - 1)) * 2 - 1;
    curve[i] = Math.tanh(drive * x) / normaliser;
  }
  return curve;
}

export function createDistortion(ctx: BaseAudioContext, amount: number): WaveShaperNode {
  const shaper = ctx.createWaveShaper();
  shaper.curve = createDistortionCurve(amount);
  shaper.oversample = '2x';
  return shaper;
}

function createVibrato(
  ctx: BaseAudioContext,
  rateHz: number,
  depthCents: number,
  targets: readonly AudioParam[],
): VoiceSource | undefined {
  if (!(rateHz > 0) || !(depthCents > 0)) return undefined;
  const lfo = ctx.createOscillator();
  lfo.type = 'sine';
  lfo.frequency.value = rateHz;
  const depth = createGain(ctx, depthCents);
  lfo.connect(depth);
  for (let i = 0; i < targets.length; i += 1) {
    const target = targets[i];
    if (target === undefined) continue;
    depth.connect(target);
  }
  return lfo;
}

export function connectChain(
  ctx: BaseAudioContext,
  from: AudioNode,
  specs: readonly FilterSpec[],
): AudioNode {
  let node = from;
  for (let i = 0; i < specs.length; i += 1) {
    const spec = specs[i];
    if (spec === undefined) continue;
    const filter = createFilter(ctx, spec);
    node.connect(filter);
    node = filter;
  }
  return node;
}

export function createHarmonicVoice(ctx: BaseAudioContext, spec: HarmonicVoiceSpec): Voice {
  const fundamental = ctx.createOscillator();
  fundamental.type = spec.waveform;
  fundamental.frequency.value = spec.frequencyHz;
  const harmonic = ctx.createOscillator();
  harmonic.type = 'sine';
  harmonic.frequency.value = spec.frequencyHz * 2;
  const mix = createGain(ctx, 1);
  fundamental.connect(mix);
  const harmonicGain = createGain(ctx, spec.harmonicLevel);
  harmonic.connect(harmonicGain);
  harmonicGain.connect(mix);
  const filtered = connectChain(ctx, mix, spec.bandpass === undefined ? [] : [spec.bandpass]);
  const output = createGain(ctx, 1);
  filtered.connect(output);
  const lfo = createVibrato(ctx, spec.vibratoHz, spec.vibratoCents, [
    fundamental.detune,
    harmonic.detune,
  ]);
  const sources: VoiceSource[] =
    lfo === undefined ? [fundamental, harmonic] : [fundamental, harmonic, lfo];
  return {
    output,
    sources,
    start(startS: number, stopS: number): void {
      for (let i = 0; i < sources.length; i += 1) sources[i]?.start(startS);
      for (let i = 0; i < sources.length; i += 1) sources[i]?.stop(stopS);
    },
  };
}

export function createFmVoice(ctx: BaseAudioContext, spec: FmVoiceSpec): Voice {
  const carrier = ctx.createOscillator();
  carrier.type = spec.waveform;
  carrier.frequency.value = spec.carrierHz;
  const modulator = ctx.createOscillator();
  modulator.type = 'sine';
  modulator.frequency.value = spec.modulatorHz;
  const modGain = createGain(ctx, spec.carrierHz * spec.index);
  modulator.connect(modGain);
  modGain.connect(carrier.frequency);
  const output = createGain(ctx, 1);
  carrier.connect(output);
  const sources: VoiceSource[] = [carrier, modulator];
  return {
    output,
    sources,
    start(startS: number, stopS: number): void {
      for (let i = 0; i < sources.length; i += 1) sources[i]?.start(startS);
      for (let i = 0; i < sources.length; i += 1) sources[i]?.stop(stopS);
    },
  };
}
