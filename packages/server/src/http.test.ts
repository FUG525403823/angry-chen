import { mkdtemp, rm } from 'node:fs/promises';
import { createServer } from 'node:http';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

import { describe, expect, it } from 'vitest';

import { PROTOCOL_VERSION } from '@ac/shared';

import { createHttpHandler } from './http.ts';
import { createJsonMatchStore } from './match/store.ts';
import { createHarness } from './testing/harness.ts';

async function startHttp(
  harness: ReturnType<typeof createHarness>,
): Promise<{ port: number; close(): Promise<void> }> {
  const server = createServer(createHttpHandler(harness.game, Date.now()));
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
});
