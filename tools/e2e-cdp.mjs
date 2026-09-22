#!/usr/bin/env node
// 真人试玩缺陷的浏览器端验证 + 卡顿测量工具（Edge + CDP，零新依赖：node 内置 + packages/server 已有的 ws）。
//
// 为什么需要它：docs/plans/P04 §7.1 与 docs/evidence/client-frame-alloc.md §4 都记着
// 「本机没有可用浏览器，帧率未量化」。真人反馈「画面巨卡」必须在真实浏览器里量出 fps、
// 帧间隔 p95、每帧工作耗时与分阶段 p95，才能判断是代码、合成路径还是软件渲染。
//
// 需要先构建客户端产物（服务器按 ADR-007 自己托管 packages/client/dist）：
//   pnpm build:client && node tools/e2e-cdp.mjs
//   node tools/e2e-cdp.mjs --headful --seconds 20 --out docs/evidence/playtest-cdp.md
//
// 退出码：全部断言通过 0；任一断言失败或流程异常 1。
import { spawn, spawnSync } from 'node:child_process';
import { existsSync } from 'node:fs';
import { createRequire } from 'node:module';
import { mkdir, mkdtemp, rm, writeFile } from 'node:fs/promises';
import { createServer } from 'node:net';
import { tmpdir } from 'node:os';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

// ws 是 packages/server 的依赖（根目录解析不到），与 tools/bots.mjs 同样的取法。
const serverRequire = createRequire(new URL('../packages/server/package.json', import.meta.url));
const wsModule = await import('ws').catch(() => serverRequire('ws'));
const WebSocket = wsModule.WebSocket ?? wsModule.default;

const ROOT = fileURLToPath(new URL('..', import.meta.url));
const EDGE_CANDIDATES = [
  process.env.EDGE_PATH,
  'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe',
  'C:/Program Files/Microsoft/Edge/Application/msedge.exe',
].filter((value) => typeof value === 'string' && value !== '');

const DEFAULT_CDP_PORT = 9333;
const DEFAULT_GAME_PORT = 8787;
const SHEEP_PROBE =
  '(() => { const out = []; window.__ac.view.forEachVisible((e) => { if (e.kind === 1) out.push([e.id, Math.round(e.pos.x * 100) / 100, Math.round(e.pos.z * 100) / 100]); }); return out; })()';

function sleep(ms) {
  return new Promise((resolve) => {
    setTimeout(resolve, ms);
  });
}

function parseArgs(argv) {
  const options = {
    headful: false,
    seconds: 12,
    cdpPort: DEFAULT_CDP_PORT,
    gamePort: Number(process.env.PORT ?? DEFAULT_GAME_PORT),
    out: 'docs/evidence/playtest-cdp.md',
    noServer: false,
    pageUrl: '',
    joinRoom: '',
    joinTimeoutMs: 90000,
    windowSize: '1280,800',
    scale: '',
    maximized: false,
    keepOpen: false,
  };
  for (let i = 0; i < argv.length; i += 1) {
    const token = argv[i];
    if (token === '--headful') options.headful = true;
    else if (token === '--keep-open') options.keepOpen = true;
    else if (token === '--seconds') options.seconds = Number(argv[++i]);
    else if (token === '--cdp-port') options.cdpPort = Number(argv[++i]);
    else if (token === '--game-port') options.gamePort = Number(argv[++i]);
    else if (token === '--no-server') options.noServer = true;
    else if (token === '--url') options.pageUrl = String(argv[++i]);
    else if (token === '--join-room') options.joinRoom = String(argv[++i]).toUpperCase();
    else if (token === '--join-timeout') options.joinTimeoutMs = Number(argv[++i]);
    else if (token === '--window-size') options.windowSize = String(argv[++i]);
    else if (token === '--scale') options.scale = String(argv[++i]);
    else if (token === '--maximized') options.maximized = true;
    else if (token === '--out') options.out = String(argv[++i]);
    else if (token === '--help' || token === '-h') {
      process.stdout.write(
        '用法: node tools/e2e-cdp.mjs [--headful] [--seconds 12] [--out docs/evidence/playtest-cdp.md]\n',
      );
      process.exit(0);
    }
  }
  return options;
}

async function portIsFree(port) {
  return await new Promise((resolve) => {
    const probe = createServer();
    probe.once('error', () => resolve(false));
    probe.once('listening', () => probe.close(() => resolve(true)));
    probe.listen(port, '127.0.0.1');
  });
}

async function waitForHttp(url, timeoutMs, label) {
  const deadline = Date.now() + timeoutMs;
  let last = 'n/a';
  while (Date.now() < deadline) {
    try {
      const response = await fetch(url);
      if (response.ok) return await response.text();
      last = 'HTTP ' + String(response.status);
    } catch (error) {
      last = String(error?.message ?? error);
    }
    await sleep(200);
  }
  throw new Error('等待 ' + label + ' 超时：' + last);
}

