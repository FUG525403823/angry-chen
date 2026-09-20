import { createServer } from 'node:http';
import { resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

import { LIMITS } from '@ac/shared';

import { createHttpHandler } from './http.ts';
import { LOG_EVENTS, createStdoutLogger, type Logger } from './log.ts';
import { createJsonMatchStore, DEFAULT_DATA_DIR } from './match/store.ts';
import { createOriginGuard, parseAllowedOrigins } from './security.ts';
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

function describeError(error: unknown): string {
  if (error instanceof Error) return error.stack ?? error.message;
  return String(error);
}

export async function startServer(
  env: NodeJS.ProcessEnv = process.env,
  logger: Logger = createStdoutLogger(env),
): Promise<StartedServer> {
  const port = readIntEnv('PORT', DEFAULT_PORT, env);
  const host = env.HOST ?? DEFAULT_HOST;
  const maxRooms = readIntEnv('MAX_ROOMS', LIMITS.maxRooms, env);
  const maxPlayersPerRoom = readIntEnv('MAX_PLAYERS_PER_ROOM', LIMITS.maxPlayersPerRoom, env);
  const dataDir = env.DATA_DIR ?? DEFAULT_DATA_DIR;
  const store = createJsonMatchStore({ dir: dataDir });
  await store.load();
  const httpServer = createServer();
  const originPolicy = parseAllowedOrigins(env.ALLOWED_ORIGINS);
  const guardOrigin = createOriginGuard(originPolicy, (origin?: string) => {
    logger.warn(LOG_EVENTS.securityOriginRejected, { detail: { origin: origin ?? '' } });
  });
  httpServer.on('upgrade', guardOrigin);
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
    log: logger,
    dataDir,
  });
  httpServer.on('request', createHttpHandler(game, Date.now()));
  await new Promise<void>((resolveListen) => {
    httpServer.listen(port, host, () => resolveListen());
  });
  await game.listen();
  const address = httpServer.address();
  const boundPort = address !== null && typeof address !== 'string' ? address.port : port;
  logger.info(LOG_EVENTS.listening, {
    detail: {
      url: 'http://' + host + ':' + String(boundPort),
      health: '/health',
      metrics: '/metrics',
      leaderboard: '/api/leaderboard',
      recentMatches: '/api/matches/recent',
      dataDir,
      maxRooms,
      maxPlayersPerRoom,
      allowedOrigins: originPolicy.mode,
    },
  });
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
  const logger = createStdoutLogger();
  process.on('uncaughtException', (error: unknown) => {
    logger.error(LOG_EVENTS.errorUncaught, { detail: { error: describeError(error) } });
  });
  process.on('unhandledRejection', (reason: unknown) => {
    logger.error(LOG_EVENTS.errorUnhandledRejection, { detail: { reason: describeError(reason) } });
  });
  const server = await startServer(process.env, logger);
  let shuttingDown = false;
  const shutdown = (signal: string): void => {
    if (shuttingDown) return;
    shuttingDown = true;
    logger.info(LOG_EVENTS.shutdownRequested, { detail: { signal } });
    const forced = setTimeout(() => process.exit(1), 3000);
    forced.unref();
    void server.close().then(() => {
      logger.info(LOG_EVENTS.shutdownComplete, {});
      process.exit(0);
    });
  };
  process.on('SIGINT', () => shutdown('SIGINT'));
  process.on('SIGTERM', () => shutdown('SIGTERM'));
}

const entry = process.argv[1];
if (entry !== undefined && resolve(entry) === fileURLToPath(import.meta.url)) {
  main().catch((error: unknown) => {
    createStdoutLogger().error(LOG_EVENTS.startFailed, { detail: { error: describeError(error) } });
    process.exit(1);
  });
}
