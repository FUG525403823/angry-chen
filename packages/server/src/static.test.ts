import { mkdir, mkdtemp, rm, writeFile } from 'node:fs/promises';
import { createServer, request as httpRequest } from 'node:http';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { pathToFileURL } from 'node:url';
import { gunzipSync } from 'node:zlib';

import { afterEach, describe, expect, it } from 'vitest';

import {
  CLIENT_DIST_OFF,
  contentTypeFor,
  createStaticServer,
  resolveClientDist,
  resolveStaticPath,
} from './static.ts';

const tempDirs: string[] = [];
const closers: (() => Promise<void>)[] = [];
const APP_JS = 'const tick = 1;\n'.repeat(400);

afterEach(async () => {
  for (const close of closers.splice(0)) await close();
  for (const dir of tempDirs.splice(0)) await rm(dir, { recursive: true, force: true });
});

async function makeTempDir(prefix: string): Promise<string> {
  const dir = await mkdtemp(join(tmpdir(), prefix));
  tempDirs.push(dir);
  return dir;
}

async function makeDist(): Promise<string> {
  const dir = await makeTempDir('ac-static-');
  await mkdir(join(dir, 'assets'), { recursive: true });
  await writeFile(join(dir, 'index.html'), '<!doctype html><title>ac</title>');
  await writeFile(join(dir, 'assets', 'app.js'), APP_JS);
  return dir;
}

interface RawResponse {
  readonly status: number;
  readonly headers: Record<string, string | string[] | undefined>;
  readonly body: Buffer;
}

function rawRequest(
  url: string,
  options: { method?: string; headers?: Record<string, string> } = {},
): Promise<RawResponse> {
  return new Promise<RawResponse>((resolvePromise, rejectPromise) => {
    const req = httpRequest(
      url,
      { method: options.method ?? 'GET', headers: options.headers ?? {} },
      (res) => {
        const chunks: Buffer[] = [];
        res.on('data', (chunk: Buffer) => chunks.push(chunk));
        res.on('end', () => {
          resolvePromise({
            status: res.statusCode ?? 0,
            headers: res.headers,
            body: Buffer.concat(chunks),
          });
        });
      },
    );
    req.on('error', rejectPromise);
    req.end();
  });
}

async function startStatic(root: string): Promise<string> {
  const staticServer = createStaticServer({ root });
  const server = createServer((req, res) => {
    if (staticServer.handle(req, res)) return;
    res.writeHead(404, { 'content-type': 'application/json; charset=utf-8' });
    res.end(JSON.stringify({ error: 'not-found' }));
  });
  await new Promise<void>((resolveListen) => server.listen(0, '127.0.0.1', () => resolveListen()));
  closers.push(
    () =>
      new Promise<void>((resolveClose) => {
        server.close(() => resolveClose());
      }),
  );
  const address = server.address();
  const port = address !== null && typeof address !== 'string' ? address.port : 0;
  return 'http://127.0.0.1:' + String(port);
}

describe('resolveStaticPath', () => {
  const root = join('C:', 'repo', 'packages', 'client', 'dist');

  it('把 URL 路径映射到 root 内，并给目录请求补 index.html', () => {
    expect(resolveStaticPath(root, '/')).toBe(join(root, 'index.html'));
    expect(resolveStaticPath(root, '/assets/app.js')).toBe(join(root, 'assets', 'app.js'));
    expect(resolveStaticPath(root, '//assets///app.js')).toBe(join(root, 'assets', 'app.js'));
    expect(resolveStaticPath(root, '/deep/')).toBe(join(root, 'deep', 'index.html'));
  });

  it('拒绝穿越、反斜杠、NUL 与非法百分号编码', () => {
    expect(resolveStaticPath(root, '/../package.json')).toBeNull();
    expect(resolveStaticPath(root, '/%2e%2e%2fpackage.json')).toBeNull();
    expect(resolveStaticPath(root, '/assets/..%2f..%2fpackage.json')).toBeNull();
    expect(resolveStaticPath(root, '/assets\\app.js')).toBeNull();
    expect(resolveStaticPath(root, '/%00.js')).toBeNull();
    expect(resolveStaticPath(root, '/%E0%A4%A')).toBeNull();
  });
});

describe('contentTypeFor', () => {
  it('覆盖构建产物的主要类型，未知扩展名给 octet-stream', () => {
    expect(contentTypeFor('index.html')).toContain('text/html');
    expect(contentTypeFor('index-abc.js')).toContain('text/javascript');
    expect(contentTypeFor('index-abc.css')).toContain('text/css');
    expect(contentTypeFor('three.wasm')).toBe('application/wasm');
    expect(contentTypeFor('weird.xyz')).toBe('application/octet-stream');
  });
});

