import { mkdir, mkdtemp, rm, writeFile } from 'node:fs/promises';
import { createServer } from 'node:http';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

import { describe, expect, it } from 'vitest';

import { PROTOCOL_VERSION } from '@ac/shared';

import { createHttpHandler } from './http.ts';
import { createJsonMatchStore, type MatchResultRecord } from './match/store.ts';
import { createStaticServer, type StaticServer } from './static.ts';
import { createHarness } from './testing/harness.ts';

function makeStoredRecord(matchId: string, kills: number): MatchResultRecord {
  return {
    matchId,
    startedAtMs: kills * 1000,
    durationMs: 1000,
    waveReached: 3,
    winnerTeam: 0,
    playerCount: 1,
    players: [
      {
        name: 'alice',
        kills,
        headshots: 0,
        shotsFired: 10,
        hits: 5,
        revives: 0,
        downs: 0,
        aliveMs: 900,
        leftMidMatch: false,
      },
    ],
  };
}

async function startHttp(
  harness: ReturnType<typeof createHarness>,
  staticServer?: StaticServer,
): Promise<{ port: number; close(): Promise<void> }> {
  const server = createServer(createHttpHandler(harness.game, Date.now(), staticServer));
  await new Promise<void>((resolveListen) => {
    server.listen(0, '127.0.0.1', () => resolveListen());
  });
  const address = server.address();
  const port = address !== null && typeof address !== 'string' ? address.port : 0;
  return {
    port,
    close(): Promise<void> {
      return new Promise<void>((resolveClose) => {
        server.close(() => resolveClose());
      });
    },
  };
}

