import { afterEach, describe, expect, it } from 'vitest';

import { installFakeAudioContext, type FakeAudioGlobal } from './audioTestContext.ts';
import {
  MIXER_VOICE_LIMIT,
  PANNER_MAX_DISTANCE,
  PANNER_REF_DISTANCE,
  PANNER_ROLLOFF_FACTOR,
  createMixer,
} from './mixer.ts';
import {
  RIFLE_BODY_HZ,
  RIFLE_LOWPASS_HZ,
  RIFLE_NOISE_ATTACK_S,
  RIFLE_NOISE_DECAY_S,
  SFX_NAMES,
  SFX_RECIPES,
  bleatNameForForm,
  playSfx,
  sfxNameForWeaponSlot,
  type NoiseLayer,
  type SfxName,
  type ToneLayer,
} from './sfx.ts';
import { createAudioContext } from './synth.ts';

const POSITION = { x: 6, y: 1, z: -2 };

let fake: FakeAudioGlobal | undefined;

afterEach(() => {
  if (fake === undefined) return;
  fake.uninstall();
  fake = undefined;
});

function noiseLayers(name: SfxName): NoiseLayer[] {
  return SFX_RECIPES[name].layers.filter((layer): layer is NoiseLayer => layer.kind === 'noise');
}

function toneLayers(name: SfxName): ToneLayer[] {
  return SFX_RECIPES[name].layers.filter((layer): layer is ToneLayer => layer.kind === 'tone');
}

function loudestDecayS(name: SfxName): number {
  let decay = 0;
  for (const layer of noiseLayers(name)) decay = Math.max(decay, layer.decayS);
  return decay;
}

function brightestFilterHz(name: SfxName): number {
  let hz = 0;
  for (const layer of noiseLayers(name)) {
    for (const filter of layer.filters) hz = Math.max(hz, filter.frequencyHz);
  }
  return hz;
}

function darkestFilterHz(name: SfxName): number {
  let hz = Number.POSITIVE_INFINITY;
  for (const layer of noiseLayers(name)) {
    for (const filter of layer.filters) {
      if (filter.kind !== 'lowpass') continue;
      hz = Math.min(hz, filter.frequencyHz);
    }
  }
  return hz;
}

function attachMixer(): ReturnType<typeof createMixer> {
  fake = installFakeAudioContext();
  const context = createAudioContext();
  if (context === undefined) throw new Error('fake AudioContext 未生效');
  return createMixer(context);
}

