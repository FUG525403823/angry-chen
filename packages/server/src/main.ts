import { createServer } from 'node:http';
import { resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

import { LIMITS } from '@ac/shared';

import { createHttpHandler } from './http.ts';
import { formatLogLine } from './log.ts';
import { createJsonMatchStore, DEFAULT_DATA_DIR } from './match/store.ts';
import { createGameServer } from './server.ts';
import { createWsTransport } from './transport/ws-adapter.ts';

export const DEFAULT_PORT = 8787;
export const DEFAULT_HOST = '127.0.0.1';
export interface StartedServer {
  readonly port: number;
  close(): Promise<void>;
}

export function readIntEnv(
  name: string,
  fallback: number,
  env: NodeJS.ProcessEnv = process.env,
): number {
  const raw = env[name];
  if (raw === undefined || raw.trim() === '') return fallback;
  const value = Number.parseInt(raw, 10);
  if (!Number.isFinite(value) || value <= 0) return fallback;
  return value;
}

export async function startServer(env: NodeJS.ProcessEnv = process.env): Promise<StartedServer> {
  const port = readIntEnv('PORT', DEFAULT_PORT, env);
  const host = env.HOST ?? DEFAULT_HOST;
  const maxRooms = readIntEnv('MAX_ROOMS', LIMITS.maxRooms, env);
  const maxPlayersPerRoom = readIntEnv('MAX_PLAYERS_PER_ROOM', LIMITS.maxPlayersPerRoom, env);
  const dataDir = env.DATA_DIR ?? DEFAULT_DATA_DIR;
  const store = createJsonMatchStore({ dir: dataDir });
  await store.load();
  const httpServer = createServer();
  const transport = createWsTransport({
    port,
    host,
    server: httpServer,
    maxFrameBytes: LIMITS.maxFrameBytes,
  });
  const game = createGameServer(transport, {
    maxRooms,
    maxPlayersPerRoom,
    store,
    log: (message: string): void => {
      console.log(message);
    },
  });
  httpServer.on('request', createHttpHandler(game, Date.now()));
  await new Promise<void>((resolveListen) => {
    httpServer.listen(port, host, () => resolveListen());
  });
  await game.listen();
  const address = httpServer.address();
  const boundPort = address !== null && typeof address !== 'string' ? address.port : port;
  console.log(
    formatLogLine('info', 'listening', {
      url: 'http://' + host + ':' + String(boundPort),
      health: '/health',
      metrics: '/metrics',
      leaderboard: '/api/leaderboard',
      recentMatches: '/api/matches/recent',
      dataDir,
      maxRooms,
      maxPlayersPerRoom,
    }),
  );
  return {
    port: boundPort,
    async close(): Promise<void> {
      await game.close();
      await new Promise<void>((resolveClose) => {
        httpServer.close(() => resolveClose());
      });
    },
  };
}

async function main(): Promise<void> {
  const server = await startServer();
  let shuttingDown = false;
  const shutdown = (signal: string): void => {
    if (shuttingDown) return;
    shuttingDown = true;
    console.log(formatLogLine('info', 'shutdownRequested', { signal }));
    const forced = setTimeout(() => process.exit(1), 3000);
    forced.unref();
    void server.close().then(() => {
      console.log(formatLogLine('info', 'shutdownComplete', {}));
      process.exit(0);
    });
  };
  process.on('SIGINT', () => shutdown('SIGINT'));
  process.on('SIGTERM', () => shutdown('SIGTERM'));
}

const entry = process.argv[1];
if (entry !== undefined && resolve(entry) === fileURLToPath(import.meta.url)) {
  main().catch((error: unknown) => {
    console.error(formatLogLine('error', 'startFailed', { error: String(error) }));
    process.exit(1);
  });
}