describe('运维端点', () => {
  it('/health 返回 200 与房间/连接状态', async () => {
    const harness = createHarness();
    const http = await startHttp(harness);
    const alice = harness.connect();
    alice.join('alice', '0000');

    const response = await fetch('http://127.0.0.1:' + String(http.port) + '/health');
    expect(response.status).toBe(200);
    const body = (await response.json()) as Record<string, unknown>;
    expect(body['status']).toBe('ok');
    expect(body['protocolVersion']).toBe(PROTOCOL_VERSION);
    expect(body['rooms']).toBe(1);
    expect(body['connections']).toBe(1);
    expect(body['players']).toBe(1);

    const query = await fetch('http://127.0.0.1:' + String(http.port) + '/health?verbose=1');
    expect(query.status).toBe(200);

    await http.close();
    await harness.close();
  });

  it('/metrics 输出 Prometheus 文本且反映实时计数', async () => {
    const harness = createHarness();
    const http = await startHttp(harness);
    const alice = harness.connect();
    alice.join('alice', '0000');
    alice.client.send(new Uint8Array([0x7f]));
    harness.tick(5);

    const response = await fetch('http://127.0.0.1:' + String(http.port) + '/metrics');
    expect(response.status).toBe(200);
    expect(response.headers.get('content-type')).toContain('text/plain');
    const text = await response.text();
    expect(text).toContain('ac_tick_jitter_ms_p95');
    expect(text).toContain('ac_tick_jitter_ms_p50');
    expect(text).toContain('ac_snapshot_bytes_avg');
    expect(text).toContain('ac_snapshot_bytes_max');
    expect(text).toContain('ac_malformed_frames_total 1');
    expect(text).toContain('ac_grace_starts_total');
    expect(text).toContain('ac_grace_reconnects_total');
    expect(text).toContain('ac_grace_timeouts_total');
    expect(text).toContain('ac_rooms 1');
    expect(text).toContain('ac_connections 1');
    expect(text).toContain('ac_players 1');
    expect(text).toContain('ac_joins_total 1');

    await http.close();
    await harness.close();
  });

  it('/metrics 反映新增的字节上限与宽限计数', async () => {
    const harness = createHarness();
    const http = await startHttp(harness);
    const alice = harness.connect();
    const bob = harness.connect();
    alice.join('alice', '0000');
    const code = alice.welcome()?.roomCode ?? '';
    bob.join('bob', code);
    harness.tick(2);
    bob.disconnect();
    harness.tick(1);

    const response = await fetch('http://127.0.0.1:' + String(http.port) + '/metrics');
    expect(response.status).toBe(200);
    const text = await response.text();
    expect(text).toMatch(/ac_snapshot_bytes_max [1-9]/);
    expect(text).toContain('ac_grace_starts_total 1');
    expect(text).toContain('ac_grace_reconnects_total 0');
    expect(text).toContain('ac_grace_timeouts_total 0');

    await http.close();
    await harness.close();
  });

  it('/api/leaderboard 与 /api/matches/recent 返回 store 记录', async () => {
    const dir = await mkdtemp(join(tmpdir(), 'ac-http-'));
    const store = createJsonMatchStore({ dir });
    await store.load();
    await store.appendMatchResult({
      matchId: 'X-1',
      startedAtMs: 10,
      durationMs: 1000,
      waveReached: 4,
      winnerTeam: 0,
      playerCount: 1,
      players: [
        {
          name: 'alice',
          kills: 6,
          headshots: 2,
          shotsFired: 20,
          hits: 12,
          revives: 1,
          downs: 0,
          aliveMs: 900,
          leftMidMatch: false,
        },
      ],
    });
    await store.flush();

    const harness = createHarness({ store });
    const http = await startHttp(harness);
    const base = 'http://127.0.0.1:' + String(http.port);

    const leaderboard = await fetch(base + '/api/leaderboard?limit=20');
    expect(leaderboard.status).toBe(200);
    const boardBody: unknown = await leaderboard.json();
    expect(boardBody).toMatchObject({ ok: true });

    const recent = await fetch(base + '/api/matches/recent?limit=10');
    expect(recent.status).toBe(200);
    const recentBody: unknown = await recent.json();
    expect(recentBody).toMatchObject({ ok: true, entries: [{ matchId: 'X-1' }] });

    await http.close();
    await harness.close();
    await rm(dir, { recursive: true, force: true });
  });

  it('O09①：连发 31 次超过 30 次/分钟 → 第 31 次 429 且带 retry-after', async () => {
    const harness = createHarness();
    const http = await startHttp(harness);
    const url = 'http://127.0.0.1:' + String(http.port) + '/api/leaderboard?limit=5';
    let last = 0;
    let lastRetryAfter: string | null = null;
    for (let i = 0; i < 31; i += 1) {
      const response = await fetch(url);
      last = response.status;
      if (i === 30) lastRetryAfter = response.headers.get('retry-after');
    }
    expect(last).toBe(429);
    expect(lastRetryAfter).toBe('60');
    expect(harness.game.metrics.httpRateLimited).toBe(1);

    await http.close();
    await harness.close();
  });

  it('O09②：两次相同读请求，第二次命中 60s 缓存（httpCacheHits 增长）', async () => {
    const dir = await mkdtemp(join(tmpdir(), 'ac-http-cache-'));
    const store = createJsonMatchStore({ dir });
    await store.load();
    await store.appendMatchResult(makeStoredRecord('C-1', 5));
    await store.flush();

    const harness = createHarness({ store });
    const http = await startHttp(harness);
    const base = 'http://127.0.0.1:' + String(http.port);

    const first = await fetch(base + '/api/leaderboard?limit=3');
    expect(first.status).toBe(200);
    expect(harness.game.metrics.httpCacheHits).toBe(0);
    const second = await fetch(base + '/api/leaderboard?limit=3');
    expect(second.status).toBe(200);
    expect(await second.json()).toEqual(await first.json());
    expect(harness.game.metrics.httpCacheHits).toBe(1);

    const metrics = await fetch(base + '/metrics');
    const text = await metrics.text();
    expect(text).toContain('ac_records_retained 1');
    expect(text).toContain('ac_http_cache_hits_total 1');
    expect(text).toContain('ac_http_rate_limited_total 0');

    await http.close();
    await harness.close();
    await rm(dir, { recursive: true, force: true });
  });

  it('O09③：写入新对局后缓存失效（不需要等 TTL）', async () => {
    const dir = await mkdtemp(join(tmpdir(), 'ac-http-invalidate-'));
    const store = createJsonMatchStore({ dir });
    await store.load();
    await store.appendMatchResult(makeStoredRecord('D-1', 1));
    await store.flush();

    const harness = createHarness({ store });
    const http = await startHttp(harness);
    const base = 'http://127.0.0.1:' + String(http.port);

    const before = (await (await fetch(base + '/api/matches/recent?limit=5')).json()) as {
      entries: { matchId: string }[];
    };
    expect(before.entries.map((entry) => entry.matchId)).toEqual(['D-1']);

    await store.appendMatchResult(makeStoredRecord('D-2', 2));
    await store.flush();

    const after = (await (await fetch(base + '/api/matches/recent?limit=5')).json()) as {
      entries: { matchId: string }[];
    };
    expect(after.entries.map((entry) => entry.matchId)).toEqual(['D-2', 'D-1']);
    expect(harness.game.metrics.httpCacheHits).toBe(0);

    await http.close();
    await harness.close();
    await rm(dir, { recursive: true, force: true });
  });

  it('未知路径返回 404', async () => {
    const harness = createHarness();
    const http = await startHttp(harness);
    const response = await fetch('http://127.0.0.1:' + String(http.port) + '/nope');
    expect(response.status).toBe(404);
    const body = (await response.json()) as Record<string, unknown>;
    expect(body['error']).toBe('not-found');
    await http.close();
    await harness.close();
  });

  it('挂载静态托管后 / 返回页面，运维端点仍然优先（ADR-007）', async () => {
    const harness = createHarness();
    const dir = await mkdtemp(join(tmpdir(), 'ac-http-static-'));
    await mkdir(join(dir, 'assets'), { recursive: true });
    await writeFile(join(dir, 'index.html'), '<!doctype html><title>ac</title>');
    await writeFile(join(dir, 'assets', 'app.js'), 'const a = 1;\n');
    const http = await startHttp(harness, createStaticServer({ root: dir }));
    const base = 'http://127.0.0.1:' + String(http.port);

    const page = await fetch(base + '/');
    expect(page.status).toBe(200);
    expect(await page.text()).toContain('<title>ac</title>');

    const asset = await fetch(base + '/assets/app.js');
    expect(asset.status).toBe(200);
    expect(String(asset.headers.get('cache-control'))).toContain('immutable');

    const health = await fetch(base + '/health');
    expect(health.status).toBe(200);
    expect(((await health.json()) as Record<string, unknown>)['status']).toBe('ok');

    const board = await fetch(base + '/api/leaderboard');
    expect(board.status).toBe(200);

    const missing = await fetch(base + '/nope');
    expect(missing.status).toBe(404);

    await http.close();
    await harness.close();
    await rm(dir, { recursive: true, force: true });
  });
});