/** 极简 CDP 客户端：一个连接、请求-响应 + 事件分发。 */
function createCdp(wsUrl) {
  const socket = new WebSocket(wsUrl, { maxPayload: 256 * 1024 * 1024 });
  const pending = new Map();
  const handlers = new Map();
  const ready = new Promise((resolve, reject) => {
    socket.once('open', () => resolve());
    socket.once('error', (error) => reject(error));
  });
  socket.on('message', (raw) => {
    let message;
    try {
      message = JSON.parse(raw.toString());
    } catch {
      return;
    }
    if (typeof message.id === 'number') {
      const entry = pending.get(message.id);
      if (entry === undefined) return;
      pending.delete(message.id);
      if (message.error !== undefined) entry.reject(new Error(message.error.message));
      else entry.resolve(message.result);
      return;
    }
    for (const handler of handlers.get(message.method) ?? []) handler(message.params ?? {});
  });
  let nextId = 1;
  return {
    ready,
    send(method, params = {}) {
      const id = nextId;
      nextId += 1;
      return new Promise((resolve, reject) => {
        pending.set(id, { resolve, reject });
        socket.send(JSON.stringify({ id, method, params }));
      });
    },
    on(method, handler) {
      const list = handlers.get(method) ?? [];
      list.push(handler);
      handlers.set(method, list);
    },
    close() {
      try {
        socket.close();
      } catch {
        // 忽略关闭异常
      }
    },
  };
}

function edgePath() {
  for (const candidate of EDGE_CANDIDATES) if (existsSync(candidate)) return candidate;
  return undefined;
}

function gitHead() {
  const out = spawnSync('git', ['rev-parse', '--short', 'HEAD'], { cwd: ROOT, encoding: 'utf8' });
  return out.status === 0 ? out.stdout.trim() : 'unknown';
}

function median(values) {
  if (values.length === 0) return 0;
  const sorted = [...values].sort((a, b) => a - b);
  const mid = Math.floor(sorted.length / 2);
  return sorted.length % 2 === 1 ? sorted[mid] : (sorted[mid - 1] + sorted[mid]) / 2;
}

const options = parseArgs(process.argv.slice(2));
const checks = [];
const notes = [];
let cdp;
let edge;
let server;
let profileDir;

function check(name, ok, detail) {
  checks.push({ name, ok: ok === true, detail: detail === undefined ? '' : String(detail) });
  process.stdout.write(
    (ok === true ? '  [OK] ' : '  [!!] ') +
      name +
      (detail === undefined ? '' : ' — ' + String(detail)) +
      '\n',
  );
}

async function evaluate(expression) {
  const result = await cdp.send('Runtime.evaluate', { expression, returnByValue: true });
  if (result.exceptionDetails !== undefined) {
    const description =
      result.exceptionDetails.exception?.description ?? result.exceptionDetails.text ?? 'unknown';
    throw new Error('页面求值异常：' + String(description).split('\n')[0]);
  }
  return result.result.value;
}

async function waitFor(expression, label, timeoutMs) {
  const deadline = Date.now() + timeoutMs;
  let last;
  while (Date.now() < deadline) {
    try {
      last = await evaluate(expression);
      if (last) return last;
    } catch (error) {
      last = String(error?.message ?? error);
    }
    await sleep(150);
  }
  throw new Error('等待「' + label + '」超时，最后取值：' + JSON.stringify(last));
}

async function clickButtonByText(text) {
  return await evaluate(
    '(() => { const target = ' +
      JSON.stringify(text) +
      '; const buttons = [...document.querySelectorAll("button")];' +
      ' const hit = buttons.find((b) => (b.textContent || "").trim() === target);' +
      ' if (!hit) return ""; hit.click(); return (hit.textContent || "").trim(); })()',
  );
}

async function startServer(port) {
  const child = spawn(process.execPath, ['--import', 'tsx', 'src/main.ts'], {
    cwd: join(ROOT, 'packages', 'server'),
    env: { ...process.env, PORT: String(port), HOST: '127.0.0.1' },
    stdio: 'ignore',
  });
  await waitForHttp('http://127.0.0.1:' + String(port) + '/', 30000, '游戏服务器');
  return child;
}

async function launchEdge(path, port, headful, options_) {
  const dir = await mkdtemp(join(tmpdir(), 'ac-playtest-'));
  const args = [
    '--remote-debugging-port=' + String(port),
    '--user-data-dir=' + dir,
    '--no-first-run',
    '--no-default-browser-check',
    '--disable-extensions',
    '--disable-sync',
    '--disable-background-timer-throttling',
    '--disable-renderer-backgrounding',
    '--autoplay-policy=no-user-gesture-required',
    'about:blank',
  ];
  if (options_.maximized) args.unshift('--start-maximized');
  else args.unshift('--window-size=' + options_.windowSize);
  if (options_.scale !== '') args.unshift('--force-device-scale-factor=' + options_.scale);
  if (!headful) args.unshift('--headless=new');
  const child = spawn(path, args, { stdio: 'ignore', windowsHide: headful });
  const version = JSON.parse(
    await waitForHttp('http://127.0.0.1:' + String(port) + '/json/version', 30000, 'Edge DevTools'),
  );
  return { child, dir, version };
}

