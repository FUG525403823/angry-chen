import { describe, expect, it } from 'vitest';

import {
  accuracyPercentOf,
  fetchLeaderboard,
  formatDurationMs,
  leaderboardRowText,
  parseLeaderboardBody,
  parseMatchRecord,
  pickMatchRecord,
  winnerBannerOf,
  type JsonFetcher,
  type MatchRecord,
} from './results.ts';

function recordInput(overrides: Record<string, unknown> = {}): Record<string, unknown> {
  return {
    matchId: 'AB2D-1000',
    startedAtMs: 1000,
    durationMs: 125000,
    waveReached: 10,
    winnerTeam: 0,
    playerCount: 2,
    players: [
      {
        name: '陈sir',
        kills: 42,
        headshots: 9,
        shotsFired: 200,
        hits: 120,
        revives: 3,
        downs: 1,
        aliveMs: 120000,
        leftMidMatch: false,
      },
      {
        name: '阿陈',
        kills: 8,
        headshots: 1,
        shotsFired: 40,
        hits: 10,
        revives: 0,
        downs: 2,
        aliveMs: 90000,
        leftMidMatch: true,
      },
    ],
    ...overrides,
  };
}

describe('结算记录解析', () => {
  it('解析合法的比赛记录', () => {
    const record = parseMatchRecord(recordInput());
    expect(record).not.toBeNull();
    expect(record?.players).toHaveLength(2);
    expect(record?.players[0]?.name).toBe('陈sir');
    expect(record?.players[1]?.leftMidMatch).toBe(true);
    expect(record?.winnerTeam).toBe(0);
  });

  it('拒绝非法输入', () => {
    expect(parseMatchRecord(null)).toBeNull();
    expect(parseMatchRecord('nope')).toBeNull();
    expect(parseMatchRecord(recordInput({ matchId: undefined }))).toBeNull();
    expect(parseMatchRecord(recordInput({ players: 'nope' }))?.players).toEqual([]);
  });

  it('解析排行榜响应体，坏条目被跳过', () => {
    const entries = [recordInput(), { matchId: 3 }, null];
    expect(parseLeaderboardBody({ ok: true, entries })).toHaveLength(1);
    expect(parseLeaderboardBody({ ok: true })).toEqual([]);
    expect(parseLeaderboardBody('nope')).toEqual([]);
  });
});

describe('结算派生值', () => {
  it('命中率夹在 0–100 之间', () => {
    const player = parseMatchRecord(recordInput())?.players[0];
    expect(player).toBeDefined();
    if (player === undefined) return;
    expect(accuracyPercentOf(player)).toBe(60);
    expect(accuracyPercentOf({ ...player, hits: 0, shotsFired: 0 })).toBe(0);
    expect(accuracyPercentOf({ ...player, hits: 999 })).toBe(100);
  });

  it('时长格式化为 m:ss', () => {
    expect(formatDurationMs(0)).toBe('0:00');
    expect(formatDurationMs(65000)).toBe('1:05');
    expect(formatDurationMs(3600000)).toBe('60:00');
  });

  it('胜负横幅文案', () => {
    expect(winnerBannerOf(0)).toBe('胜利：守住牧场');
    expect(winnerBannerOf(1)).toBe('失败：羊群获胜');
  });

  it('按胜负与波次匹配刚结束的记录', () => {
    const record = parseMatchRecord(recordInput());
    expect(record).not.toBeNull();
    if (record === null) return;
    const summary = { winnerTeam: 0, waveReached: 10, durationMs: 0, players: [] };
    expect(pickMatchRecord([record], summary)).toBe(record);
    expect(
      pickMatchRecord([record], { ...summary, winnerTeam: 1, waveReached: 7 }),
    ).toBeUndefined();
  });

  it('排行榜行文案包含最高击杀', () => {
    const record = parseMatchRecord(recordInput());
    if (record === null) return;
    expect(leaderboardRowText(record)).toBe('AB2D-1000 · 第10波 · 最高击杀 42 · 2:05');
  });
});

describe('结算页 HTTP 读取', () => {
  it('用相对 URL 请求排行榜前 10 并解析', async () => {
    const urls: string[] = [];
    const fetcher: JsonFetcher = (url) => {
      urls.push(url);
      return Promise.resolve({
        ok: true,
        json: () => Promise.resolve({ ok: true, entries: [recordInput()] }),
      });
    };
    const records: MatchRecord[] = await fetchLeaderboard(fetcher);
    expect(urls).toEqual(['/api/leaderboard?limit=10']);
    expect(records).toHaveLength(1);
  });

  it('HTTP 失败或网络异常时返回空列表', async () => {
    const failing: JsonFetcher = () =>
      Promise.resolve({ ok: false, json: () => Promise.resolve({}) });
    expect(await fetchLeaderboard(failing)).toEqual([]);
    const throwing: JsonFetcher = () => Promise.reject(new Error('offline'));
    expect(await fetchLeaderboard(throwing)).toEqual([]);
  });
});
