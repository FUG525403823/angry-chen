import { SHEEP_STATE, type SimEvent } from '@ac/shared';
import { afterEach, describe, expect, it } from 'vitest';

import { installFakeAudioContext, type FakeAudioGlobal } from './audioTestContext.ts';
import {
  ACTOR_STALE_MS,
  bleatFormFromState,
  bleatGainForDistance,
  createAudioLayer,
  isChargeWindup,
  outcomeCueForWinnerTeam,
  type ActorEntity,
  type ActorSource,
  type AudioWorldView,
} from './layer.ts';
import { DEFAULT_LISTENER } from './mixer.ts';
import { SUBTITLE_TEXT } from './subtitles.ts';

const LOCAL_PID = 7;
const POSE = { ...DEFAULT_LISTENER, x: 0, y: 2, z: 0 };

let fake: FakeAudioGlobal | undefined;

afterEach(() => {
  if (fake === undefined) return;
  fake.uninstall();
  fake = undefined;
});

function simEvent(
  type: string,
  patch: {
    flags?: number;
    subjectId?: number;
    targetId?: number;
    value?: number;
    x?: number;
    y?: number;
    z?: number;
  } = {},
): SimEvent {
  return {
    type,
    tick: 1,
    flags: patch.flags ?? 0,
    subjectId: patch.subjectId ?? 0,
    targetId: patch.targetId ?? 0,
    x: patch.x ?? 0,
    y: patch.y ?? 0,
    z: patch.z ?? 0,
    value: patch.value ?? 0,
  };
}

function sheep(id: number, state: number, x: number): ActorEntity {
  return { id, kind: 1, pos: { x, y: 0, z: -8 }, state, hpRatio: 1 };
}

function sourceOf(entities: readonly ActorEntity[]): ActorSource {
  return {
    forEachVisible(cb: (entity: ActorEntity) => void): void {
      for (const entity of entities) cb(entity);
    },
  };
}

function viewOf(entities: readonly ActorEntity[], localTeam = 0): AudioWorldView {
  return { source: sourceOf(entities), localPlayerId: LOCAL_PID, localTeam };
}

describe('音频层字幕（P09 §5.4）', () => {
  it('冲锋预警、波次开始、倒地、胜负四类字幕都会出现', () => {
    const texts: string[] = [];
    const layer = createAudioLayer({ onSubtitle: (text: string) => texts.push(text) });
    const chase = [sheep(3, SHEEP_STATE.chase, 6)];
    layer.update(16.7, POSE, viewOf(chase));
    layer.update(16.7, POSE, viewOf([sheep(3, SHEEP_STATE.windup, 6)]));
    expect(texts).toContain(SUBTITLE_TEXT.chargeWarn);
    layer.handleEvents([simEvent('waveStart', { value: 4 })]);
    expect(texts).toContain('第 4 波开始');
    layer.handleEvents([simEvent('playerDowned', { subjectId: LOCAL_PID })]);
    expect(texts).toContain(SUBTITLE_TEXT.downed);
    layer.handleEvents([
      simEvent('matchEnded', { subjectId: 0 }),
      simEvent('matchEnded', { subjectId: 1 }),
    ]);
    expect(texts).toContain(SUBTITLE_TEXT.victory);
    expect(texts).toContain(SUBTITLE_TEXT.defeat);
    expect(layer.subtitles.length).toBeGreaterThan(0);
    const after = layer.subtitles.join('|');
    layer.update(10_000, POSE, viewOf([]));
    expect(layer.subtitles.join('|')).not.toBe(after);
  });

  it('倒地与胜负只对本地玩家播报', () => {
    const texts: string[] = [];
    const layer = createAudioLayer({ onSubtitle: (text: string) => texts.push(text) });
    layer.update(16.7, POSE, viewOf([]));
    layer.handleEvents([simEvent('playerDowned', { subjectId: 99 })]);
    expect(texts.length).toBe(0);
    layer.handleEvents([simEvent('matchEnded', { subjectId: 1 })]);
    expect(texts).toEqual([SUBTITLE_TEXT.defeat]);
  });

  it('字幕文本队列上限与超时', () => {
    expect(outcomeCueForWinnerTeam(0, 0)).toBe('victory');
    expect(outcomeCueForWinnerTeam(1, 0)).toBe('defeat');
    expect(outcomeCueForWinnerTeam(1, 1)).toBe('victory');
  });
});

