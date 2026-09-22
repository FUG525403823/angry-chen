import { mkdtemp } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { pathToFileURL } from 'node:url';

import { describe, expect, it } from 'vitest';

import { isLoopbackHost, joinUrlsFor, resolveEnvFileCandidates } from './main.ts';

describe('joinUrlsFor', () => {
  it('只监听回环时给回环地址——列局域网 IP 是误导', () => {
    expect(joinUrlsFor('127.0.0.1', 8787, ['192.168.1.5'])).toEqual(['http://127.0.0.1:8787']);
    expect(joinUrlsFor('localhost', 9000, [])).toEqual(['http://127.0.0.1:9000']);
  });

  it('监听 0.0.0.0 时把每个非内网 IPv4 都列成可分享地址', () => {
    expect(joinUrlsFor('0.0.0.0', 8787, ['192.168.1.5', '10.0.0.7'])).toEqual([
      'http://192.168.1.5:8787',
      'http://10.0.0.7:8787',
    ]);
  });

  it('isLoopbackHost 只认回环别名', () => {
    expect(isLoopbackHost('127.0.0.1')).toBe(true);
    expect(isLoopbackHost('::1')).toBe(true);
    expect(isLoopbackHost('0.0.0.0')).toBe(false);
  });
});

describe('resolveEnvFileCandidates', () => {
  it('cwd 优先，其次是仓库根（`pnpm --filter` 的 cwd 是包目录）', async () => {
    const root = await mkdtemp(join(tmpdir(), 'ac-env-'));
    const cwd = join(root, 'packages', 'server');
    const moduleUrl = pathToFileURL(join(cwd, 'src', 'main.ts')).href;
    expect(resolveEnvFileCandidates(cwd, moduleUrl)).toEqual([
      resolve(cwd, '.env'),
      resolve(root, '.env'),
    ]);
  });
});
