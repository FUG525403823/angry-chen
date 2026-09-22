import { appendFile, mkdtemp, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

import { describe, expect, it } from 'vitest';

import { createJsonMatchStore, MATCHES_FILE_NAME, type MatchResultRecord } from './store.ts';

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

  it('O09①：10 万行文件流式加载，常驻记录被 maxRecords 压住', async () => {
    const dir = await tempDir();
    const file = join(dir, MATCHES_FILE_NAME);
    const total = 100_000;
    const chunk: string[] = [];
    for (let i = 0; i < total; i += 1) {
      chunk.push(JSON.stringify(makeRecord('M-' + String(i).padStart(6, '0'), i, i % 7)));
      if (chunk.length === 10_000) {
        await appendFile(file, chunk.join(String.fromCharCode(10)) + String.fromCharCode(10));
        chunk.length = 0;
      }
    }
    const store = createJsonMatchStore({ dir, maxRecords: 10_000 });
    await store.load();
    expect(store.corruptLines).toBe(0);
    expect(store.recordCount).toBeLessThanOrEqual(10_000);
    expect(store.recordCount).toBeGreaterThan(9_900);
    const recent = await store.getRecentMatches(3);
    expect(recent[0]?.matchId).toBe('M-099999');

    await rm(dir, { recursive: true, force: true });
  }, 60_000);

  it('O09②：maxRecords=100 时交替写入 200 条，最旧被淘汰、最新都在', async () => {
    const dir = await tempDir();
    const store = createJsonMatchStore({ dir, maxRecords: 100 });
    await store.load();
    for (let i = 0; i < 200; i += 1) {
      await store.appendMatchResult(makeRecord('N-' + String(i).padStart(3, '0'), i, i % 5));
    }
    await store.flush();
    expect(store.recordCount).toBe(100);
    const recent = await store.getRecentMatches(100);
    expect(recent.length).toBe(100);
    expect(recent[0]?.matchId).toBe('N-199');
    expect(recent.map((record) => record.matchId)).not.toContain('N-099');
    expect(await store.getRecentMatches(100)).toEqual(recent);

    await rm(dir, { recursive: true, force: true });
  });

  it('O09③：top-N 与全排序取前 N 逐条一致（含并列决胜）', async () => {
    const dir = await tempDir();
    const store = createJsonMatchStore({ dir, maxRecords: 1_000 });
    await store.load();
    const records: MatchResultRecord[] = [];
    for (let i = 0; i < 60; i += 1) {
      const kills = i % 4;
      const startedAtMs = 1_000 + (i % 5) * 10;
      const record = makeRecord('T-' + String(i).padStart(3, '0'), startedAtMs, kills);
      records.push(record);
      await store.appendMatchResult(record);
    }
    await store.flush();
    const reference = records
      .map((record) => ({ record, maxKills: record.players[0]?.kills ?? 0 }))
      .sort((a, b) => {
        if (b.maxKills !== a.maxKills) return b.maxKills - a.maxKills;
        if (b.record.startedAtMs !== a.record.startedAtMs) {
          return b.record.startedAtMs - a.record.startedAtMs;
        }
        return a.record.matchId < b.record.matchId ? -1 : 1;
      })
      .map((entry) => entry.record.matchId);
    for (const limit of [1, 7, 25, 60]) {
      const top = await store.listTopScores(limit);
      expect(top.map((record) => record.matchId)).toEqual(reference.slice(0, limit));
    }

    await rm(dir, { recursive: true, force: true });
  });

  it('O09④：写入新对局后 top/recent 立刻反映（缓存失效）', async () => {
    const dir = await tempDir();
    const store = createJsonMatchStore({ dir, maxRecords: 10 });
    await store.load();
    await store.appendMatchResult(makeRecord('X-001', 1000, 1));
    expect((await store.listTopScores(1))[0]?.matchId).toBe('X-001');
    const versionBefore = store.version;
    await store.appendMatchResult(makeRecord('X-002', 2000, 9));
    expect(store.version).toBeGreaterThan(versionBefore);
    expect((await store.listTopScores(1))[0]?.matchId).toBe('X-002');
    expect((await store.getRecentMatches(1))[0]?.matchId).toBe('X-002');

    await rm(dir, { recursive: true, force: true });
  });

  it('O09⑤：坏行计数语义不变（流式加载后仍只统计坏行）', async () => {
    const dir = await tempDir();
    const file = join(dir, MATCHES_FILE_NAME);
    await appendFile(file, JSON.stringify(makeRecord('G-1', 1, 1)) + String.fromCharCode(10));
    await appendFile(file, 'not json' + String.fromCharCode(10));
    await appendFile(file, '{"matchId":1}' + String.fromCharCode(10));
    await appendFile(file, JSON.stringify(makeRecord('G-2', 2, 2)) + String.fromCharCode(10));
    const store = createJsonMatchStore({ dir, maxRecords: 5 });
    await store.load();
    expect(store.corruptLines).toBe(2);
    expect(store.recordCount).toBe(2);

    await rm(dir, { recursive: true, force: true });
  });
});
