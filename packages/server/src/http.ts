import type { IncomingMessage, ServerResponse } from 'node:http';
import { PROTOCOL_VERSION } from '@ac/shared';

import { renderPrometheus } from './metrics.ts';
import type { GameServer } from './server.ts';

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

export function createHttpHandler(
  game: GameServer,
  startedAtMs: number,
): (req: IncomingMessage, res: ServerResponse) => void {
  return (req: IncomingMessage, res: ServerResponse): void => {
    const url = req.url ?? '/';
    const path = url.split('?')[0] ?? '/';
    if (path === '/api/leaderboard' || path === '/api/matches/recent') {
      const fallback = path === '/api/leaderboard' ? 20 : 10;
      const limit = parseLimit(url, fallback);
      const pending =
        path === '/api/leaderboard'
          ? game.store.listTopScores(limit)
          : game.store.getRecentMatches(limit);
      pending.then(
        (entries) => {
          res.writeHead(200, jsonHeaders());
          res.end(JSON.stringify({ ok: true, entries }));
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
      });
      res.writeHead(200, {
        'content-type': 'text/plain; version=0.0.4; charset=utf-8',
        'cache-control': 'no-store',
      });
      res.end(body);
      return;
    }
    res.writeHead(404, { 'content-type': 'application/json; charset=utf-8' });
    res.end(JSON.stringify({ error: 'not-found', path }));
  };
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
