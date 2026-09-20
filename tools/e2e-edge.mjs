#!/usr/bin/env node
// P10 §3/§4：可选 Edge E2E。用 puppeteer-core 驱动本机 Edge 打开客户端、进房、开火、截图。
// 缺失 puppeteer-core 或浏览器时打印明确的 Skipped 并以 0 退出；不接入 pnpm check。
// 注意：截图默认写到系统临时目录——仓库禁止提交图片素材（check-docs 的素材扫描会拦）。
import { existsSync, mkdirSync } from 'node:fs';
import { createRequire } from 'node:module';
import { tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const rootDir = resolve(dirname(fileURLToPath(import.meta.url)), '..');

function parseArgs(argv) {
  const args = new Map();
  for (let i = 0; i < argv.length; i += 1) {
    const token = argv[i] ?? '';
    if (!token.startsWith('--')) continue;
    const key = token.slice(2);
    const next = argv[i + 1];
    if (next !== undefined && !next.startsWith('--')) {
      args.set(key, next);
      i += 1;
    } else {
      args.set(key, 'true');
    }
  }
  return args;
}

const args = parseArgs(process.argv.slice(2));
const url = args.get('url') ?? 'http://localhost:5173';
const headful = args.has('headful');
const timeoutMs = Math.max(3000, Number.parseInt(args.get('timeout') ?? '20000', 10) || 20000);
const screenshot =
  args.get('screenshot') ?? join(tmpdir(), 'angry-chen-e2e-' + String(Date.now()) + '.png');

const BROWSER_CANDIDATES = [
  'C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe',
  'C:\\Program Files\\Microsoft\\Edge\\Application\\msedge.exe',
  'C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe',
  'C:\\Program Files (x86)\\Google\\Chrome\\Application\\chrome.exe',
];

function findBrowser() {
  for (const candidate of BROWSER_CANDIDATES) {
    if (existsSync(candidate)) return candidate;
  }
  return '';
}

function resolvePuppeteer() {
  const require = createRequire(new URL('../packages/client/package.json', import.meta.url));
  try {
    return require('puppeteer-core');
  } catch {
    return null;
  }
}

const puppeteer = resolvePuppeteer();
if (puppeteer === null) {
  process.stdout.write(
    'Skipped: puppeteer-core 未安装（本机无网络，未新增依赖；E2E 为可选产物，不影响 pnpm check）\n',
  );
  process.exit(0);
}

const executablePath = findBrowser();
if (executablePath === '') {
  process.stdout.write(
    'Skipped: 未找到 Chromium 系浏览器（探测路径：' + BROWSER_CANDIDATES.join('、') + '）\n',
  );
  process.exit(0);
}

process.stdout.write('e2e-edge: browser=' + executablePath + ' url=' + url + '\n');

let browser = null;
try {
  browser = await puppeteer.launch({
    executablePath,
    headless: !headful,
    args: ['--no-sandbox', '--disable-gpu', '--disable-dev-shm-usage', '--window-size=1280,720'],
  });
  const page = await browser.newPage();
  await page.setViewport({ width: 1280, height: 720 });
  const consoleErrors = [];
  page.on('pageerror', (error) => consoleErrors.push(String(error)));
  await page.goto(url, { waitUntil: 'domcontentloaded', timeout: timeoutMs });
  await page.waitForSelector('canvas', { timeout: timeoutMs });
  const canvas = await page.$('canvas');
  const box = canvas === null ? null : await canvas.boundingBox();
  if (box !== null) {
    await page.mouse.click(box.x + box.width / 2, box.y + box.height / 2);
  }
  await new Promise((resolveWait) => setTimeout(resolveWait, 2000));
  const title = await page.title();
  mkdirSync(dirname(screenshot), { recursive: true });
  await page.screenshot({ path: screenshot });
  process.stdout.write(
    'e2e-edge: ok title="' +
      title +
      '" canvas=' +
      (box === null ? 'missing' : 'visible') +
      ' screenshot=' +
      screenshot +
      ' pageErrors=' +
      String(consoleErrors.length) +
      '\n',
  );
  if (consoleErrors.length > 0)
    process.stdout.write('e2e-edge: page errors: ' + consoleErrors.join(' | ') + '\n');
  process.exit(consoleErrors.length === 0 ? 0 : 1);
} catch (error) {
  process.stdout.write('e2e-edge: FAILED ' + String(error) + '\n');
  process.exit(1);
} finally {
  if (browser !== null) await browser.close();
}
