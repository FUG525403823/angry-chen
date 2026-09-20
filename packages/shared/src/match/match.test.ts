import { describe, expect, it } from 'vitest';

import {
  MATCH_PHASE,
  MATCH_PHASE_NAMES,
  accuracyOf,
  accumulateAlive,
  accumulateDowned,
  canTransition,
  createPlayerMatchStats,
  noteDown,
  noteHit,
  noteKill,
  noteRevive,
  noteShot,
  survivalMsOf,
  type MatchPhase,
} from '../index.ts';

const ALL_PHASES: readonly MatchPhase[] = Object.freeze([
  MATCH_PHASE.lobby,
  MATCH_PHASE.loading,
  MATCH_PHASE.playing,
  MATCH_PHASE.intermission,
  MATCH_PHASE.ended,
]);

const LEGAL: ReadonlySet<string> = new Set([
  'lobby->loading',
  'loading->playing',
  'playing->intermission',
  'playing->ended',
  'intermission->playing',
  'intermission->ended',
  'ended->lobby',
]);

function key(from: MatchPhase, to: MatchPhase): string {
  return (MATCH_PHASE_NAMES[from] ?? String(from)) + '->' + (MATCH_PHASE_NAMES[to] ?? String(to));
}

describe('对局阶段迁移表', () => {
  it('5x5 全组合：仅文档列出的 7 条迁移合法', () => {
    let legal = 0;
    let illegal = 0;
    for (const from of ALL_PHASES) {
      for (const to of ALL_PHASES) {
        const result = canTransition(from, to);
        if (LEGAL.has(key(from, to))) {
          expect(result).toEqual({ ok: true });
          legal += 1;
        } else {
          expect(result).toEqual({ ok: false, reason: 'illegal-transition' });
          illegal += 1;
        }
      }
    }
    expect(legal).toBe(7);
    expect(illegal).toBe(18);
    expect(legal + illegal).toBe(25);
  });

  it('阶段编号与 P08 §5.1 一致', () => {
    expect(MATCH_PHASE.lobby).toBe(0);
    expect(MATCH_PHASE.loading).toBe(1);
    expect(MATCH_PHASE.playing).toBe(2);
    expect(MATCH_PHASE.intermission).toBe(3);
    expect(MATCH_PHASE.ended).toBe(4);
  });
});

describe('玩家统计累加器', () => {
  it('命中率与存活时长派生正确', () => {
    const stats = createPlayerMatchStats();
    noteShot(stats);
    noteShot(stats);
    noteShot(stats);
    noteShot(stats);
    noteHit(stats);
    noteHit(stats);
    expect(accuracyOf(stats)).toBeCloseTo(0.5, 6);

    accumulateAlive(stats, 10_000);
    accumulateDowned(stats, 4_000);
    expect(stats.aliveMs).toBe(10_000);
    expect(survivalMsOf(stats, 14_000)).toBe(10_000);
    expect(survivalMsOf(stats, 3_000)).toBe(0);
  });

  it('击杀/爆头/倒地/救援逐事件累加', () => {
    const stats = createPlayerMatchStats();
    noteKill(stats, false);
    noteKill(stats, true);
    noteDown(stats);
    noteDown(stats);
    noteRevive(stats);
    expect(stats.kills).toBe(2);
    expect(stats.headshots).toBe(1);
    expect(stats.downs).toBe(2);
    expect(stats.revives).toBe(1);
    expect(accuracyOf(createPlayerMatchStats())).toBe(0);
  });
});