async function pageTargetUrl(port) {
  const deadline = Date.now() + 20000;
  while (Date.now() < deadline) {
    const list = JSON.parse(
      await waitForHttp('http://127.0.0.1:' + String(port) + '/json/list', 10000, 'Edge targets'),
    );
    const page = list.find((target) => target.type === 'page');
    if (page !== undefined) return page.webSocketDebuggerUrl;
    await sleep(200);
  }
  throw new Error('没有找到 page 目标');
}

/** 找一个真的落在 canvas 上的点击点（HUD/面板可能盖住中心）。 */
const CLICK_POINT_PROBE =
  '(() => { const canvas = window.__ac.renderer.domElement; const r = canvas.getBoundingClientRect();' +
  ' for (let gy = 1; gy <= 9; gy += 1) { for (let gx = 1; gx <= 9; gx += 1) {' +
  ' const x = Math.round(r.left + (r.width * gx) / 10); const y = Math.round(r.top + (r.height * gy) / 10);' +
  ' if (document.elementFromPoint(x, y) === canvas) return { x, y, hit: "canvas" }; } }' +
  ' const el = document.elementFromPoint(r.left + r.width / 2, r.top + r.height / 2);' +
  ' return { x: Math.round(r.left + r.width / 2), y: Math.round(r.top + r.height / 2),' +
  ' hit: el === null ? "none" : el.tagName + "." + String(el.className) }; })()';

