import type { IncomingMessage, ServerResponse } from 'node:http';
import { PROTOCOL_VERSION } from '@ac/shared';

import { renderPrometheus } from './metrics.ts';
import type { GameServer } from './server.ts';

export function createHttpHandler(
  game: GameServer,
  startedAtMs: number,
): (req: IncomingMessage, res: ServerResponse) => void {
  return (req: IncomingMessage, res: ServerResponse): void => {
    const url = req.url ?? '/';
    const path = url.split('?')[0] ?? '/';
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
        clientLagTicksAvg: clientLagTicksAvg(game),
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

export function playerCount(game: GameServer): number {
  let players = 0;
  for (const room of game.rooms.rooms.values()) players += room.sessions.length;
  return players;
}