describe('音效配方（P09 §4）', () => {
  it('步枪是 10ms 攻击 + 120ms 衰减的白噪声，经 3kHz 低通并叠加二次谐波', () => {
    const noise = SFX_RECIPES.rifle.layers[0];
    expect(noise?.kind).toBe('noise');
    if (noise === undefined || noise.kind !== 'noise') return;
    expect(noise.attackS).toBe(RIFLE_NOISE_ATTACK_S);
    expect(noise.decayS).toBe(RIFLE_NOISE_DECAY_S);
    expect(RIFLE_NOISE_ATTACK_S).toBe(0.01);
    expect(RIFLE_NOISE_DECAY_S).toBe(0.12);
    expect(noise.filters[0]?.kind).toBe('lowpass');
    expect(noise.filters[0]?.frequencyHz).toBe(RIFLE_LOWPASS_HZ);
    expect(RIFLE_LOWPASS_HZ).toBe(3000);
    const harmonic = SFX_RECIPES.rifle.layers.find((layer) => layer.kind === 'harmonic');
    expect(harmonic?.kind).toBe('harmonic');
    if (harmonic === undefined || harmonic.kind !== 'harmonic') return;
    expect(harmonic.frequencyHz).toBe(RIFLE_BODY_HZ);
    expect(harmonic.frequencyHz).toBe(RIFLE_LOWPASS_HZ / 2);
    expect(harmonic.harmonicLevel).toBeGreaterThan(0);
  });

  it('手枪更短更高、霰弹更低更闷', () => {
    expect(loudestDecayS('pistol')).toBeLessThan(loudestDecayS('rifle'));
    expect(loudestDecayS('shotgun')).toBeGreaterThan(loudestDecayS('rifle'));
    expect(brightestFilterHz('pistol')).toBeGreaterThan(RIFLE_LOWPASS_HZ);
    expect(darkestFilterHz('shotgun')).toBeLessThan(RIFLE_LOWPASS_HZ);
    const pistolHighpass = noiseLayers('pistol')[0]?.filters.find(
      (filter) => filter.kind === 'highpass',
    );
    expect(pistolHighpass?.frequencyHz).toBeGreaterThan(0);
    expect(toneLayers('shotgun').length).toBe(0);
    expect(SFX_RECIPES.shotgun.layers.some((layer) => layer.kind === 'fm')).toBe(true);
  });

  it('四声羊叫都是 220–320Hz 的双振荡器带带通与颤音', () => {
    for (const name of ['bleatGrunt', 'bleatRam', 'bleatElite', 'bleatKing'] as const) {
      const layer = SFX_RECIPES[name].layers[0];
      expect(layer?.kind).toBe('harmonic');
      if (layer === undefined || layer.kind !== 'harmonic') continue;
      expect(layer.frequencyHz).toBeGreaterThanOrEqual(220);
      expect(layer.frequencyHz).toBeLessThanOrEqual(320);
      expect(layer.harmonicLevel).toBeGreaterThan(0);
      expect(layer.bandpass?.kind).toBe('bandpass');
      expect(layer.vibratoHz ?? 0).toBeGreaterThan(0);
      expect(layer.vibratoCents ?? 0).toBeGreaterThan(0);
    }
    const bleats = new Set(
      (['bleatGrunt', 'bleatRam', 'bleatElite', 'bleatKing'] as const).map((name) =>
        SFX_RECIPES[name].layers[0]?.kind === 'harmonic' ? SFX_RECIPES[name].durationMs : 0,
      ),
    );
    expect(bleats.size).toBe(4);
  });

  it('冲锋预警是上滑正弦 + 噪声，狂暴怒吼是低频锯齿 + 失真', () => {
    const sweep = toneLayers('chargeWarn')[0];
    expect(sweep?.waveform).toBe('sine');
    expect(sweep?.frequencyEndHz ?? 0).toBeGreaterThan(sweep?.frequencyHz ?? 0);
    expect(noiseLayers('chargeWarn').length).toBe(1);
    const roar = toneLayers('berserk')[0];
    expect(roar?.waveform).toBe('sawtooth');
    expect(roar?.frequencyHz).toBeLessThan(100);
    expect(roar?.distortion ?? 0).toBeGreaterThan(0.5);
    expect(noiseLayers('berserk').length).toBe(1);
  });

  it('UI 点击与胜负提示走 ui 总线，胜负提示是两段以上音符', () => {
    expect(SFX_RECIPES.uiClick.bus).toBe('ui');
    expect(SFX_RECIPES.uiClick.spatial).toBe(false);
    expect(SFX_RECIPES.victory.bus).toBe('ui');
    expect(SFX_RECIPES.defeat.bus).toBe('ui');
    expect(toneLayers('victory').length).toBeGreaterThanOrEqual(3);
    expect(toneLayers('defeat').length).toBeGreaterThanOrEqual(2);
    const victoryEnd = toneLayers('victory').map((layer) => layer.frequencyHz);
    const defeatEnd = toneLayers('defeat').map((layer) => layer.frequencyHz);
    expect(victoryEnd[0]).toBeGreaterThan(defeatEnd[0] ?? 0);
  });

  it('武器槽与羊形映射到对应配方', () => {
    expect(sfxNameForWeaponSlot(0)).toBe('pistol');
    expect(sfxNameForWeaponSlot(1)).toBe('rifle');
    expect(sfxNameForWeaponSlot(2)).toBe('shotgun');
    expect(bleatNameForForm(0)).toBe('bleatGrunt');
    expect(bleatNameForForm(3)).toBe('bleatKing');
    expect(bleatNameForForm(99)).toBe('bleatGrunt');
  });
});

describe('音效播放（无 AudioContext 与假 AudioContext）', () => {
  it('没有任何 AudioContext 时全部配方都不抛异常也不占用声部', () => {
    const mixer = createMixer(undefined);
    for (const name of SFX_NAMES) {
      expect(playSfx(mixer, name)).toBe(false);
      expect(playSfx(mixer, name, { position: POSITION, gain: 0.5 })).toBe(false);
    }
    expect(mixer.voices().length).toBe(0);
    expect(mixer.attached).toBe(false);
  });

  it('有 AudioContext 时每个配方都调度了发声源', () => {
    const mixer = attachMixer();
    expect(mixer.attached).toBe(true);
    for (const name of SFX_NAMES) expect(playSfx(mixer, name, { position: POSITION })).toBe(true);
    expect(fake?.stats.scheduled ?? 0).toBeGreaterThan(SFX_NAMES.length);
    expect(mixer.voices().length).toBe(SFX_NAMES.length);
    expect(mixer.voices().length).toBeLessThanOrEqual(MIXER_VOICE_LIMIT);
  });

  it('3D 音效走 PannerNode 且使用冻结的距离参数，2D 音效不建 panner', () => {
    const mixer = attachMixer();
    expect(playSfx(mixer, 'rifle')).toBe(true);
    expect(fake?.stats.panners.length).toBe(0);
    expect(playSfx(mixer, 'bleatKing', { position: POSITION })).toBe(true);
    expect(fake?.stats.panners.length).toBe(1);
    const panner = fake?.stats.panners[0];
    expect(panner?.refDistance).toBe(PANNER_REF_DISTANCE);
    expect(panner?.maxDistance).toBe(PANNER_MAX_DISTANCE);
    expect(panner?.rolloffFactor).toBe(PANNER_ROLLOFF_FACTOR);
    expect(panner?.distanceModel).toBe('inverse');
    expect(panner?.x).toBe(POSITION.x);
    expect(panner?.y).toBe(POSITION.y);
    expect(panner?.z).toBe(POSITION.z);
    expect(playSfx(mixer, 'victory')).toBe(true);
    expect(fake?.stats.panners.length).toBe(1);
  });
});
