export type LogLevel = 'debug' | 'info' | 'warn' | 'error';

export const LOG_LEVEL: Readonly<Record<LogLevel, number>> = Object.freeze({
  debug: 10,
  info: 20,
  warn: 30,
  error: 40,
});

export const LOG_LEVEL_NAMES: readonly LogLevel[] = Object.freeze([
  'debug',
  'info',
  'warn',
  'error',
]);

export const DEFAULT_LOG_LEVEL: LogLevel = 'info';
export const LOG_LEVEL_ENV = 'LOG_LEVEL';

export const LOG_EVENTS = Object.freeze({
  roomCreate: 'room.create',
  roomReclaim: 'room.reclaim',
  sessionJoin: 'session.join',
  sessionLeave: 'session.leave',
  sessionJoinThrottled: 'session.join_throttled',
  sessionRateLimited: 'session.rate_limited',
  chatMessage: 'chat.message',
  frameMalformed: 'malformedFrame',
  frameHandlerError: 'frameHandlerError',
  waveStart: 'wave.start',
  waveClear: 'wave.clear',
  matchStart: 'match.start',
  matchEnd: 'match.end',
  matchTransitionDenied: 'illegalTransition',
  anticheatSpeed: 'anticheat.speed',
  graceStart: 'grace.start',
  graceReconnect: 'grace.reconnect',
  graceTimeout: 'grace.timeout',
  securityOriginRejected: 'security.origin_rejected',
  storeError: 'store.error',
  reportWriteFailed: 'report.write_failed',
  listening: 'listening',
  shutdownRequested: 'shutdownRequested',
  shutdownComplete: 'shutdownComplete',
  startFailed: 'startFailed',
  errorUncaught: 'error.uncaught',
  errorUnhandledRejection: 'error.unhandledRejection',
} as const);

export type LogEvent = (typeof LOG_EVENTS)[keyof typeof LOG_EVENTS];

export type LogDetailValue = string | number | boolean;
export type LogDetail = Readonly<Record<string, LogDetailValue | undefined>>;

export interface LogFields {
  readonly room?: string;
  readonly tick?: number;
  readonly pid?: number;
  readonly detail?: LogDetail;
}

export function isLogLevel(value: string): value is LogLevel {
  return value === 'debug' || value === 'info' || value === 'warn' || value === 'error';
}

export function parseLogLevel(raw: string | undefined): LogLevel | null {
  if (raw === undefined) return null;
  const value = raw.trim().toLowerCase();
  return isLogLevel(value) ? value : null;
}

export function resolveLogLevel(
  raw: string | undefined,
  fallback: LogLevel = DEFAULT_LOG_LEVEL,
): LogLevel {
  return parseLogLevel(raw) ?? fallback;
}

export function formatLogLine(
  level: LogLevel,
  evt: LogEvent,
  fields?: LogFields,
  tsMs: number = Date.now(),
): string {
  return JSON.stringify({
    ts: tsMs,
    level,
    evt,
    room: fields?.room ?? null,
    tick: fields?.tick ?? 0,
    pid: fields?.pid ?? 0,
    detail: fields?.detail ?? {},
  });
}

export interface LoggerOptions {
  readonly level?: string;
  readonly write?: (line: string) => void;
  readonly now?: () => number;
}

export interface Logger {
  readonly level: LogLevel;
  enabled(level: LogLevel): boolean;
  log(level: LogLevel, evt: LogEvent, fields?: LogFields): void;
  debug(evt: LogEvent, fields?: LogFields): void;
  info(evt: LogEvent, fields?: LogFields): void;
  warn(evt: LogEvent, fields?: LogFields): void;
  error(evt: LogEvent, fields?: LogFields): void;
}

export type LogSink = Logger | ((line: string) => void);

function writeStdoutLine(line: string): void {
  process.stdout.write(line + String.fromCharCode(10));
}

export function createLogger(options: LoggerOptions = {}): Logger {
  const level = resolveLogLevel(options.level);
  const threshold = LOG_LEVEL[level];
  const write = options.write ?? writeStdoutLine;
  const now = options.now ?? ((): number => Date.now());
  const emitLine = (lineLevel: LogLevel, evt: LogEvent, fields?: LogFields): void => {
    if (LOG_LEVEL[lineLevel] < threshold) return;
    write(formatLogLine(lineLevel, evt, fields, now()));
  };
  return {
    level,
    enabled(candidate: LogLevel): boolean {
      return LOG_LEVEL[candidate] >= threshold;
    },
    log: emitLine,
    debug(evt: LogEvent, fields?: LogFields): void {
      emitLine('debug', evt, fields);
    },
    info(evt: LogEvent, fields?: LogFields): void {
      emitLine('info', evt, fields);
    },
    warn(evt: LogEvent, fields?: LogFields): void {
      emitLine('warn', evt, fields);
    },
    error(evt: LogEvent, fields?: LogFields): void {
      emitLine('error', evt, fields);
    },
  };
}

export function createStdoutLogger(env: NodeJS.ProcessEnv = process.env): Logger {
  const raw = env[LOG_LEVEL_ENV];
  return createLogger(raw === undefined ? {} : { level: raw });
}

export function isLogger(sink: LogSink): sink is Logger {
  return typeof sink !== 'function';
}

export function emit(
  sink: LogSink | undefined,
  level: LogLevel,
  evt: LogEvent,
  fields?: LogFields,
): void {
  if (sink === undefined) return;
  if (typeof sink === 'function') {
    sink(formatLogLine(level, evt, fields));
    return;
  }
  sink.log(level, evt, fields);
}

export const SILENT_LOGGER: Logger = createLogger({ write: (): void => undefined });
