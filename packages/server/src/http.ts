import type { IncomingMessage, ServerResponse } from 'node:http';
import { PROTOCOL_VERSION, SNAPSHOT_RATE_X10 } from '@ac/shared';

import { renderPrometheus } from './metrics.ts';
import type { GameServer } from './server.ts';
import type { StaticServer } from './static.ts';

function jsonHeaders(): Record<string, string> {
  return { 'content-type': 'application/json; charset=utf-8', 'cache-control': 'no-store' };
}

function parseLimit(url: string, fallback: number): number {
  const queryIndex = url.indexOf('?');
  if (queryIndex < 0) return fallback;
  const params = new URLSearchParams(url.slice(queryIndex + 1));
  const raw = params.get('limit');
  if (raw === null) return fallback;
  const value = Number.parseInt(raw, 10);
  if (!Number.isFinite(value) || value <= 0) return fallback;
  return Math.min(value, 100);
}

/** O09：读接口 60s TTL 缓存 + 30 次/分钟/IP 限流（冻结契约见 O09 §5）。 */
export const HTTP_CACHE_TTL_MS = 60_000;
export const HTTP_READ_RATE_LIMIT = 30;
export const HTTP_RATE_WINDOW_MS = 60_000;

/** O09：按 IP 的滑动窗口（与 WS 侧 `checkRateLimit` 同款模式，读接口专用）。 */
function isReadRateLimited(hits: Map<string, number[]>, ip: string, nowMs: number): boolean {
  const list = hits.get(ip) ?? [];
  let keep = 0;
  for (let i = 0; i < list.length; i += 1) {
    const at = list[i];
    if (at !== undefined && nowMs - at < HTTP_RATE_WINDOW_MS) {
      list[keep] = at;
      keep += 1;
    }
  }
  list.length = keep;
  if (keep >= HTTP_READ_RATE_LIMIT) {
    hits.set(ip, list);
    return true;
  }
  list.push(nowMs);
  hits.set(ip, list);
  return false;
}

export function createHttpHandler(
  game: GameServer,
  startedAtMs: number,
  staticServer?: StaticServer,
): (req: IncomingMessage, res: ServerResponse) => void {
  const readCache = new Map<string, { atMs: number; version: number; body: string }>();
  const rateHits = new Map<string, number[]>();
  return (req: IncomingMessage, res: ServerResponse): void => {
    const url = req.url ?? '/';
    const path = url.split('?')[0] ?? '/';
    if (path === '/api/leaderboard' || path === '/api/matches/recent') {
      const fallback = path === '/api/leaderboard' ? 20 : 10;
      const limit = parseLimit(url, fallback);
      const ip = req.socket.remoteAddress ?? 'unknown';
      const nowMs = Date.now();
      if (isReadRateLimited(rateHits, ip, nowMs)) {
        game.metrics.httpRateLimited += 1;
        res.writeHead(429, {
          ...jsonHeaders(),
          'retry-after': String(Math.ceil(HTTP_RATE_WINDOW_MS / 1000)),
        });
        res.end(JSON.stringify({ ok: false, error: 'rate-limited' }));
        return;
      }
      const key = path + ':' + String(limit);
      const version = game.store.version ?? 0;
      const cached = readCache.get(key);
      if (
        cached !== undefined &&
        cached.version === version &&
        nowMs - cached.atMs < HTTP_CACHE_TTL_MS
      ) {
        game.metrics.httpCacheHits += 1;
        res.writeHead(200, jsonHeaders());
        res.end(cached.body);
        return;
      }
      const pending =
        path === '/api/leaderboard'
          ? game.store.listTopScores(limit)
          : game.store.getRecentMatches(limit);
      pending.then(
        (entries) => {
          const body = JSON.stringify({ ok: true, entries });
          readCache.set(key, { atMs: nowMs, version, body });
          res.writeHead(200, jsonHeaders());
          res.end(body);
        },
        (error: unknown) => {
          res.writeHead(500, jsonHeaders());
          res.end(JSON.stringify({ ok: false, error: String(error) }));
        },
      );
      return;
    }
    if (path === '/health') {
      const body = JSON.stringify({
        status: 'ok',
        protocolVersion: PROTOCOL_VERSION,
        rooms: game.rooms.size,
        connections: game.sessionCount,
        players: playerCount(game),
        ticks: game.metrics.ticks,
      });
      res.writeHead(200, {
        'content-type': 'application/json; charset=utf-8',
        'cache-control': 'no-store',
      });
      res.end(body);
      return;
    }
    if (path === '/metrics') {
      game.metrics.recordsRetained = game.store.recordCount ?? game.metrics.recordsRetained;
      const body = renderPrometheus(game.metrics, {
        rooms: game.rooms.size,
        connections: game.sessionCount,
        players: playerCount(game),
        uptimeSeconds: (Date.now() - startedAtMs) / 1000,
        oversizedFrames: game.transport.oversizedFrames ?? 0,
        slowClientDrops: game.transport.slowClientDrops ?? 0,
        sendQueueBytes: game.transport.sendQueueBytes ?? 0,
        clientLagTicksAvg: clientLagTicksAvg(game),
        graceActive: graceActive(game),
        snapshotRateX10: minSnapshotRateX10(game),
      });
      res.writeHead(200, {
        'content-type': 'text/plain; version=0.0.4; charset=utf-8',
        'cache-control': 'no-store',
      });
      res.end(body);
      return;
    }
    // ADR-007：静态托管排在所有运维端点之后，绝不吞掉 /health、/metrics、/api/*。
    if (staticServer !== undefined && staticServer.handle(req, res)) return;
    res.writeHead(404, { 'content-type': 'application/json; charset=utf-8' });
    res.end(JSON.stringify({ error: 'not-found', path }));
  };
}

/** O05：各房间自适应快照率的最小值（1/10 Hz）；无房间时给默认档位。 */
export function minSnapshotRateX10(game: GameServer): number {
  let min = SNAPSHOT_RATE_X10;
  let seen = false;
  for (const room of game.rooms.rooms.values()) {
    min = seen ? Math.min(min, room.snapshotRateX10) : room.snapshotRateX10;
    seen = true;
  }
  return min;
}

export function clientLagTicksAvg(game: GameServer): number {
  let samples = 0;
  let total = 0;
  for (const room of game.rooms.rooms.values()) {
    for (const session of room.sessions) {
      samples += 1;
      total += session.lagTicks;
    }
  }
  if (samples === 0) return 0;
  return total / samples;
}

export function graceActive(game: GameServer): number {
  let count = 0;
  for (const room of game.rooms.rooms.values()) {
    for (const session of room.sessions) {
      if (session.disconnectedAtMs !== null) count += 1;
    }
  }
  return count;
}

export function playerCount(game: GameServer): number {
  let players = 0;
  for (const room of game.rooms.rooms.values()) players += room.sessions.length;
  return players;
}
