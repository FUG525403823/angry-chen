import { describe, expect, it } from 'vitest';

import { createSimEvent, type SimEvent } from '../world.ts';
import { LIMITS, OPCODE, type MatchState } from './protocol.ts';
import { decodeEventFrame, decodeMatchState, encodeEventFrame, encodeMatchState } from './codec.ts';

const buffer = new Uint8Array(LIMITS.maxFrameBytes);

function makeEvent(
  type: string,
  subjectId: number,
  targetId: number,
  value: number,
  flags: number,
  tick: number,
): SimEvent {
  const event = createSimEvent();
  event.type = type;
  event.tick = tick;
  event.subjectId = subjectId;
  event.targetId = targetId;
  event.value = value;
  event.flags = flags;
  return event;
}

const samples: SimEvent[] = [
  makeEvent('playerHit', 1, 5, 40, 3, 12),
  makeEvent('sheepKilled', 7, 9, 1, 0, 12),
  makeEvent('waveStart', 0, 0, 3, 120, 12),
  makeEvent('waveClear', 0, 0, 3, 4500, 12),
  makeEvent('playerDowned', 2, 0, 0, 0, 12),
  makeEvent('reviveProgress', 3, 2, 750, 0, 12),
  makeEvent('reviveDone', 3, 2, 0, 0, 12),
  makeEvent('rageActivated', 4, 0, 8000, 0, 12),
  makeEvent('matchEnded', 1, 0, 9, 305000, 12),
];

describe('事件编解码', () => {
  it('九种事件在同一帧内往返一致', () => {
    const size = encodeEventFrame(12, samples, buffer);
    const decoded = decodeEventFrame(buffer.subarray(0, size), []);
    if (!decoded.ok) throw new Error('decode failed');
    expect(decoded.value.length).toBe(samples.length);
    for (let i = 0; i < samples.length; i += 1) {
      const expected = samples[i];
      const actual = decoded.value[i];
      expect(actual?.type).toBe(expected?.type);
      expect(actual?.tick).toBe(12);
      expect(actual?.subjectId).toBe(expected?.subjectId);
      expect(actual?.targetId).toBe(expected?.targetId);
      expect(actual?.flags).toBe(expected?.flags);
      if (expected?.type !== 'matchEnded') expect(actual?.value).toBe(expected?.value);
    }
  });

  it('未登记的事件类型被跳过，不影响同帧其它事件', () => {
    const events = [makeEvent('unknownThing', 1, 2, 3, 4, 5), samples[0], samples[4]];
    const size = encodeEventFrame(5, events as SimEvent[], buffer);
    const decoded = decodeEventFrame(buffer.subarray(0, size), []);
    if (!decoded.ok) throw new Error('decode failed');
    expect(decoded.value.length).toBe(2);
    expect(decoded.value[0]?.type).toBe('playerHit');
    expect(decoded.value[1]?.type).toBe('playerDowned');
  });

  it('未知事件号与截断载荷返回失败', () => {
    const frame = new Uint8Array([OPCODE.event, 0, 0, 0, 0, 1, 200]);
    expect(decodeEventFrame(frame, [])).toEqual({ ok: false, reason: 'unknown-event' });

    const truncated = new Uint8Array([OPCODE.event, 0, 0, 0, 0, 1, 1, 0]);
    expect(decodeEventFrame(truncated, [])).toEqual({ ok: false, reason: 'truncated' });

    const size = encodeEventFrame(1, [samples[0] as SimEvent], buffer);
    const extra = new Uint8Array(size + 1);
    extra.set(buffer.subarray(0, size));
    expect(decodeEventFrame(extra, [])).toEqual({ ok: false, reason: 'bad-length' });
  });

  it('空事件帧合法但进程不为空数组分配事件', () => {
    const size = encodeEventFrame(3, [], buffer);
    expect(size).toBe(6);
    const decoded = decodeEventFrame(buffer.subarray(0, size), []);
    if (!decoded.ok) throw new Error('decode failed');
    expect(decoded.value.length).toBe(0);
  });
});

describe('对局状态编解码', () => {
  const state: MatchState = {
    phase: 1,
    wave: 4,
    intermissionMs: 5000,
    players: [
      {
        pid: 1,
        name: '陈sir',
        ready: true,
        weapon: 1,
        hpRatio: 0.5,
        kills: 12,
        mag: 0,
        reserve: 0,
        reloadLeft10Ms: 0,
        rage: 0,
        rageLeft100Ms: 0,
        downed: false,
        reviveRatio255: 0,
      },
      {
        pid: 2,
        name: 'player2',
        ready: false,
        weapon: 0,
        hpRatio: 1,
        kills: 0,
        mag: 0,
        reserve: 0,
        reloadLeft10Ms: 0,
        rage: 0,
        rageLeft100Ms: 0,
        downed: false,
        reviveRatio255: 0,
      },
      {
        pid: 3,
        name: '羊群克星',
        ready: true,
        weapon: 2,
        hpRatio: 0.25,
        kills: 99,
        mag: 0,
        reserve: 0,
        reloadLeft10Ms: 0,
        rage: 0,
        rageLeft100Ms: 0,
        downed: false,
        reviveRatio255: 0,
      },
      {
        pid: 4,
        name: 'p4',
        ready: true,
        weapon: 0,
        hpRatio: 0,
        kills: 1,
        mag: 0,
        reserve: 0,
        reloadLeft10Ms: 0,
        rage: 0,
        rageLeft100Ms: 0,
        downed: false,
        reviveRatio255: 0,
      },
    ],
  };

  it('四名玩家往返一致', () => {
    const size = encodeMatchState(state, buffer);
    const decoded = decodeMatchState(buffer.subarray(0, size));
    if (!decoded.ok) throw new Error('decode failed');
    expect(decoded.value.phase).toBe(1);
    expect(decoded.value.wave).toBe(4);
    expect(decoded.value.intermissionMs).toBe(5000);
    expect(decoded.value.players.length).toBe(4);
    expect(decoded.value.players[0]?.name).toBe('陈sir');
    expect(decoded.value.players[2]?.name).toBe('羊群克星');
    expect(decoded.value.players[2]?.hpRatio).toBeCloseTo(0.25, 2);
    expect(decoded.value.players[0]?.ready).toBe(true);
    expect(decoded.value.players[1]?.ready).toBe(false);
  });

  it('人数超过上限的字段被拒，尾随字节被拒', () => {
    const frame = new Uint8Array([OPCODE.matchState, 0, 0, 0, 0, 5]);
    expect(decodeMatchState(frame)).toEqual({ ok: false, reason: 'bad-value' });

    const size = encodeMatchState(state, buffer);
    const extra = new Uint8Array(size + 2);
    extra.set(buffer.subarray(0, size));
    expect(decodeMatchState(extra)).toEqual({ ok: false, reason: 'bad-length' });
  });

  it('复用外部 MatchState 对象', () => {
    const size = encodeMatchState(state, buffer);
    const out: MatchState = { phase: 0, wave: 0, intermissionMs: 0, players: [] };
    const decoded = decodeMatchState(buffer.subarray(0, size), out);
    if (!decoded.ok) throw new Error('decode failed');
    expect(decoded.value).toBe(out);
    expect(out.players.length).toBe(4);
  });
});
