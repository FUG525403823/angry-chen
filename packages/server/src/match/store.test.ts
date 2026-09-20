import { appendFile, mkdtemp, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

import { describe, expect, it } from 'vitest';

import { createJsonMatchStore, type MatchResultRecord } from './store.ts';

function makeRecord(matchId: string, startedAtMs: number, kills: number): MatchResultRecord {
  return {
    matchId,
    startedAtMs,
    durationMs: 60_000,
    waveReached: 3,
    winnerTeam: 0,
    playerCount: 1,
    players: [
      {
        name: 'p',
        kills,
        headshots: 0,
        shotsFired: 10,
        hits: 5,
        revives: 0,
        downs: 0,
        aliveMs: 50_000,
        leftMidMatch: false,
      },
    ],
  };
}

async function tempDir(): Promise<string> {
  return mkdtemp(join(tmpdir(), 'ac-store-'));
}

describe('JsonMatchStore（NDJSON）', () => {
  it('跳过损坏行、按击杀与时间排序且不抛异常', async () => {
    const dir = await tempDir();
    const store = createJsonMatchStore({ dir });
    await store.load();
    await appendFile(store.filePath, JSON.stringify(makeRecord('B-4000', 4000, 5)) + '\n');
    await appendFile(store.filePath, '{ this is not json\n');
    await appendFile(store.filePath, JSON.stringify(makeRecord('A-1000', 1000, 9)) + '\n');

    const reloaded = createJsonMatchStore({ dir });
    await reloaded.load();
    expect(reloaded.corruptLines).toBe(1);

    const top = await reloaded.listTopScores(10);
    expect(top.map((record) => record.matchId)).toEqual(['A-1000', 'B-4000']);

    const recent = await reloaded.getRecentMatches(10);
    expect(recent.map((record) => record.matchId)).toEqual(['B-4000', 'A-1000']);
    expect(await reloaded.listTopScores(0)).toEqual([]);
    expect(await reloaded.getRecentMatches(0)).toEqual([]);

    await rm(dir, { recursive: true, force: true });
  });

  it('appendMatchResult 追加一行，重启后可读', async () => {
    const dir = await tempDir();
    const store = createJsonMatchStore({ dir });
    await store.load();
    await store.appendMatchResult(makeRecord('C-100', 100, 3));
    await store.flush();

    const reloaded = createJsonMatchStore({ dir });
    await reloaded.load();
    expect(reloaded.corruptLines).toBe(0);
    const recent = await reloaded.getRecentMatches(5);
    expect(recent.length).toBe(1);
    expect(recent[0]?.matchId).toBe('C-100');

    await rm(dir, { recursive: true, force: true });
  });
});