describe('resolveClientDist', () => {
  it('未设置时按模块位置解析 <repo>/packages/client/dist', async () => {
    const root = await makeTempDir('ac-dist-');
    const distDir = join(root, 'packages', 'client', 'dist');
    await mkdir(distDir, { recursive: true });
    const moduleUrl = pathToFileURL(join(root, 'packages', 'server', 'src', 'main.ts')).href;
    const resolution = resolveClientDist(undefined, moduleUrl);
    expect(resolution.root).toBe(distDir);
    expect(resolution.explicit).toBe(false);
  });

  it('显式目录生效；off/none 关闭；显式但不存在时 root 为 null 且保留原文', async () => {
    const root = await makeTempDir('ac-dist-');
    const distDir = join(root, 'dist');
    await mkdir(distDir, { recursive: true });
    const moduleUrl = pathToFileURL(join(root, 'packages', 'server', 'src', 'main.ts')).href;
    expect(resolveClientDist(distDir, moduleUrl).root).toBe(distDir);
    expect(resolveClientDist(CLIENT_DIST_OFF, moduleUrl)).toEqual({
      root: null,
      explicit: true,
      requested: 'off',
    });
    expect(resolveClientDist('none', moduleUrl).root).toBeNull();
    const missing = resolveClientDist(join(root, 'nope'), moduleUrl);
    expect(missing.root).toBeNull();
    expect(missing.explicit).toBe(true);
    expect(missing.requested).toBe(join(root, 'nope'));
  });

  it('默认目录不存在时静默关闭（不报 explicit）', async () => {
    const root = await makeTempDir('ac-dist-');
    const moduleUrl = pathToFileURL(join(root, 'packages', 'server', 'src', 'main.ts')).href;
    const resolution = resolveClientDist(undefined, moduleUrl);
    expect(resolution.root).toBeNull();
    expect(resolution.explicit).toBe(false);
  });
});

describe('createStaticServer', () => {
  it('GET / 返回 index.html（no-cache），/assets/* 一年 immutable', async () => {
    const base = await startStatic(await makeDist());
    const index = await rawRequest(base + '/');
    expect(index.status).toBe(200);
    expect(String(index.headers['content-type'])).toContain('text/html');
    expect(index.headers['cache-control']).toBe('no-cache');
    expect(index.body.toString()).toContain('<title>ac</title>');

    const asset = await rawRequest(base + '/assets/app.js');
    expect(asset.status).toBe(200);
    expect(String(asset.headers['content-type'])).toContain('text/javascript');
    expect(asset.headers['cache-control']).toBe('public, max-age=31536000, immutable');
    expect(asset.headers['content-length']).toBe(String(Buffer.byteLength(APP_JS)));
    expect(asset.body.toString()).toBe(APP_JS);
  });

  it('客户端接受 gzip 时流式压缩并声明 vary', async () => {
    const base = await startStatic(await makeDist());
    const asset = await rawRequest(base + '/assets/app.js', {
      headers: { 'accept-encoding': 'gzip' },
    });
    expect(asset.status).toBe(200);
    expect(asset.headers['content-encoding']).toBe('gzip');
    expect(asset.headers['vary']).toBe('accept-encoding');
    expect(asset.headers['content-length']).toBeUndefined();
    expect(gunzipSync(asset.body).toString()).toBe(APP_JS);
  });

  it('HEAD 只回头部', async () => {
    const base = await startStatic(await makeDist());
    const head = await rawRequest(base + '/', { method: 'HEAD' });
    expect(head.status).toBe(200);
    expect(String(head.headers['content-type'])).toContain('text/html');
    expect(head.body.length).toBe(0);
  });

  it('SPA 兜底只吃导航请求：text/html 且末段无扩展名', async () => {
    const base = await startStatic(await makeDist());
    const navigation = await rawRequest(base + '/room/ABCD', {
      headers: { accept: 'text/html,application/xhtml+xml' },
    });
    expect(navigation.status).toBe(200);
    expect(navigation.body.toString()).toContain('<title>ac</title>');

    const data = await rawRequest(base + '/room/ABCD', { headers: { accept: 'application/json' } });
    expect(data.status).toBe(404);
    expect(JSON.parse(data.body.toString())).toEqual({ error: 'not-found' });

    const missingAsset = await rawRequest(base + '/assets/missing.js', {
      headers: { accept: 'text/html' },
    });
    expect(missingAsset.status).toBe(404);
  });

  it('非 GET/HEAD 与穿越路径都交回调用方（404 JSON）', async () => {
    const base = await startStatic(await makeDist());
    const post = await rawRequest(base + '/', { method: 'POST' });
    expect(post.status).toBe(404);

    const traversal = await rawRequest(base + '/%2e%2e%2f%2e%2e%2fpackage.json');
    expect(traversal.status).toBe(404);
    expect(JSON.parse(traversal.body.toString())).toEqual({ error: 'not-found' });
  });
});
