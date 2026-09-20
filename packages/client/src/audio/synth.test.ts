import { afterEach, describe, expect, it } from 'vitest';

import { installFakeAudioContext, type FakeAudioGlobal } from './audioTestContext.ts';
import {
  DISTORTION_SAMPLES,
  ENVELOPE_FLOOR_GAIN,
  MIN_RAMP_SECONDS,
  NOISE_BUFFER_SECONDS,
  applyAdsr,
  createAudioContext,
  createDistortionCurve,
  createFmVoice,
  createHarmonicVoice,
  createNoiseBuffer,
  getNoiseBuffer,
  type EnvelopeTarget,
} from './synth.ts';

interface EnvelopeCall {
  readonly op: 'set' | 'linear' | 'exponential';
  readonly value: number;
  readonly when: number;
}

function recordEnvelope(): { readonly target: EnvelopeTarget; readonly calls: EnvelopeCall[] } {
  const calls: EnvelopeCall[] = [];
  const target: EnvelopeTarget = {
    setValueAtTime(value: number, when: number): void {
      calls.push({ op: 'set', value, when });
    },
    linearRampToValueAtTime(value: number, when: number): void {
      calls.push({ op: 'linear', value, when });
    },
    exponentialRampToValueAtTime(value: number, when: number): void {
      calls.push({ op: 'exponential', value, when });
    },
  };
  return { target, calls };
}

let fake: FakeAudioGlobal | undefined;

afterEach(() => {
  if (fake === undefined) return;
  fake.uninstall();
  fake = undefined;
});

describe('合成原语（无 AudioContext）', () => {
  it('惰性创建：没有 AudioContext 全局时返回 undefined 而不抛异常', () => {
    expect(typeof AudioContext).toBe('undefined');
    expect(createAudioContext()).toBeUndefined();
  });
});

describe('ADSR 包络数学', () => {
  it('攻击到峰值、按指数衰减到地板、在释放点归零', () => {
    const { target, calls } = recordEnvelope();
    const released = applyAdsr(target, 1, {
      attackS: 0.01,
      decayS: 0.12,
      durationS: 0.13,
      peak: 0.9,
    });
    expect(released).toBeCloseTo(1.13, 6);
    expect(calls).toEqual([
      { op: 'set', value: ENVELOPE_FLOOR_GAIN, when: 1 },
      { op: 'linear', value: 0.9, when: 1.01 },
      { op: 'exponential', value: ENVELOPE_FLOOR_GAIN, when: 1.13 },
      { op: 'set', value: 0, when: 1.13 },
    ]);
  });

  it('峰值与斜坡时长都被夹在安全范围，时长不小于攻击+衰减', () => {
    const { target, calls } = recordEnvelope();
    const released = applyAdsr(target, 0, {
      attackS: 0,
      decayS: Number.NaN,
      durationS: 0,
      peak: 0,
    });
    expect(calls[1]?.value).toBe(ENVELOPE_FLOOR_GAIN);
    expect(calls[1]?.when).toBeCloseTo(MIN_RAMP_SECONDS, 6);
    expect(calls[2]?.when).toBeCloseTo(MIN_RAMP_SECONDS * 2, 6);
    expect(released).toBeCloseTo(MIN_RAMP_SECONDS * 2, 6);
    expect(DISTORTION_SAMPLES).toBe(2048);
  });
});

describe('噪声与失真原语', () => {
  it('噪声 buffer 长度按采样率换算、样本落在 (-1, 1) 且按上下文缓存', () => {
    fake = installFakeAudioContext();
    const context = createAudioContext();
    if (context === undefined) throw new Error('fake AudioContext 未生效');
    const buffer = createNoiseBuffer(context, NOISE_BUFFER_SECONDS);
    expect(buffer.length).toBe(Math.floor(context.sampleRate * NOISE_BUFFER_SECONDS));
    const data = buffer.getChannelData(0);
    let peak = 0;
    for (let i = 0; i < data.length; i += 1) peak = Math.max(peak, Math.abs(data[i] ?? 0));
    expect(peak).toBeGreaterThan(0.5);
    expect(peak).toBeLessThan(1);
    const cached = getNoiseBuffer(context);
    expect(getNoiseBuffer(context)).toBe(cached);
    expect(cached.length).toBe(buffer.length);
  });

  it('失真曲线关于原点奇对称且有界', () => {
    const curve = createDistortionCurve(1);
    expect(curve.length).toBe(DISTORTION_SAMPLES);
    expect(curve[0]).toBeCloseTo(-1, 5);
    expect(curve[DISTORTION_SAMPLES - 1]).toBeCloseTo(1, 5);
    for (let i = 0; i < DISTORTION_SAMPLES; i += 1) {
      const mirrored = curve[DISTORTION_SAMPLES - 1 - i] ?? 0;
      expect(curve[i] ?? 0).toBeCloseTo(-mirrored, 5);
    }
    const soft = createDistortionCurve(0);
    const middle = Math.floor(DISTORTION_SAMPLES / 4);
    expect(Math.abs(curve[middle] ?? 0)).toBeGreaterThan(Math.abs(soft[middle] ?? 0));
  });
});

describe('振荡器声部调度', () => {
  it('谐波声部含基频与二倍频两个振荡器', () => {
    fake = installFakeAudioContext();
    const context = createAudioContext();
    if (context === undefined) throw new Error('fake AudioContext 未生效');
    const voice = createHarmonicVoice(context, {
      frequencyHz: 240,
      waveform: 'sawtooth',
      harmonicLevel: 0.5,
      bandpass: { kind: 'bandpass', frequencyHz: 900, q: 1 },
      vibratoHz: 6,
      vibratoCents: 20,
    });
    expect(voice.sources.length).toBe(3);
    voice.start(0, 0.5);
    expect(fake.stats.scheduled).toBeGreaterThanOrEqual(3);
    expect(fake.stats.oscillators).toBeGreaterThanOrEqual(3);
    expect(fake.stats.filters).toBe(1);
  });

  it('FM 声部用调制器推动载波频率', () => {
    fake = installFakeAudioContext();
    const context = createAudioContext();
    if (context === undefined) throw new Error('fake AudioContext 未生效');
    const before = fake.stats.gains;
    const voice = createFmVoice(context, {
      carrierHz: 160,
      modulatorHz: 55,
      index: 3,
      waveform: 'sine',
    });
    expect(voice.sources.length).toBe(2);
    expect(fake.stats.gains).toBe(before + 2);
    voice.start(0, 0.4);
    expect(fake.stats.scheduled).toBe(2);
  });
});
