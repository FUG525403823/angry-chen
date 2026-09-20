const newline = String.fromCharCode(10);

export const LOG_LEVEL = Object.freeze({ debug: 10, info: 20, warn: 30, error: 40 } as const);

export type LogLevelName = keyof typeof LOG_LEVEL;

export const LOG_LEVEL_NAMES: readonly LogLevelName[] = Object.freeze([
  'debug',
  'info',
  'warn',
  'error',
]);

export const DEFAULT_LOG_LEVEL: LogLevelName = 'info';
export const LOG_LEVEL_ENV = 'LOG_LEVEL';

export const LOG_EVENTS = Object.freeze({
  roomCreate: 'room.create',
  roomReclaim: 'room.reclaim',
  sessionJoin: 'session.join',
  sessionLeave: 'session.leave',
  sessionReconnect: 'session.reconnect',
  sessionRateLimited: 'session.rate_limited',
  sessionJoinThrottled: 'session.join_throttled',
  chatMessage: 'chat.message',
  frameMalformed: 'frame.malformed',
  frameOversized: 'frame.oversized',
  waveStart: 'wave.start',
  waveClear: 'wave.clear',
  matchStart: 'match.start',
  matchEnd: 'match.end',
  matchTransitionDenied: 'match.transition_denied',
  anticheatSpeed: 'anticheat.speed',
  graceStart: 'grace.start',
  graceTimeout: 'grace.timeout',
  graceReconnect: 'grace.reconnect',
  securityOriginRejected: 'security.origin_rejected',
  storeError: 'store.error',
  reportWriteFailed: 'report.write_failed',
  serverListening: 'server.listening',
  serverShutdownRequested: 'server.shutdown_requested',
  serverShutdownComplete: 'server.shutdown_complete',
  serverStartFailed: 'server.start_failed',
  errorUncaught: 'error.uncaught',
} as const);

export type LogEvent = (typeof LOG_EVENTS)[keyof typeof LOG_EVENTS];

export type LogDetailValue = string | number | boolean;
export type LogDetail = Readonly<Record<string, LogDetailValue | undefined>>;

export const EMPTY_DETAIL: LogDetail = Object.freeze({});

export interface LogFields {
  readonly room?: string;
  readonly tick?: number;
  readonly pid?: number;
  readonly detail?: LogDetail;
}

export interface LoggerOptions {
  readonly level?: string;
  readonly write?: (line: string) => void;
  readonly now?: () => number;
}

export interface Logger {
  readonly level: LogLevelName;
  enabled(level: LogLevelName): boolean;
  log(level: LogLevelName, evt: LogEvent, fields?: LogFields): void;
  debug(evt: LogEvent, fields?: LogFields): void;
  info(evt: LogEvent, fields?: LogFields): void;
  warn(evt: LogEvent, fields?: LogFields): void;
  error(evt: LogEvent, fields?: LogFields): void;
}

export function parseLogLevel(raw: string | undefined): LogLevelName | null {
  if (raw === undefined) return null;
  const value = raw.trim().toLowerCase();
  if (value === 'debug') return 'debug';
  if (value === 'info') return 'info';
  if (value === 'warn') return 'warn';
  if (value === 'error') return 'error';
  return null;
}

export function resolveLogLevel(
  raw: string | undefined,
  fallback: LogLevelName = DEFAULT_LOG_LEVEL,
): LogLevelName {
  return parseLogLevel(raw) ?? fallback;
}

export function formatLogLine(
  level: LogLevelName,
  evt: LogEvent,
  fields?: LogFields,
  tsMs: number = Date.now(),
): string {
  const parts: string[] = [
    '"ts":' + JSON.stringify(tsMs),
    '"level":' + JSON.stringify(level),
    '"evt":' + JSON.stringify(evt),
    '"room":' + JSON.stringify(fields?.room ?? null),
    '"tick":' + JSON.stringify(fields?.tick ?? 0),
    '"pid":' + JSON.stringify(fields?.pid ?? 0),
    '"detail":' + JSON.stringify(fields?.detail ?? EMPTY_DETAIL),
  ];
  return '{' + parts.join(',') + '}';
}

function stdoutLine(line: string): void {
  process.stdout.write(line + newline);
}

export function createLogger(options: LoggerOptions = {}): Logger {
  const level = resolveLogLevel(options.level, DEFAULT_LOG_LEVEL);
  const threshold = LOG_LEVEL[level];
  const write = options.write ?? stdoutLine;
  const now = options.now ?? ((): number => Date.now());
  const emit = (lineLevel: LogLevelName, evt: LogEvent, fields?: LogFields): void => {
    if (LOG_LEVEL[lineLevel] < threshold) return;
    write(formatLogLine(lineLevel, evt, fields, now()));
  };
  return {
    level,
    enabled(candidate: LogLevelName): boolean {
      return LOG_LEVEL[candidate] >= threshold;
    },
    log: emit,
    debug(evt: LogEvent, fields?: LogFields): void {
      emit('debug', evt, fields);
    },
    info(evt: LogEvent, fields?: LogFields): void {
      emit('info', evt, fields);
    },
    warn(evt: LogEvent, fields?: LogFields): void {
      emit('warn', evt, fields);
    },
    error(evt: LogEvent, fields?: LogFields): void {
      emit('error', evt, fields);
    },
  };
}

export const SILENT_LOGGER: Logger = createLogger({
  write: (): void => undefined,
});
