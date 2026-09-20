import { describe, expect, it } from 'vitest';

import {
  DEFAULT_LOG_LEVEL,
  LOG_EVENTS,
  LOG_LEVEL_ENV,
  createLogger,
  emit,
  formatLogLine,
  isLogger,
  parseLogLevel,
  resolveLogLevel,
  SILENT_LOGGER,
} from './log.ts';

const newline = String.fromCharCode(10);

describe('结构化日志', () => {
  it('解析、校验与回退 LOG_LEVEL', () => {
    expect(parseLogLevel('debug')).toBe('debug');
    expect(parseLogLevel(' WARN ')).toBe('warn');
    expect(parseLogLevel('Error')).toBe('error');
    expect(parseLogLevel('trace')).toBeNull();
    expect(parseLogLevel(undefined)).toBeNull();
    expect(resolveLogLevel(undefined)).toBe(DEFAULT_LOG_LEVEL);
    expect(resolveLogLevel('bogus', 'error')).toBe('error');
    expect(resolveLogLevel('info')).toBe('info');
    expect(LOG_LEVEL_ENV).toBe('LOG_LEVEL');
  });

  it('evt 词表包含冻结的短名', () => {
    const names: string[] = Object.values(LOG_EVENTS);
    expect(names).toEqual(
      expect.arrayContaining([
        'room.create',
        'session.join',
        'wave.start',
        'wave.clear',
        'anticheat.speed',
        'grace.start',
        'grace.timeout',
        'match.end',
        'error.uncaught',
      ]),
    );
  });

  it('输出冻结字段集合的单行 JSON', () => {
    const line = formatLogLine(
      'info',
      LOG_EVENTS.sessionJoin,
      { room: 'ABCD', tick: 12, pid: 3, detail: { name: '陈sir', ok: true, n: 2 } },
      1234,
    );
    expect(line.includes(newline)).toBe(false);
    const record = JSON.parse(line) as Record<string, unknown>;
    expect(Object.keys(record)).toEqual(['ts', 'level', 'evt', 'room', 'tick', 'pid', 'detail']);
    expect(record['ts']).toBe(1234);
    expect(record['level']).toBe('info');
    expect(record['evt']).toBe('session.join');
    expect(record['room']).toBe('ABCD');
    expect(record['tick']).toBe(12);
    expect(record['pid']).toBe(3);
    expect(record['detail']).toEqual({ name: '陈sir', ok: true, n: 2 });
  });

  it('缺省字段补 null / 0 / 空对象', () => {
    const record = JSON.parse(formatLogLine('warn', LOG_EVENTS.waveStart, undefined, 7)) as Record<
      string,
      unknown
    >;
    expect(record['room']).toBeNull();
    expect(record['tick']).toBe(0);
    expect(record['pid']).toBe(0);
    expect(record['detail']).toEqual({});
  });

  it('按级别过滤且不打印低于阈值的日志', () => {
    const lines: string[] = [];
    const logger = createLogger({
      level: 'warn',
      now: () => 5,
      write: (line: string) => lines.push(line),
    });
    expect(logger.level).toBe('warn');
    expect(logger.enabled('debug')).toBe(false);
    expect(logger.enabled('warn')).toBe(true);
    expect(logger.enabled('error')).toBe(true);
    logger.debug(LOG_EVENTS.listening, {});
    logger.info(LOG_EVENTS.listening, {});
    logger.warn(LOG_EVENTS.shutdownRequested, { detail: { signal: 'SIGTERM' } });
    logger.error(LOG_EVENTS.errorUncaught, { detail: { error: 'boom' } });
    expect(lines.length).toBe(2);
    const warn = JSON.parse(lines[0] ?? '{}') as Record<string, unknown>;
    expect(warn['level']).toBe('warn');
    expect(warn['evt']).toBe('shutdownRequested');
    expect(warn['ts']).toBe(5);
  });

  it('emit 兼容 Logger 与函数两种 sink', () => {
    const lines: string[] = [];
    const sink = (line: string): void => {
      lines.push(line);
    };
    expect(isLogger(sink)).toBe(false);
    expect(isLogger(SILENT_LOGGER)).toBe(true);
    emit(sink, 'info', LOG_EVENTS.listening, { detail: { url: 'http://x' } });
    emit(undefined, 'info', LOG_EVENTS.listening);
    emit(SILENT_LOGGER, 'info', LOG_EVENTS.listening);
    expect(lines.length).toBe(1);
    const record = JSON.parse(lines[0] ?? '{}') as Record<string, unknown>;
    expect(record['evt']).toBe('listening');
    expect(record['detail']).toEqual({ url: 'http://x' });
  });
});
