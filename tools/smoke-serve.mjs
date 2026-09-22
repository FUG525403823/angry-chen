import { spawn } from 'node:child_process';
import { existsSync, mkdtempSync, readdirSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

// ADR-007 单进程联机冒烟：真起一个服务端进程，验证「一个地址即可玩」。
// 跨平台（Windows / Linux / macOS）：CI 在 ubuntu-latest 上跑它，本机在 Windows 上跑它。
// 前置：pnpm build:client（缺 dist 会直接失败并提示）。用法：pnpm smoke
const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const SERVER_DIR = join(ROOT, 'packages', 'server');
const DIST_DIR = join(ROOT, 'packages', 'client', 'dist');
const READY_TIMEOUT_MS = 30_000;
const WS_TIMEOUT_MS = 5_000;

function fail(message) {
  console.error('=== smoke-serve ===');
  console.error('FAIL: ' + message);
  process.exit(1);
}

if (!existsSync(join(DIST_DIR, 'index.html'))) {
  fail('缺少 packages/client/dist/index.html——先跑 pnpm build:client（或 pnpm serve）');
}
const assetDir = join(DIST_DIR, 'assets');
const assetName = (existsSync(assetDir) ? readdirSync(assetDir) : []).find((name) =>
  name.endsWith('.js'),
);
if (assetName === undefined) fail('packages/client/dist/assets 下没有 .js 产物');

const WebSocketImpl = globalThis.WebSocket;
if (WebSocketImpl === undefined)
  fail('需要 Node 22+ 的内置 WebSocket（当前 ' + process.version + '）');

const dataDir = mkdtempSync(join(tmpdir(), 'ac-smoke-'));
const child = spawn(process.execPath, ['--import', 'tsx', 'src/main.ts'], {
  cwd: SERVER_DIR,
  env: { ...process.env, HOST: '127.0.0.1', PORT: '0', DATA_DIR: dataDir, LOG_LEVEL: 'info' },
  stdio: ['ignore', 'pipe', 'pipe'],
});

let stderrTail = '';
child.stderr.on('data', (chunk) => {
  stderrTail = (stderrTail + String(chunk)).slice(-2000);
});

function stop() {
  if (child.exitCode === null && child.signalCode === null) child.kill();
}
process.on('exit', () => {
  stop();
  rmSync(dataDir, { recursive: true, force: true });
});

function waitForListening() {
  return new Promise((resolvePromise, rejectPromise) => {
    const timer = setTimeout(
      () =>
        rejectPromise(new Error('等待 listening 日志超时（' + String(READY_TIMEOUT_MS) + 'ms）')),
      READY_TIMEOUT_MS,
    );
    let buffer = '';
    child.stdout.on('data', (chunk) => {
      buffer += String(chunk);
      const lines = buffer.split('\n');
      buffer = lines.pop() ?? '';
      for (const line of lines) {
        if (!line.trim().startsWith('{')) continue;
        let parsed;
        try {
          parsed = JSON.parse(line);
        } catch {
          continue;
        }
        if (parsed.evt === 'listening') {
          clearTimeout(timer);
          resolvePromise(parsed);
        }
      }
    });
    child.on('exit', (code) => {
      clearTimeout(timer);
      rejectPromise(new Error('服务端提前退出（code=' + String(code) + '）\n' + stderrTail));
    });
  });
}

function checkWebSocket(url) {
  return new Promise((resolvePromise, rejectPromise) => {
    const socket = new WebSocketImpl(url);
    const timer = setTimeout(() => {
      socket.close();
      rejectPromise(new Error('等待 101 握手超时'));
    }, WS_TIMEOUT_MS);
    socket.addEventListener('open', () => {
      clearTimeout(timer);
      socket.close();
      resolvePromise('101 Switching Protocols');
    });
    socket.addEventListener('error', () => {
      clearTimeout(timer);
      rejectPromise(new Error('握手失败（Upgrade 被拒或路径不对）'));
    });
  });
}

const listening = await waitForListening().catch((error) => {
  stop();
  fail(error instanceof Error ? error.message : String(error));
});
const origin = new URL(listening.detail.url);
const base = 'http://127.0.0.1:' + origin.port;

const passed = [];
const failed = [];
async function check(label, run) {
  try {
    passed.push(label + '：' + (await run()));
  } catch (error) {
    failed.push(label + ' → ' + (error instanceof Error ? error.message : String(error)));
  }
}
function expect(condition, message) {
  if (!condition) throw new Error(message);
}

await check('GET /', async () => {
  const res = await fetch(base + '/');
  expect(res.status === 200, '状态码 ' + String(res.status));
  const type = res.headers.get('content-type') ?? '';
  expect(type.includes('text/html'), 'content-type=' + type);
  const body = await res.text();
  expect(body.includes('<'), '响应体不像 HTML');
  return '200 text/html';
});

await check('GET /assets/' + assetName, async () => {
  const res = await fetch(base + '/assets/' + assetName, {
    headers: { 'accept-encoding': 'gzip' },
  });
  expect(res.status === 200, '状态码 ' + String(res.status));
  const cache = res.headers.get('cache-control') ?? '';
  expect(cache.includes('immutable'), 'cache-control=' + cache);
  const encoding = res.headers.get('content-encoding') ?? '(identity)';
  await res.arrayBuffer();
  return '200 cache-control=' + cache + ' content-encoding=' + encoding;
});

await check('GET /health', async () => {
  const res = await fetch(base + '/health');
  const body = await res.json();
  expect(res.status === 200, '状态码 ' + String(res.status));
  expect(body.status === 'ok', 'status=' + String(body.status));
  return '200 status=ok protocolVersion=' + String(body.protocolVersion);
});

await check('GET /api/leaderboard', async () => {
  const res = await fetch(base + '/api/leaderboard');
  const body = await res.json();
  expect(res.status === 200 && body.ok === true, '状态码 ' + String(res.status));
  return '200 ok=true';
});

await check('路径穿越 /%2e%2e%2f%2e%2e%2fpackage.json', async () => {
  const res = await fetch(base + '/%2e%2e%2f%2e%2e%2fpackage.json');
  expect(res.status === 404, '状态码 ' + String(res.status));
  await res.arrayBuffer();
  return '404';
});

await check('导航请求 /room/ABCD 走 index.html 兜底', async () => {
  const res = await fetch(base + '/room/ABCD', { headers: { accept: 'text/html' } });
  expect(res.status === 200, '状态码 ' + String(res.status));
  await res.arrayBuffer();
  return '200';
});

await check('WebSocket /ws 握手', () => checkWebSocket('ws://127.0.0.1:' + origin.port + '/ws'));
await check('服务端日志含 clientDist 与 joinUrls', async () => {
  expect(typeof listening.detail.clientDist === 'string', 'clientDist 缺失');
  expect(Array.isArray(listening.detail.joinUrls), 'joinUrls 缺失');
  return listening.detail.clientDist;
});

stop();
for (const line of passed) console.log('  · ' + line);
if (failed.length > 0) {
  console.error('=== smoke-serve ===');
  for (const line of failed) console.error('FAIL: ' + line);
  process.exit(1);
}
console.log('=== smoke-serve ===');
console.log(
  'OK：单进程联机冒烟全部通过（' +
    process.platform +
    ' ' +
    process.arch +
    '，Node ' +
    process.version +
    '）',
);