describe('音频层世界与事件驱动（P09 §4）', () => {
  it('羊形由状态推导，只有 windup 触发冲锋预警', () => {
    expect(isChargeWindup(SHEEP_STATE.windup)).toBe(true);
    expect(isChargeWindup(SHEEP_STATE.charge)).toBe(false);
    expect(bleatFormFromState(SHEEP_STATE.graze)).toBe(0);
    expect(bleatFormFromState(SHEEP_STATE.windup)).toBe(1);
    expect(bleatFormFromState(SHEEP_STATE.ranged)).toBe(2);
    expect(bleatFormFromState(SHEEP_STATE.kingPhase3)).toBe(3);
    expect(bleatFormFromState(SHEEP_STATE.kingSummoning)).toBe(3);
  });

  it('距离越远羊叫增益越低，且不小于下限', () => {
    expect(bleatGainForDistance(1)).toBe(1);
    expect(bleatGainForDistance(Number.NaN)).toBe(1);
    const mid = bleatGainForDistance(15);
    expect(mid).toBeLessThan(1);
    expect(mid).toBeGreaterThan(0.45);
    expect(bleatGainForDistance(200)).toBeCloseTo(0.45, 6);
  });

  it('没有 AudioContext 时世界更新与全部事件类型都不抛异常', () => {
    const layer = createAudioLayer();
    expect(layer.attached).toBe(false);
    const flock = [sheep(1, SHEEP_STATE.chase, 4), sheep(2, SHEEP_STATE.graze, 12)];
    for (let i = 0; i < 400; i += 1) layer.update(16.7, POSE, viewOf(flock));
    expect(layer.attached).toBe(false);
    layer.handleEvents([
      simEvent('playerHit', { subjectId: LOCAL_PID, targetId: 1, flags: 1 }),
      simEvent('playerHit', { subjectId: 1, targetId: LOCAL_PID, value: 12 }),
      simEvent('sheepKilled', { subjectId: LOCAL_PID, targetId: 1 }),
      simEvent('waveStart', { value: 2 }),
      simEvent('waveClear', { value: 2 }),
      simEvent('playerDowned', { subjectId: LOCAL_PID }),
      simEvent('reviveProgress', { targetId: LOCAL_PID, value: 500 }),
      simEvent('reviveDone', { subjectId: 9, targetId: LOCAL_PID }),
      simEvent('rageActivated', { subjectId: LOCAL_PID }),
      simEvent('matchEnded', { subjectId: 0 }),
    ]);
    layer.notifyFire(1);
    layer.notifyReload();
    layer.notifyUi();
    layer.notifyChargeWarning({ x: 2, y: 0, z: -3 });
    layer.dispose();
  });

  it('首次交互后 attach 创建 AudioContext，之后羊叫与开火开始发声', () => {
    const layer = createAudioLayer();
    fake = installFakeAudioContext();
    expect(layer.attach()).toBe(true);
    expect(layer.attached).toBe(true);
    expect(fake.stats.contexts).toBe(1);
    const flock = [sheep(11, SHEEP_STATE.chase, 5)];
    for (let i = 0; i < 600; i += 1) layer.update(16.7, POSE, viewOf(flock));
    expect(fake.stats.scheduled).toBeGreaterThan(0);
    const before = fake.stats.scheduled;
    layer.notifyFire(1);
    layer.notifyReload();
    layer.notifyUi();
    layer.notifyChargeWarning();
    layer.handleEvents([simEvent('rageActivated', { subjectId: LOCAL_PID })]);
    expect(fake.stats.scheduled).toBeGreaterThan(before);
    layer.setVolumes(0.5, 0.25);
    expect(layer.mixer.getBusGain('sfx')).toBeCloseTo(0.2, 6);
    expect(layer.mixer.getBusGain('master')).toBe(0.5);
    layer.dispose();
  });

  it('没有可用 AudioContext 时 attach 返回 false', () => {
    const layer = createAudioLayer();
    expect(layer.attach()).toBe(false);
    expect(layer.attached).toBe(false);
    layer.setVolumes(1, 1);
    expect(layer.mixer.getVolume('sfx')).toBe(1);
    layer.dispose();
  });

  it('长时间不见的羊会被清理，不影响后续更新', () => {
    const layer = createAudioLayer();
    layer.update(16.7, POSE, viewOf([sheep(21, SHEEP_STATE.chase, 3)]));
    layer.update(ACTOR_STALE_MS + 100, POSE, viewOf([]));
    layer.update(16.7, POSE, viewOf([sheep(21, SHEEP_STATE.chase, 3)]));
    layer.dispose();
  });
});
