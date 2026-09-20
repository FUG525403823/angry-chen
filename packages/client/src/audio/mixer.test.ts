import { afterEach, describe, expect, it } from 'vitest';

import { installFakeAudioContext, type FakeAudioGlobal } from './audioTestContext.ts';
import {
  DEFAULT_LISTENER,
  MASTER_BUS_GAIN,
  MIXER_VOICE_LIMIT,
  PANNER_DISTANCE_MODEL,
  PANNER_MAX_DISTANCE,
  PANNER_REF_DISTANCE,
  PANNER_ROLLOFF_FACTOR,
  SFX_BUS_GAIN,
  UI_BUS_GAIN,
  busGainFor,
  createMixer,
  type Mixer,
} from './mixer.ts';
import { playSfx } from './sfx.ts';
import { createAudioContext } from './synth.ts';

let fake: FakeAudioGlobal | undefined;

afterEach(() => {
  if (fake === undefined) return;
  fake.uninstall();
  fake = undefined;
});

function attachMixer(): Mixer {
  fake = installFakeAudioContext();
  const context = createAudioContext();
  if (context === undefined) throw new Error('fake AudioContext 未生效');
  return createMixer(context);
}

function openVoices(mixer: Mixer, count: number): number[] {
  const distances: number[] = [];
  for (let i = 1; i <= count; i += 1) {
    const ok = playSfx(mixer, 'bleatGrunt', { position: { x: i, y: 0, z: 0 } });
    expect(ok).toBe(true);
    distances.push(i);
  }
  return distances;
}

describe('音频总线（P09 §5.4）', () => {
  it('冻结总线上限与距离参数', () => {
    expect(MIXER_VOICE_LIMIT).toBe(24);
    expect(MASTER_BUS_GAIN).toBe(1);
    expect(SFX_BUS_GAIN).toBe(0.8);
    expect(UI_BUS_GAIN).toBe(0.5);
    expect(PANNER_REF_DISTANCE).toBe(3);
    expect(PANNER_MAX_DISTANCE).toBe(40);
    expect(PANNER_ROLLOFF_FACTOR).toBe(1.4);
    expect(PANNER_DISTANCE_MODEL).toBe('inverse');
    expect(busGainFor('sfx', 1)).toBe(0.8);
    expect(busGainFor('ui', 1)).toBe(0.5);
    expect(busGainFor('master', 1)).toBe(1);
  });

  it('音量滑块夹在 0–1 并乘到对应总线增益上', () => {
    const mixer = attachMixer();
    mixer.setVolume('master', 0.5);
    mixer.setVolume('sfx', 0.5);
    mixer.setVolume('ui', 2);
    expect(mixer.getVolume('master')).toBe(0.5);
    expect(mixer.getVolume('ui')).toBe(1);
    expect(mixer.getBusGain('master')).toBe(0.5);
    expect(mixer.getBusGain('sfx')).toBeCloseTo(0.4, 6);
    expect(mixer.getBusGain('ui')).toBe(0.5);
    mixer.setVolume('master', -3);
    expect(mixer.getBusGain('master')).toBe(0);
    mixer.setVolume('master', Number.NaN);
    expect(mixer.getBusGain('master')).toBe(0);
  });

  it('同时发声上限 24，超出时丢弃最远的声部', () => {
    const mixer = attachMixer();
    openVoices(mixer, 25);
    const slots = mixer.voices();
    expect(slots.length).toBe(MIXER_VOICE_LIMIT);
    const distances = slots.map((slot) => slot.distanceM);
    expect(Math.max(...distances)).toBe(25);
    expect(distances.indexOf(24)).toBe(-1);
    expect(fake?.stats.stopped ?? 0).toBeGreaterThan(0);
    openVoices(mixer, 3);
    expect(mixer.voices().length).toBe(MIXER_VOICE_LIMIT);
  });

  it('声部到期后被回收，监听者位姿可无 AudioContext 更新', () => {
    const mixer = attachMixer();
    openVoices(mixer, 3);
    expect(mixer.voices().length).toBe(3);
    mixer.update(16.7);
    expect(mixer.voices().length).toBe(3);
    mixer.update(2000);
    expect(mixer.voices().length).toBe(0);
    mixer.updateListener({ ...DEFAULT_LISTENER, x: 10, z: -4 });
    expect(mixer.distanceTo({ x: 10, y: 0, z: 0 })).toBeCloseTo(4, 6);
    expect(mixer.distanceTo(undefined)).toBe(0);
  });

  it('没有 AudioContext 时所有接口都不抛异常', () => {
    const mixer = createMixer(undefined);
    expect(mixer.attached).toBe(false);
    expect(mixer.context).toBeUndefined();
    mixer.updateListener(DEFAULT_LISTENER);
    mixer.update(16.7);
    mixer.setVolume('sfx', 0.3);
    expect(mixer.getVolume('sfx')).toBeCloseTo(0.3, 6);
    expect(mixer.getBusGain('sfx')).toBeCloseTo(0.24, 6);
    expect(mixer.openVoice('sfx', { x: 1, y: 0, z: 0 }, 120)).toBeUndefined();
    expect(mixer.voices().length).toBe(0);
    mixer.dispose();
  });

  it('dispose 停止所有声部并关闭上下文', () => {
    const mixer = attachMixer();
    openVoices(mixer, 4);
    mixer.dispose();
    expect(mixer.voices().length).toBe(0);
    expect(fake?.stats.closed).toBe(1);
  });
});