async function main() {
  const target = edgePath();
  if (target === undefined) throw new Error('找不到 Edge：请设置 EDGE_PATH 环境变量');
  if (options.noServer !== true && !(await portIsFree(options.gamePort))) {
    throw new Error('端口 ' + String(options.gamePort) + ' 已被占用：先停掉正在跑的服务');
  }
  if (!(await portIsFree(options.cdpPort)))
    throw new Error('CDP 端口 ' + String(options.cdpPort) + ' 已被占用');
  // 默认指向服务器自己托管的产物（ADR-007 生产形态）；--url 可指向 Vite dev（5173）做对照实验。
  const gameUrl =
    options.pageUrl !== ''
      ? options.pageUrl
      : 'http://127.0.0.1:' +
        String(options.gamePort) +
        '/?debug=1&server=ws://127.0.0.1:' +
        String(options.gamePort) +
        '/ws';

  if (options.noServer !== true) {
    process.stdout.write('启动游戏服务器 :' + String(options.gamePort) + ' …\n');
    server = await startServer(options.gamePort);
  }
  process.stdout.write('启动 Edge（' + (options.headful ? '有窗口' : '无头') + '）…\n');
  const launched = await launchEdge(target, options.cdpPort, options.headful, options);
  edge = launched.child;
  profileDir = launched.dir;

  cdp = createCdp(await pageTargetUrl(options.cdpPort));
  await cdp.ready;
  const pageErrors = [];
  cdp.on('Runtime.exceptionThrown', (params) => {
    pageErrors.push(String(params.exceptionDetails?.exception?.description ?? '').split('\n')[0]);
  });
  cdp.on('Runtime.consoleAPICalled', (params) => {
    if (params.type === 'error') {
      pageErrors.push(
        (params.args ?? [])
          .map((arg) => String(arg.value ?? arg.description ?? ''))
          .join(' ')
          .slice(0, 200),
      );
    }
  });
  await cdp.send('Runtime.enable');
  await cdp.send('Page.enable');
  await cdp.send('Page.navigate', { url: gameUrl });
  try {
    await waitFor(
      'typeof window.__ac === "object" && window.__ac !== null',
      '客户端启动（?debug=1）',
      40000,
    );
  } catch (error) {
    const diagnostics = await evaluate(
      '(() => ({ title: document.title, text: (document.body ? document.body.innerText : "").slice(0, 200) }))()',
    ).catch(() => 'unavailable');
    throw new Error(
      String(error?.message ?? error) +
        ' / 现场：' +
        JSON.stringify(diagnostics) +
        ' / 页面错误：' +
        JSON.stringify(pageErrors.slice(0, 3)) +
        '（记得先 pnpm build:client）',
    );
  }

  const glInfo = await evaluate(
    '(() => { const canvas = document.createElement("canvas");' +
      ' const gl = canvas.getContext("webgl2") || canvas.getContext("webgl");' +
      ' if (!gl) return { renderer: "no-webgl", vendor: "no-webgl", version: "none" };' +
      ' const ext = gl.getExtension("WEBGL_debug_renderer_info");' +
      ' return { renderer: String(gl.getParameter(ext ? ext.UNMASKED_RENDERER_WEBGL : gl.RENDERER)),' +
      ' vendor: String(gl.getParameter(ext ? ext.UNMASKED_VENDOR_WEBGL : gl.VENDOR)),' +
      ' version: String(gl.getParameter(gl.VERSION)) }; })()',
  );
  const software = /swiftshader|llvmpipe|software|basic render/i.test(glInfo.renderer);
  process.stdout.write('GL renderer: ' + glInfo.renderer + '\n');
  const viewport = await evaluate(
    '(() => { const c = window.__ac.renderer.domElement;' +
      ' return { screen: [screen.width, screen.height], dpr: devicePixelRatio,' +
      ' canvasCss: [c.clientWidth, c.clientHeight], canvasPixels: [c.width, c.height] }; })()',
  );
  process.stdout.write('视口: ' + JSON.stringify(viewport) + '\n');
  notes.push('视口：' + JSON.stringify(viewport));

  // —— 大厅 → 开局 ——
  await waitFor('!!document.querySelector("button")', '大厅按钮', 20000);
  if (options.joinRoom !== '') {
    // 加入既有房间（由 tools/bots.mjs 起局）：只填房间码 + 加入，开局由 bot 房主触发
    // 服务器只允许在 lobby / intermission 阶段加入（packages/server/src/session.ts:342）：
    // 房间正在 playing 时会回 matchInProgress，所以要重试到落在波间空档。
    const JOIN_INPUT =
      '(() => { const input = document.querySelector("input.code-input"); if (input === null) return false;' +
      ' const setter = Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, "value").set;' +
      ' setter.call(input, ' +
      JSON.stringify(options.joinRoom) +
      '); input.dispatchEvent(new Event("input", { bubbles: true })); return true; })()';
    let inRoom = false;
    const joinDeadline = Date.now() + options.joinTimeoutMs;
    let attempts = 0;
    while (Date.now() < joinDeadline && !inRoom) {
      await evaluate(JOIN_INPUT).catch(() => {});
      await clickButtonByText('加入房间');
      attempts += 1;
      await sleep(1500);
      inRoom =
        (await evaluate(
          'window.__ac.connection.roomCode === ' + JSON.stringify(options.joinRoom),
        )) === true;
    }
    check(
      '以访客身份加入房间（波间空档重试 ' + String(attempts) + ' 次）',
      inRoom,
      inRoom ? '已进入 ' + options.joinRoom : '超时未进入',
    );
    notes.push('以访客身份加入房间 ' + options.joinRoom + '，尝试 ' + String(attempts) + ' 次');
  } else {
    const created = await clickButtonByText('创建房间');
    check('大厅可点「创建房间」', created === '创建房间', created === '' ? '未找到按钮' : created);
  }
  if (options.joinRoom === '') {
    await waitFor(
      '[...document.querySelectorAll("button")].some((b) => (b.textContent || "").trim() === "开始对局")',
      '房间视图（开始对局按钮）',
      20000,
    );
  }
  const roomCode = await evaluate('window.__ac.connection.roomCode');
  notes.push('房间码 ' + String(roomCode));
  if (options.joinRoom !== '') {
    await waitFor('window.__ac.view.getStats().snapshotsPerSec > 0', '服务器快照流', 30000);
  }
  const readyLabel = options.joinRoom === '' ? await clickButtonByText('准备') : '';
  notes.push('准备按钮：' + (readyLabel === '' ? '未找到' : readyLabel));
  // 开始按钮的 disabled 由服务器广播的 matchState 决定（allPlayersReady）：必须等它变可用再点，
  // 否则点击会被浏览器直接吞掉（disabled 按钮不触发 click）。
  const START_ENABLED_PROBE =
    '(() => { const b = [...document.querySelectorAll("button")].find((x) => (x.textContent || "").trim() === "开始对局");' +
    ' return b !== undefined && b.disabled === false; })()';
  let startEnabled = false;
  if (options.joinRoom !== '') {
    startEnabled = true; // 房主是 bot：开局由它触发
  } else {
    try {
      startEnabled =
        (await waitFor(START_ENABLED_PROBE, '开始按钮可用（服务器确认全员已准备）', 15000)) ===
        true;
    } catch {
      startEnabled = false;
    }
  }
  notes.push(
    '开始按钮可用：' +
      String(startEnabled) +
      '，提示：' +
      String(
        await evaluate(
          '(() => { const el = document.querySelector(".hint"); return el ? el.textContent : "no-hint"; })()',
        ),
      ),
  );
  const startLabel =
    options.joinRoom === '' ? await clickButtonByText('开始对局') : '（bot 房主开局）';
  check(
    '房主可点「开始对局」（服务器确认准备后按钮才可用）',
    startEnabled && (options.joinRoom !== '' || startLabel === '开始对局'),
    startLabel === '' ? '未找到按钮' : 'enabled=' + String(startEnabled),
  );
  try {
    await waitFor('window.__ac.view.getStats().snapshotsPerSec > 0', '服务器快照流', 20000);
  } catch (error) {
    const state = await evaluate(
      '(() => { const c = window.__ac.connection; const hint = document.querySelector(".lobby-message");' +
        ' return { status: c.status, roomCode: c.roomCode, tick: window.__ac.view.getAppliedTick(),' +
        ' lobbyMessage: hint === null ? "" : hint.textContent }; })()',
    ).catch(() => 'unavailable');
    throw new Error(String(error?.message ?? error) + ' / 客户端状态：' + JSON.stringify(state));
  }

  // —— 羊出现（= 已开战；阶段经 1.5s loading 进入 playing 后刷波）——
  let sheep = [];
  for (let i = 0; i < 60 && sheep.length === 0; i += 1) {
    sheep = await evaluate(SHEEP_PROBE);
    if (sheep.length > 0) break;
    await sleep(500);
  }
  check('开战后场景中出现羊', sheep.length > 0, String(sheep.length) + ' 只');
  const meshState = await evaluate(
    '(() => { const body = window.__ac.scene.getObjectByName("sheep-body-0"); const wool = window.__ac.scene.getObjectByName("sheep-wool-0");' +
      ' return { body: body ? { culled: body.frustumCulled, count: body.count } : null,' +
      ' wool: wool ? { culled: wool.frustumCulled, count: wool.count } : null }; })()',
  );
  check(
    '实例网格不做视锥剔除且已上传实例（否则背对世界原点就看不到羊）',
    meshState.body !== null && meshState.body.culled === false && meshState.body.count > 0,
    JSON.stringify(meshState),
  );
  if (sheep.length > 0) {
    await sleep(1500);
    const later = await evaluate(SHEEP_PROBE);
    const moved = later.filter((entry) => {
      const first = sheep.find((item) => item[0] === entry[0]);
      return (
        first !== undefined &&
        (Math.abs(first[1] - entry[1]) > 0.01 || Math.abs(first[2] - entry[2]) > 0.01)
      );
    }).length;
    check(
      '羊会移动（快照插值生效）',
      later.length > 0 && moved > 0,
      String(moved) + '/' + String(later.length) + ' 只移动',
    );

    // 把视角转向最近的羊：相机前向必须指向它（这一条同时证明「准星 = 命中判定」）
    const aim = await evaluate(
      '(() => { const me = window.__ac.view.getLocalPlayer(); if (!me) return null;' +
        ' let best = null; let bestD = Infinity;' +
        ' window.__ac.view.forEachVisible((e) => { if (e.kind !== 1) return;' +
        ' const dx = e.pos.x - me.pos.x; const dz = e.pos.z - me.pos.z; const d = dx * dx + dz * dz;' +
        ' if (d < bestD) { bestD = d; best = { x: e.pos.x, y: e.pos.y, z: e.pos.z }; } });' +
        ' if (best === null) return null;' +
        ' window.__ac.sampler.setYaw(Math.atan2(best.x - me.pos.x, best.z - me.pos.z));' +
        ' window.__ac.sampler.setPitch(0);' +
        ' return { target: best, distance: Math.sqrt(bestD) }; })()',
    );
    if (aim !== null) {
      await sleep(200);
      const projected = await evaluate(
        '(() => { const ac = window.__ac; const me = ac.view.getLocalPlayer();' +
          ' const camera = ac.camera; const v = camera.position.clone();' +
          ' v.set(' +
          String(aim.target.x) +
          ', ' +
          String(aim.target.y) +
          ', ' +
          String(aim.target.z) +
          ');' +
          ' v.project(camera);' +
          ' const bearing = me === undefined ? 0 : Math.atan2(' +
          String(aim.target.x) +
          ' - me.pos.x, ' +
          String(aim.target.z) +
          ' - me.pos.z);' +
          ' const s = ac.sampler; const before = s.state.yaw; s.setYaw(bearing); const applied = s.state.yaw - before;' +
          ' return { x: v.x, y: v.y, z: v.z, bearing, applied }; })()',
      );
      check(
        '把视角转向羊后羊落在准星中心（相机朝向 = 服务端射线方向）',
        Math.abs(projected.x) < 0.15 && projected.z < 1,
        '距离 ' + aim.distance.toFixed(1) + 'm，屏幕 NDC x ' + projected.x.toFixed(3),
      );
      notes.push(
        '瞄准方位角 ' +
          projected.bearing.toFixed(3) +
          '，yaw 设定增量 ' +
          projected.applied.toFixed(3),
      );
    }
  }

  // —— 指针锁定（客户端预测与开火以它为闸门）——
  await cdp.send('Page.bringToFront').catch(() => {});
  let box = await evaluate(CLICK_POINT_PROBE);
  await evaluate(
    'window.__ac.__lockError = ""; document.addEventListener("pointerlockerror", () => { window.__ac.__lockError = "pointerlockerror"; }, { once: true })',
  );
  let locked = false;
  for (let attempt = 0; attempt < 3 && !locked; attempt += 1) {
    await cdp.send('Input.dispatchMouseEvent', {
      type: 'mousePressed',
      x: box.x,
      y: box.y,
      button: 'left',
      clickCount: 1,
    });
    await cdp.send('Input.dispatchMouseEvent', {
      type: 'mouseReleased',
      x: box.x,
      y: box.y,
      button: 'left',
      clickCount: 1,
    });
    await sleep(400);
    locked = (await evaluate('document.pointerLockElement !== null')) === true;
    if (!locked) box = await evaluate(CLICK_POINT_PROBE);
  }
  const lockEnv = await evaluate(
    '(() => ({ focused: document.hasFocus(), active: navigator.userActivation ? navigator.userActivation.isActive : null, visible: document.visibilityState }))()',
  );
  const lockDetail = locked
    ? '已锁定'
    : '未锁定（点击点命中 ' +
      String(box.hit) +
      '，' +
      String(await evaluate('window.__ac.__lockError || "无 pointerlockerror"')) +
      '，文档 ' +
      JSON.stringify(lockEnv) +
      '）';
  check('指针锁定成功（无头模式可能失败，可加 --headful 复验）', locked, lockDetail);
  notes.push('指针锁定：' + lockDetail);

  if (locked) {
    // —— 鼠标左右方向（同一次求值内派发并读回，排除指针锁定时光标归位造成的漂移）——
    const mouseSign = await evaluate(
      '(() => { const s = window.__ac.sampler; const before = s.state.yaw;' +
        ' document.dispatchEvent(new MouseEvent("mousemove", { movementX: 120, movementY: 0 }));' +
        ' const after = s.state.yaw; s.setYaw(before); return { delta: after - before }; })()',
    );
    check(
      '鼠标右移（movementX > 0）→ yaw 减小（视角右转）',
      mouseSign.delta < 0,
      'Δyaw ' + mouseSign.delta.toFixed(4),
    );
    // 真实输入通道（CDP 合成事件）只作为备注：指针锁定时光标会归位，movementX 不可预期
    const yawBeforeWarp = await evaluate('window.__ac.sampler.state.yaw');
    await cdp.send('Input.dispatchMouseEvent', { type: 'mouseMoved', x: box.x + 120, y: box.y });
    await sleep(200);
    notes.push(
      'CDP 合成 mouseMoved 后的 yaw 变化：' +
        ((await evaluate('window.__ac.sampler.state.yaw')) - yawBeforeWarp).toFixed(3),
    );

    // —— W 前进方向 = 视线方向 ——
    const before = await evaluate(
      '(() => ({ yaw: window.__ac.sampler.state.yaw, x: window.__ac.camera.position.x, z: window.__ac.camera.position.z }))()',
    );
    await evaluate('window.__ac.sampler.setKey("KeyW", true)');
    await sleep(700);
    const after = await evaluate(
      '(() => ({ x: window.__ac.camera.position.x, z: window.__ac.camera.position.z }))()',
    );
    await evaluate('window.__ac.sampler.setKey("KeyW", false)');
    const dx = after.x - before.x;
    const dz = after.z - before.z;
    const length = Math.hypot(dx, dz);
    const dot =
      length < 1e-4 ? 0 : (dx * Math.sin(before.yaw) + dz * Math.cos(before.yaw)) / length;
    check(
      '按 W 朝视线方向前进（模拟前向 = (sin yaw, cos yaw)）',
      length > 0.5 && dot > 0.9,
      '位移 ' + length.toFixed(2) + 'm，方向点积 ' + dot.toFixed(3),
    );

    // —— 开火：曳光存在且起点偏离眼位 ——
    const eye = await evaluate('window.__ac.camera.position.toArray()');
    await evaluate('window.__ac.__maxTracer = 0');
    await cdp.send('Input.dispatchMouseEvent', {
      type: 'mousePressed',
      x: box.x,
      y: box.y,
      button: 'left',
      clickCount: 1,
    });
    let tracer = null;
    for (let i = 0; i < 25 && tracer === null; i += 1) {
      await sleep(50);
      tracer = await evaluate(
        '(() => { const t = window.__ac.scene.getObjectByName("tracers"); if (!t || t.count === 0) return null;' +
          ' const m = t.instanceMatrix.array; return { count: t.count, m: [m[12], m[13], m[14], m[8], m[9], m[10]] }; })()',
      );
    }
    await cdp.send('Input.dispatchMouseEvent', {
      type: 'mouseReleased',
      x: box.x,
      y: box.y,
      button: 'left',
      clickCount: 1,
    });
    check(
      '开火时存在曳光实例',
      tracer !== null,
      tracer === null ? '采样窗口内未见曳光' : 'count ' + String(tracer.count),
    );
    if (tracer !== null) {
      // 曳光矩阵被放在线段中点、Z 轴缩放为线段长度：起点 = 中点 − 方向 × 半长
      const [mx, my, mz, ax, ay, az] = tracer.m;
      const length = Math.hypot(ax, ay, az);
      const start = [mx - ax / 2, my - ay / 2, mz - az / 2];
      const fromEye = Math.hypot(start[0] - eye[0], start[1] - eye[1], start[2] - eye[2]);
      check(
        '曳光起点在枪口而非眼位（旧实现从眼位出发 ⇒ 与视线共线、正面只剩一个点）',
        fromEye > 0.2 && fromEye < 2,
        '起点距眼位 ' + fromEye.toFixed(3) + 'm',
      );
      check('曳光是一段真实线段', length > 1, '长度 ' + length.toFixed(2) + 'm');
    }

    // —— 真命中：准星对准最近的羊并持续开火，服务端 HP 应下降 ——
    // 旧实现 yaw 差 180°（相机背对权威方向），玩家"打不到羊"；这里用服务端复制的 hpRatio 做判据。
    const AIM_AT_SHEEP =
      '(() => { const ac = window.__ac; const me = ac.view.getLocalPlayer(); if (me === undefined) return null;' +
      ' let best = null; let bestD = Infinity;' +
      ' ac.view.forEachVisible((e) => { if (e.kind !== 1) return;' +
      ' const d = Math.hypot(e.pos.x - me.pos.x, e.pos.z - me.pos.z);' +
      ' if (d < bestD) { bestD = d; best = { id: e.id, x: e.pos.x, y: e.pos.y, z: e.pos.z, hp: e.hpRatio }; } });' +
      ' if (best === null) return null;' +
      ' const eye = ac.camera.position;' +
      ' const flat = Math.max(0.001, Math.hypot(best.x - eye.x, best.z - eye.z));' +
      ' ac.sampler.setYaw(Math.atan2(best.x - me.pos.x, best.z - me.pos.z));' +
      ' ac.sampler.setPitch(Math.atan2(best.y - eye.y, flat));' +
      ' return { id: best.id, hp: best.hp }; })()';
    const firstAim = await evaluate(AIM_AT_SHEEP);
    if (firstAim === null) {
      check('开火命中最近的羊（服务端 HP 下降）', false, '场上没有羊可打');
    } else {
      await cdp.send('Input.dispatchMouseEvent', {
        type: 'mousePressed',
        x: box.x,
        y: box.y,
        button: 'left',
        clickCount: 1,
      });
      let hp = firstAim.hp;
      let hits = 0;
      for (let i = 0; i < 50 && hp >= firstAim.hp; i += 1) {
        await sleep(80);
        await evaluate(AIM_AT_SHEEP);
        const now = await evaluate(
          '(() => { let found = null;' +
            ' window.__ac.view.forEachVisible((e) => { if (e.id === ' +
            String(firstAim.id) +
            ') found = e.hpRatio; });' +
            ' return found; })()',
        );
        if (now === null) {
          hits = 1; // 目标已被消灭（从可见集合中消失）
          break;
        }
        if (now < firstAim.hp) hits += 1;
        hp = now;
      }
      await cdp.send('Input.dispatchMouseEvent', {
        type: 'mouseReleased',
        x: box.x,
        y: box.y,
        button: 'left',
        clickCount: 1,
      });
      check(
        '开火命中最近的羊（服务端 HP 下降，旧实现 yaw 差 180° ⇒ 永远打不中）',
        hits > 0,
        '目标 ' +
          String(firstAim.id) +
          ' hpRatio ' +
          firstAim.hp.toFixed(3) +
          ' → ' +
          (hp === null ? '已消灭' : hp.toFixed(3)),
      );
    }
  }

  if (!locked) {
    // 无头/自动化环境下 pointer lock 常被浏览器拒绝，此时用「喂给本地预测器一条 W 命令」验证
    // 前进方向（与真人按 W 走的是同一条 stepLocalPlayer 代码，只绕过了 pointer.locked 闸门）。
    const drift = await evaluate(
      '(() => { const ac = window.__ac; const me = ac.view.getLocalPlayer(); if (!me) return null;' +
        ' const yaw = ac.sampler.state.yaw;' +
        ' const before = { x: ac.camera.position.x, z: ac.camera.position.z };' +
        ' const cmd = { seq: 1, tick: ac.view.getAppliedTick(), moveX: 1, moveY: 0, yaw, pitch: 0, buttons: 0, switchTo: 0 };' +
        ' for (let i = 0; i < 8; i += 1) ac.predictor.advance(125, cmd);' +
        ' ac.predictor.renderPosition(ac.scene.position);' +
        ' const p = ac.predictor.renderPosition(ac.scene.position);' +
        ' const dx = p.x - before.x; const dz = p.z - before.z; const len = Math.hypot(dx, dz);' +
        ' return { dx, dz, len, dot: len < 1e-6 ? 0 : (dx * Math.sin(yaw) + dz * Math.cos(yaw)) / len, yaw }; })()',
    );
    if (drift !== null) {
      check(
        '本地预测：moveX=1（W）沿视线方向前进',
        drift.len > 0.5 && drift.dot > 0.9,
        '位移 ' + drift.len.toFixed(2) + 'm，方向点积 ' + drift.dot.toFixed(3),
      );
    }
  }

  // —— 帧率 / 分阶段耗时采样（战斗进行中）——
  const samples = [];
  const stageSamples = [];
  const sampleCount = Math.max(4, Math.round(options.seconds * 2));
  for (let i = 0; i < sampleCount; i += 1) {
    samples.push(
      await evaluate(
        '(() => { const r = window.__ac.renderer; const s = r.getStats(); const rs = r.getRenderStats();' +
          ' return { fps: s.fps, interval: s.p95IntervalMs, work: s.p95WorkMs, draws: rs.drawCalls, tris: rs.triangles }; })()',
      ),
    );
    if (i % 4 === 3) stageSamples.push(await evaluate('window.__ac.profiler.summary()'));
    await sleep(500);
  }
  const perf = {
    fpsMedian: median(samples.map((s) => s.fps)),
    fpsMin: Math.min(...samples.map((s) => s.fps)),
    intervalMedian: median(samples.map((s) => s.interval)),
    intervalMax: Math.max(...samples.map((s) => s.interval)),
    workMedian: median(samples.map((s) => s.work)),
    workMax: Math.max(...samples.map((s) => s.work)),
    drawsMedian: median(samples.map((s) => s.draws)),
    trisMedian: median(samples.map((s) => s.tris)),
  };
  const stages = stageSamples[stageSamples.length - 1] ?? '';
  notes.push('分阶段 p95（ms）: ' + stages);
  notes.push(
    'HUD 文本：' +
      String(
        await evaluate(
          '(document.body ? document.body.innerText : "").replace(/\s+/g, " ").slice(0, 160)',
        ),
      ),
  );

  let screenshotPath = '';
  try {
    const shot = await cdp.send('Page.captureScreenshot', { format: 'png' });
    screenshotPath = join(tmpdir(), 'ac-playtest-' + String(Date.now()) + '.png');
    await writeFile(screenshotPath, Buffer.from(shot.data, 'base64'));
  } catch (error) {
    notes.push('截图失败：' + String(error?.message ?? error));
  }

  check(
    '页面无 JS 异常与 console.error',
    pageErrors.length === 0,
    pageErrors.slice(0, 3).join(' | '),
  );
  check(
    '帧率结论可判定（拿到 fps 与帧间隔）',
    perf.fpsMedian > 0 && perf.intervalMedian > 0,
    'fps 中位 ' +
      perf.fpsMedian.toFixed(1) +
      '，帧间隔 p95 中位 ' +
      perf.intervalMedian.toFixed(1) +
      'ms',
  );
  if (software)
    notes.push(
      'GL 是软件渲染（' + glInfo.renderer + '）：帧率不代表真机，请在 Edge 开启硬件加速后复测',
    );

  const passed = checks.every((entry) => entry.ok);
  process.stdout.write(
    '\n' +
      JSON.stringify({ perf, stages, softwareGl: software, gl: glInfo.renderer }, null, 1) +
      '\n',
  );
  if (screenshotPath !== '') process.stdout.write('截图（临时目录）：' + screenshotPath + '\n');

  const lines = [
    '# 真人试玩缺陷的浏览器端验证（Edge + CDP）',
    '',
    '- 时间：' + new Date().toISOString(),
    '- 提交：' + gitHead(),
    '- 浏览器：' +
      String(launched.version['Browser'] ?? 'unknown') +
      (options.headful ? '（有窗口）' : '（无头）'),
    '- GL renderer：' + glInfo.renderer + (software ? ' ⚠ 软件渲染' : ''),
    '- 页面：' + gameUrl,
    '',
    '## 断言',
    '',
  ];
  for (const entry of checks) {
    lines.push(
      '- ' +
        (entry.ok ? '✅' : '❌') +
        ' ' +
        entry.name +
        (entry.detail === '' ? '' : ' — ' + entry.detail),
    );
  }
  lines.push(
    '',
    '## 帧率（战斗中采样）',
    '',
    '| 指标 | 中位 | 最大 |',
    '| --- | --- | --- |',
    '| fps | ' + perf.fpsMedian.toFixed(1) + ' | 最低 ' + perf.fpsMin.toFixed(1) + ' |',
    '| 帧间隔 p95 (ms) | ' +
      perf.intervalMedian.toFixed(1) +
      ' | ' +
      perf.intervalMax.toFixed(1) +
      ' |',
    '| 每帧工作 p95 (ms) | ' + perf.workMedian.toFixed(1) + ' | ' + perf.workMax.toFixed(1) + ' |',
    '| draw calls | ' + perf.drawsMedian.toFixed(0) + ' | — |',
    '| 三角形 | ' + perf.trisMedian.toFixed(0) + ' | — |',
    '',
    '分阶段 p95（ms）：' + stages,
    '',
    '## 备注',
    '',
  );
  for (const note of notes) lines.push('- ' + note);
  if (pageErrors.length > 0) lines.push('- 页面错误：' + pageErrors.slice(0, 5).join(' | '));
  if (screenshotPath !== '') lines.push('- 截图（系统临时目录，不随仓库提交）：' + screenshotPath);
  lines.push('');
  await mkdir(dirname(join(ROOT, options.out)), { recursive: true });
  await writeFile(join(ROOT, options.out), lines.join('\n'), 'utf8');
  process.stdout.write('报告：' + options.out + '\n');
  process.stdout.write(passed ? '全部断言通过\n' : '有断言失败\n');
  return passed ? 0 : 1;
}

let exitCode = 1;
try {
  exitCode = await main();
} catch (error) {
  process.stderr.write('E2E 失败：' + String(error?.message ?? error) + '\n');
  exitCode = 1;
} finally {
  if (cdp !== undefined && options.keepOpen !== true) cdp.close();
  if (server !== undefined) server.kill();
  if (edge !== undefined && options.keepOpen !== true) {
    spawnSync('taskkill', ['/PID', String(edge.pid), '/T', '/F'], { stdio: 'ignore' });
  }
  if (profileDir !== undefined && options.keepOpen !== true) {
    await rm(profileDir, { recursive: true, force: true, maxRetries: 3 }).catch(() => {});
  }
}
process.exit(exitCode);
