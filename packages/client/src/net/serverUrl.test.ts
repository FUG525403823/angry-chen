import { describe, expect, it } from 'vitest';

import {
  DEFAULT_SERVER_URL,
  normalizeServerUrl,
  resolveServerUrl,
  serverUrlFromOrigin,
} from './serverUrl.ts';

function resolve(overrides: Partial<Parameters<typeof resolveServerUrl>[0]> = {}): string {
  return resolveServerUrl({
    search: '',
    protocol: 'http:',
    host: '192.168.1.5:8787',
    isDev: false,
    fallback: DEFAULT_SERVER_URL,
    ...overrides,
  });
}

describe('resolveServerUrl', () => {
  it('Vite dev 保持连本机 8787（pnpm dev 的双进程形态不变）', () => {
    expect(resolve({ isDev: true, host: 'localhost:5173' })).toBe('ws://127.0.0.1:8787');
  });

  it('生产构建与页面同源：http→ws、https→wss，路径取 /ws', () => {
    expect(resolve()).toBe('ws://192.168.1.5:8787/ws');
    expect(resolve({ protocol: 'https:', host: 'game.example.com' })).toBe(
      'wss://game.example.com/ws',
    );
  });

  it('?server= 覆盖一切（含 http/https 归一化与结尾斜杠）', () => {
    expect(resolve({ search: '?server=wss://game.example.com/ws', isDev: true })).toBe(
      'wss://game.example.com/ws',
    );
    expect(resolve({ search: '?server=http://10.0.0.7:9000' })).toBe('ws://10.0.0.7:9000');
    expect(resolve({ search: '?server=wss://game.example.com/ws/' })).toBe(
      'wss://game.example.com/ws',
    );
    expect(resolve({ search: '?room=ABCD&server=ws://127.0.0.1:9001' })).toBe(
      'ws://127.0.0.1:9001',
    );
  });

  it('非法或空的 ?server= 落回默认规则', () => {
    expect(resolve({ search: '?server=' })).toBe('ws://192.168.1.5:8787/ws');
    expect(resolve({ search: '?server=ftp://x' })).toBe('ws://192.168.1.5:8787/ws');
    expect(resolve({ search: '?server=192.168.1.5:8787' })).toBe('ws://192.168.1.5:8787/ws');
  });

  it('file:// 没有 host，落到兜底地址', () => {
    expect(resolve({ protocol: 'file:', host: '' })).toBe(DEFAULT_SERVER_URL);
  });
});

describe('normalizeServerUrl', () => {
  it('只认 ws/wss/http/https，去空白与结尾斜杠', () => {
    expect(normalizeServerUrl('  ws://host:1  ')).toBe('ws://host:1');
    expect(normalizeServerUrl('https://host/ws/')).toBe('wss://host/ws');
    expect(normalizeServerUrl('host:1')).toBeNull();
    expect(normalizeServerUrl('://host')).toBeNull();
    expect(normalizeServerUrl('ws://')).toBeNull();
  });
});

describe('serverUrlFromOrigin', () => {
  it('没有 host 时返回 null（调用方回落兜底）', () => {
    expect(serverUrlFromOrigin('file:', '')).toBeNull();
    expect(serverUrlFromOrigin('http:', '   ')).toBeNull();
  });
});
