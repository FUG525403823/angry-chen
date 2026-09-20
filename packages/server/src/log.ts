export type LogLevel = 'debug' | 'info' | 'warn' | 'error';

export type LogFields = Record<string, string | number | boolean | undefined>;

export function formatLogLine(level: LogLevel, evt: string, fields?: LogFields): string {
  const parts: string[] = [
    '"ts":' + JSON.stringify(Date.now()),
    '"level":' + JSON.stringify(level),
    '"evt":' + JSON.stringify(evt),
  ];
  if (fields !== undefined) {
    for (const key of Object.keys(fields)) {
      const value = fields[key];
      if (value === undefined) continue;
      parts.push(JSON.stringify(key) + ':' + JSON.stringify(value));
    }
  }
  return '{' + parts.join(',') + '}';
}
