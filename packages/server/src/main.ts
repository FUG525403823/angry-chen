import { createServer } from 'node:http';
import { networkInterfaces } from 'node:os';
import { resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

import { LIMITS } from '@ac/shared';

import { createHttpHandler } from './http.ts';
import { LOG_EVENTS, createStdoutLogger, type Logger } from './log.ts';
import { createJsonMatchStore, DEFAULT_DATA_DIR, DEFAULT_MAX_RECORDS } from './match/store.ts';
import { DEFAULT_REPORT_RETENTION } from './report.ts';
import { createOriginGuard, parseAllowedOrigins } from './security.ts';
import { createGameServer } from './server.ts';
import { createStaticServer, resolveClientDist } from './static.ts';
import { createWsTransport } from './transport/ws-adapter.ts';

export const DEFAULT_PORT = 8787;
export const DEFAULT_HOST = '127.0.0.1';
/** ADR-007：CLI 入口加载 .env 的环境变量名与默认文件名。 */
export const ENV_FILE_ENV = 'AC_ENV_FILE';
export const DEFAULT_ENV_FILE = '.env';
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

/** O09：计数类环境变量（允许 0；非法值回落默认并记 `config.fallback` 日志）。 */
function readCountEnv(
  name: string,
  fallback: number,
  env: NodeJS.ProcessEnv,
  logger: Logger,
): number {
  const raw = env[name];
  if (raw === undefined || raw.trim() === '') return fallback;
  const value = Number(raw);
  if (!Number.isInteger(value) || value < 0) {
    logger.warn(LOG_EVENTS.configFallback, { detail: { name, raw, fallback } });
    return fallback;
  }
  return value;
}

function describeError(error: unknown): string {
  if (error instanceof Error) return error.stack ?? error.message;
  return String(error);
}

export function isLoopbackHost(host: string): boolean {
  return host === DEFAULT_HOST || host === 'localhost' || host === '::1';
}

/**
 * ADR-007：启动日志里给出「朋友可以打开的地址」。
 * 只监听回环时给回环地址——其它地址根本连不上，列出来是误导。
 */
export function joinUrlsFor(host: string, port: number, addresses: readonly string[]): string[] {
  if (isLoopbackHost(host)) return ['http://127.0.0.1:' + String(port)];
  const urls: string[] = [];
  for (const address of addresses) urls.push('http://' + address + ':' + String(port));
  return urls;
}

export function nonInternalIPv4(): string[] {
  const addresses: string[] = [];
  for (const entries of Object.values(networkInterfaces())) {
    if (entries === undefined) continue;
    for (const entry of entries) {
      if (entry.family !== 'IPv4' || entry.internal) continue;
      addresses.push(entry.address);
    }
  }
  return addresses;
}

/** ADR-007：.env 候选路径——cwd 与仓库根（`pnpm --filter` 的 cwd 是包目录，不是仓库根）。 */
export function resolveEnvFileCandidates(cwd: string, moduleUrl: string): string[] {
  const repoRoot = fileURLToPath(new URL('../../../', moduleUrl));
  return [resolve(cwd, DEFAULT_ENV_FILE), resolve(repoRoot, DEFAULT_ENV_FILE)];
}

/** ADR-007：只在 CLI 入口调用；已存在的真实环境变量不会被 .env 覆盖（Node 原生语义）。 */
function loadEnvFile(moduleUrl: string): void {
  const explicit = process.env[ENV_FILE_ENV];
  const candidates =
    explicit === undefined || explicit.trim() === ''
      ? resolveEnvFileCandidates(process.cwd(), moduleUrl)
      : [resolve(process.cwd(), explicit)];
  for (const candidate of candidates) {
    try {
      process.loadEnvFile(candidate);
      return;
    } catch {
      // 文件不存在属正常情况：静默尝试下一个候选。
    }
  }
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
  const maxRecords = readCountEnv('MATCH_STORE_MAX_RECORDS', DEFAULT_MAX_RECORDS, env, logger);
  const reportRetention = readCountEnv('REPORT_RETENTION', DEFAULT_REPORT_RETENTION, env, logger);
  const store = createJsonMatchStore({ dir: dataDir, maxRecords });
  await store.load();
  const httpServer = createServer();
  const originPolicy = parseAllowedOrigins(env.ALLOWED_ORIGINS);
  const guardOrigin = createOriginGuard(originPolicy, (origin?: string) => {
    logger.warn(LOG_EVENTS.securityOriginRejected, { detail: { origin: origin ?? '' } });
  });
  httpServer.on('upgrade', guardOrigin);
  const clientDist = resolveClientDist(env.CLIENT_DIST, import.meta.url);
  if (clientDist.explicit && clientDist.root === null) {
    logger.warn(LOG_EVENTS.clientDistMissing, {
      detail: { name: 'CLIENT_DIST', path: clientDist.requested ?? '' },
    });
  }
  const staticServer =
    clientDist.root === null ? undefined : createStaticServer({ root: clientDist.root });
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
    reportRetention,
  });
  httpServer.on('request', createHttpHandler(game, Date.now(), staticServer));
  await new Promise<void>((resolveListen) => {
    httpServer.listen(port, host, () => resolveListen());
  });
  await game.listen();
  const address = httpServer.address();
  const boundPort = address !== null && typeof address !== 'string' ? address.port : port;
  const joinUrls = joinUrlsFor(host, boundPort, nonInternalIPv4());
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
      clientDist: clientDist.root,
      joinUrls,
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
  loadEnvFile(import.meta.url);
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
