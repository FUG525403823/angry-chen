#!/usr/bin/env node
// P10 §3/§4：N 个无头 bot 客户端。它们不是真客户端：复用 @ac/shared 的 codec 构造 Command、
// 用共享 sim 做本地预测与武器模型，并校验服务器权威结果（命中/击杀必须有本地开火前提）。
import { createRequire } from 'node:module';
import { mkdirSync, writeFileSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const serverRequire = createRequire(new URL('../packages/server/package.json', import.meta.url));
const wsModule = await import('ws').catch(() => serverRequire('ws'));
const WebSocket = wsModule.WebSocket ?? wsModule.default;

const {
  ARENA,
  BUTTON,
  CONFIG,
  LIMITS,
  MATCH_PHASE,
  NEW_ROOM_CODE,
  OPCODE,
  PROTOCOL_VERSION,
  SERVER_TICK_MS,
  SNAPSHOT_RECORD_BYTES,
  activeMag,
  createCommand,
  createRayHit,
  createRng,
  createSnapshotMirror,
  createVec3,
  createWeaponState,
  decodeError,
  decodeEventFrame,
  decodeMatchState,
  decodePong,
  decodeSnapshot,
  decodeWelcome,
  dequantizePosition,
  encodeCommand,
  encodeJoin,
  encodePing,
  encodeReady,
  encodeSimpleFrame,
  isReloading,
  rayVsAabb,
  readSnapshotRecord,
  rngFloat,
  rngRange,
  stepLocalPlayer,
  tryFire,
  tryStartReload,
  updateWeapon,
} = await import('../packages/shared/src/index.ts');
const { createAmmoLedger } = await import('../packages/client/src/prediction/ammoLedger.ts');

const PUMP_INTERVAL_MS = 5;
const PING_INTERVAL_MS = 500;
const MOD32 = 4294967296;
const WALL_BLOCK_MARGIN = 0.95;
const CORRECTION_EPSILON_M = 0.75;
const PLAYER_LIMIT_M =
  ARENA.halfSize - ARENA.fence.thickness / 2 - CONFIG.entity.radiusByKind.player;
const MOVE_CONFIG = Object.freeze({
  player: CONFIG.player,
  arena: ARENA,
  radius: CONFIG.entity.radiusByKind.player,
});

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
const requestedUrl = args.get('url') ?? '';
const port = Number.parseInt(args.get('port') ?? process.env.PORT ?? '8787', 10) || 8787;
const options = {
  // 未给 --url 时在本进程内起一台真实服务器（HTTP + ws，入口同 packages/server/src/main.ts），
  // 这样 `node tools/bots.mjs --players 4 --minutes 5` 可以独立跑完；给 --url 时只当压测客户端。
  url: requestedUrl !== '' ? requestedUrl : 'ws://127.0.0.1:' + String(port),
  external: requestedUrl !== '',
  port,
  players: Math.min(
    LIMITS.maxPlayersPerRoom,
    Math.max(1, Number.parseInt(args.get('players') ?? '4', 10) || 4),
  ),
  minutes: Math.max(0.05, Number.parseFloat(args.get('minutes') ?? '30') || 30),
  latencyMs: Math.max(0, Number.parseFloat(args.get('latency') ?? '80') || 0),
  jitterMs: Math.max(0, Number.parseFloat(args.get('jitter') ?? '20') || 0),
  loss: Math.min(1, Math.max(0, Number.parseFloat(args.get('loss') ?? '0.01') || 0)),
  seed: Number.parseInt(args.get('seed') ?? '20261010', 10) || 20261010,
  name: args.get('name') ?? 'bot',
  hold: args.has('hold'),
  strict: args.has('strict'),
  out: args.get('out') ?? '',
};

const httpUrl = options.url.replace(/^ws/, 'http').replace(/\/$/, '');
const rootDir = resolve(dirname(fileURLToPath(import.meta.url)), '..');

function clamp(value, min, max) {
  if (value < min) return min;
  if (value > max) return max;
  return value;
}

function percentile(values, p) {
  if (values.length === 0) return 0;
  const sorted = [...values].sort((a, b) => a - b);
  return sorted[Math.min(sorted.length - 1, Math.floor(sorted.length * p))] ?? 0;
}

function round(value, digits) {
  const factor = 10 ** digits;
  return Math.round(value * factor) / factor;
}

// 本机（Windows）会把 5ms 的 setInterval 量化到 ~15.6ms：房间循环 5ms 的 tick 抖动因此偏高，
// 这是计时器粒度而非代码问题。探测值写入报告，便于判定 §5.2-4 时区分环境与实现。
async function probeTimerGranularityMs() {
  return new Promise((resolveGranularity) => {
    const samples = [];
    let last = performance.now();
    const timer = setInterval(() => {
      const now = performance.now();
      samples.push(now - last);
      last = now;
      if (samples.length >= 100) {
        clearInterval(timer);
        samples.sort((a, b) => a - b);
        resolveGranularity(round(samples[Math.floor(samples.length / 2)] ?? 0, 2));
      }
    }, 5);
  });
}

function createRecordScratch() {
  return {
    id: 0,
    kind: 'player',
    flags: 0,
    xCm: 0,
    yCm: 0,
    zCm: 0,
    yawUnits: 0,
    pitchUnits: 0,
    hpRatioUnits: 0,
    state: 0,
  };
}

function createBot(index) {
  const weapon = createWeaponState();
  weapon.activeSlot = 1;
  return {
    index,
    name: options.name + String(index + 1),
    socket: null,
    mirror: createSnapshotMirror(),
    record: createRecordScratch(),
    welcome: null,
    roomCode: null,
    myPid: 0,
    matchState: null,
    matchStateFrame: { phase: 0, wave: 0, intermissionMs: 0, hostId: 0, players: [] },
    eventScratch: [],
    command: createCommand(),
    scratchCommand: createCommand(),
    outScratch: new Uint8Array(LIMITS.maxFrameBytes),
    predicted: {
      pos: { x: 0, y: 0, z: 0 },
      vel: { x: 0, y: 0, z: 0 },
      yaw: 0,
      pitch: 0,
    },
    weapon,
    // O07 §4 任务 9：每个 bot 一份弹药账本，度量「本地预测值 − 权威值」。
    ammoLedger: createAmmoLedger(),
    ammoDivergenceMax: 0,
    rng: createRng(options.seed + index * 7919, 'fx'),
    outQueue: [],
    inQueue: [],
    latencies: [],
    cmdSeq: 0,
    fireCommandsSent: 0,
    shotsSent: 0,
    commandsSent: 0,
    commandsDropped: 0,
    pingsSent: 0,
    pongs: 0,
    rtts: [],
    snapshots: 0,
    snapshotBytes: 0,
    snapshotMaxBytes: 0,
    framesIn: 0,
    bytesIn: 0,
    firstTick: 0,
    lastTick: 0,
    arrivalJitters: [],
    lastArrivalMs: 0,
    current: new Map(),
    lastKnown: new Map(),
    hitTargets: new Set(),
    sheepIds: new Set(),
    hits: 0,
    kills: 0,
    damageTaken: 0,
    events: new Map(),
    reviveActions: 0,
    errorFrames: new Map(),
    validation: {
      decodeFailures: 0,
      illegalPositions: 0,
      unknownOpcodes: 0,
      hitsWithoutShots: 0,
      killsWithoutHits: 0,
      wallPenetrationHits: 0,
      predictionCorrections: 0,
      shotCountMismatch: 0,
    },
    authPosition: null,
    predictTimerMs: 0,
    diverging: false,
    started: false,
    closed: false,
    closeCode: 0,
    readySent: false,
    lastYaw: 0,
    lastPitch: 0,
  };
}

const bots = [];
for (let i = 0; i < options.players; i += 1) bots.push(createBot(i));

function scheduleOutbound(bot, bytes, nowMs) {
  const oneWay = options.latencyMs / 2;
  const jitter = options.jitterMs / 2;
  const delay = Math.max(0, oneWay + rngRange(bot.rng, -jitter, jitter));
  bot.latencies.push(delay);
  const frame = bytes.slice(0, bytes.length);
  bot.outQueue.push({ atMs: nowMs + delay, frame });
  bot.outQueue.sort((a, b) => a.atMs - b.atMs);
}

function scheduleInbound(bot, frame, nowMs) {
  if (rngFloat(bot.rng) < options.loss) return;
  const oneWay = options.latencyMs / 2;
  const jitter = options.jitterMs / 2;
  const delay = Math.max(0, oneWay + rngRange(bot.rng, -jitter, jitter));
  bot.inQueue.push({ atMs: nowMs + delay, frame });
  bot.inQueue.sort((a, b) => a.atMs - b.atMs);
}

function clientClockMs() {
  return Date.now() % MOD32;
}

function elapsedMs(startMs) {
  return (clientClockMs() - startMs + MOD32) % MOD32;
}

function validateSnapshot(bot) {
  const record = bot.record;
  const ids = bot.mirror.ids;
  bot.current.clear();
  for (let i = 0; i < ids.length; i += 1) {
    const id = ids[i] ?? 0;
    readSnapshotRecord(bot.mirror.bytes, id * SNAPSHOT_RECORD_BYTES, record);
    const x = dequantizePosition(record.xCm);
    const y = dequantizePosition(record.yCm);
    const z = dequantizePosition(record.zCm);
    if (
      !Number.isFinite(x) ||
      !Number.isFinite(y) ||
      !Number.isFinite(z) ||
      Math.abs(x) > PLAYER_LIMIT_M + 1.5 ||
      Math.abs(z) > PLAYER_LIMIT_M + 1.5
    ) {
      bot.validation.illegalPositions += 1;
    }
    const entry = { id, kind: record.kind, x, y, z, hpRatio: record.hpRatioUnits / 255 };
    bot.current.set(id, entry);
    bot.lastKnown.set(id, entry);
    if (record.kind === 'sheep' && bot.sheepIds.size < 4096) bot.sheepIds.add(id);
  }
}

function onSnapshot(bot, frameBytes) {
  const mirror = bot.mirror;
  bot.snapshots += 1;
  bot.snapshotBytes += frameBytes;
  if (frameBytes > bot.snapshotMaxBytes) bot.snapshotMaxBytes = frameBytes;
  const arrivalMs = Date.now();
  if (bot.lastArrivalMs !== 0 && bot.arrivalJitters.length < 4096) {
    bot.arrivalJitters.push(Math.abs(arrivalMs - bot.lastArrivalMs - SERVER_TICK_MS));
  }
  bot.lastArrivalMs = arrivalMs;
  if (bot.firstTick === 0) bot.firstTick = mirror.tick;
  if (mirror.tick > bot.lastTick) bot.lastTick = mirror.tick;
  validateSnapshot(bot);
  if (bot.myPid > 0) {
    const mine = bot.current.get(bot.myPid);
    if (mine !== undefined) {
      bot.authPosition = mine;
      const predicted = bot.predicted.pos;
      const error = Math.hypot(mine.x - predicted.x, mine.z - predicted.z);
      if (error > CORRECTION_EPSILON_M) {
        if (!bot.diverging) {
          bot.diverging = true;
          bot.validation.predictionCorrections += 1;
        }
      } else if (error < CORRECTION_EPSILON_M * 0.6) {
        bot.diverging = false;
      }
    }
  }
}

function barnBlocks(bot, target) {
  const origin = bot.lastKnown.get(bot.myPid);
  if (origin === undefined || target === undefined) return false;
  const eyeY = origin.y + CONFIG.player.eyeHeight;
  const dx = target.x - origin.x;
  const dy = target.y + CONFIG.entity.heightByKind.sheep * 0.5 - eyeY;
  const dz = target.z - origin.z;
  const distance = Math.hypot(dx, dy, dz);
  if (distance <= 0.001) return false;
  const direction = createVec3();
  direction.x = dx / distance;
  direction.y = dy / distance;
  direction.z = dz / distance;
  const barn = ARENA.barn;
  const hit = createRayHit();
  rayVsAabb(
    origin.x,
    eyeY,
    origin.z,
    direction.x,
    direction.y,
    direction.z,
    barn.minX,
    barn.minY,
    barn.minZ,
    barn.maxX,
    barn.maxY,
    barn.maxZ,
    distance * WALL_BLOCK_MARGIN,
    hit,
  );
  return hit.hit;
}

function handleEvents(bot, events) {
  for (let i = 0; i < events.length; i += 1) {
    const event = events[i];
    if (event === undefined) continue;
    bot.events.set(event.type, (bot.events.get(event.type) ?? 0) + 1);
    if (event.type === 'playerHit') {
      if (event.subjectId === bot.myPid) {
        bot.hits += 1;
        if (bot.fireCommandsSent === 0) bot.validation.hitsWithoutShots += 1;
        if (event.targetId > 0) bot.hitTargets.add(event.targetId);
        if (barnBlocks(bot, bot.lastKnown.get(event.targetId))) {
          bot.validation.wallPenetrationHits += 1;
        }
      } else if (event.targetId === bot.myPid) {
        bot.damageTaken += event.value;
      }
    } else if (event.type === 'sheepKilled' && event.subjectId === bot.myPid) {
      bot.kills += 1;
      if (!bot.hitTargets.has(event.targetId)) bot.validation.killsWithoutHits += 1;
    } else if (event.type === 'playerDowned' && event.targetId === bot.myPid) {
      bot.events.set('playerDownedSelf', (bot.events.get('playerDownedSelf') ?? 0) + 1);
    }
  }
}

function handleFrame(bot, frame) {
  if (frame.length === 0) return;
  bot.framesIn += 1;
  bot.bytesIn += frame.length;
  const opcode = frame[0];
  if (opcode === OPCODE.welcome) {
    const decoded = decodeWelcome(frame);
    if (!decoded.ok) {
      bot.validation.decodeFailures += 1;
      return;
    }
    bot.welcome = decoded.value;
    bot.roomCode = decoded.value.roomCode;
    bot.myPid = decoded.value.pid;
    const mine = bot.lastKnown.get(bot.myPid) ?? bot.current.get(bot.myPid);
    if (mine !== undefined) {
      bot.predicted.pos.x = mine.x;
      bot.predicted.pos.z = mine.z;
    }
    return;
  }
  if (opcode === OPCODE.snapshot) {
    const decoded = decodeSnapshot(frame, bot.mirror);
    if (!decoded.ok) {
      bot.validation.decodeFailures += 1;
      return;
    }
    // O07 §4 任务 9：先推进 ack 水位，再统计快照。
    bot.ammoLedger.noteServerAck(bot.mirror.lastAckedSeq);
    onSnapshot(bot, frame.length);
    return;
  }
  if (opcode === OPCODE.event) {
    const decoded = decodeEventFrame(frame, bot.eventScratch);
    if (!decoded.ok) {
      bot.validation.decodeFailures += 1;
      return;
    }
    handleEvents(bot, decoded.value);
    return;
  }
  if (opcode === OPCODE.matchState) {
    const decoded = decodeMatchState(frame, bot.matchStateFrame);
    if (!decoded.ok) {
      bot.validation.decodeFailures += 1;
      return;
    }
    bot.matchState = decoded.value;
    return;
  }
  if (opcode === OPCODE.pong) {
    const decoded = decodePong(frame);
    if (!decoded.ok) return;
    bot.pongs += 1;
    bot.rtts.push(elapsedMs(decoded.value.clientTimeMs));
    return;
  }
  if (opcode === OPCODE.error) {
    const decoded = decodeError(frame);
    if (!decoded.ok) return;
    const key = String(decoded.value.code);
    bot.errorFrames.set(key, (bot.errorFrames.get(key) ?? 0) + 1);
    return;
  }
  if (opcode !== OPCODE.chatMessage) bot.validation.unknownOpcodes += 1;
}

function pump(nowMs) {
  for (const bot of bots) {
    while (bot.outQueue.length > 0 && (bot.outQueue[0]?.atMs ?? 0) <= nowMs) {
      const item = bot.outQueue.shift();
      if (item === undefined) break;
      const socket = bot.socket;
      if (socket !== null && socket.readyState === WebSocket.OPEN) socket.send(item.frame);
    }
    while (bot.inQueue.length > 0 && (bot.inQueue[0]?.atMs ?? 0) <= nowMs) {
      const item = bot.inQueue.shift();
      if (item === undefined) break;
      try {
        handleFrame(bot, item.frame);
      } catch (error) {
        bot.validation.decodeFailures += 1;
        process.stderr.write('bots: frame handler threw: ' + String(error) + '\n');
      }
    }
  }
}

function downedTeammate(bot) {
  const state = bot.matchState;
  if (state === null) return null;
  for (const player of state.players) {
    if (player.pid <= 0 || player.pid === bot.myPid) continue;
    if (player.downed) return player;
  }
  return null;
}

function nearestSheep(bot, from) {
  let best = null;
  let bestDistance = Number.POSITIVE_INFINITY;
  for (const entry of bot.current.values()) {
    if (entry.kind !== 'sheep') continue;
    const dx = entry.x - from.x;
    const dz = entry.z - from.z;
    const distance = dx * dx + dz * dz;
    if (distance < bestDistance) {
      bestDistance = distance;
      best = entry;
    }
  }
  return best;
}

function meInMatchState(bot) {
  const state = bot.matchState;
  if (state === null) return null;
  for (const player of state.players) {
    if (player.pid === bot.myPid) return player;
  }
  return null;
}

function decide(bot, cmd, myPos, localNowMs) {
  const me = meInMatchState(bot);
  cmd.moveX = 0;
  cmd.moveY = 0;
  cmd.yaw = bot.lastYaw;
  cmd.pitch = bot.lastPitch;
  cmd.buttons = 0;

  if (me !== null && me.downed) {
    bot.lastYaw = cmd.yaw;
    bot.lastPitch = cmd.pitch;
    return;
  }

  const buddy = downedTeammate(bot);
  const buddyPos = buddy === null ? null : (bot.lastKnown.get(buddy.pid) ?? null);
  if (buddy !== null && buddyPos !== undefined && buddyPos !== null) {
    const dx = buddyPos.x - myPos.x;
    const dz = buddyPos.z - myPos.z;
    const distance = Math.hypot(dx, dz);
    cmd.yaw = Math.atan2(dx, dz);
    cmd.pitch = 0;
    if (distance > 1.6) {
      cmd.moveX = 1;
    } else {
      cmd.buttons |= BUTTON.interact;
    }
    bot.reviveActions += 1;
    bot.lastYaw = cmd.yaw;
    bot.lastPitch = cmd.pitch;
    return;
  }

  const sheep = nearestSheep(bot, myPos);
  if (sheep === null) {
    cmd.moveX = 0;
    cmd.moveY = 0;
    bot.lastYaw = cmd.yaw;
    bot.lastPitch = cmd.pitch;
    return;
  }

  const targetY = sheep.y + CONFIG.entity.heightByKind.sheep * 0.5;
  const dx = sheep.x - myPos.x;
  const dz = sheep.z - myPos.z;
  const eyeY = myPos.y + CONFIG.player.eyeHeight;
  const flat = Math.hypot(dx, dz);
  cmd.yaw = Math.atan2(dx, dz);
  cmd.pitch = clamp(Math.atan2(targetY - eyeY, Math.max(0.05, flat)), -1.2, 1.2);
  cmd.moveX = clamp((flat - 10) * 0.2, -1, 1);
  cmd.moveY = Math.sin(localNowMs / 700 + bot.index * 1.1) * 0.35;
  if (flat > 25) cmd.moveX = 1;

  // O07 §4 任务 9：用权威弹药值对账，累计预测值与权威值的最大偏差。
  if (me !== null) {
    const slot = bot.weapon.activeSlot;
    const view = bot.ammoLedger.reconcile(me.mag, me.reserve, slot);
    bot.ammoDivergenceMax = Math.max(bot.ammoDivergenceMax, Math.abs(view.mag - me.mag));
  }

  const mag = activeMag(bot.weapon);
  const needsReload = mag <= 0 || (me !== null && me.mag === 0 && !isReloading(bot.weapon));
  if (needsReload) {
    cmd.buttons |= BUTTON.reload;
  } else if (flat < 45) {
    cmd.buttons |= BUTTON.fire;
  }
  bot.lastYaw = cmd.yaw;
  bot.lastPitch = cmd.pitch;
}

function tickBot(bot, nowMs, runStartMs) {
  if (!bot.started || bot.closed) return;
  const localNowMs = nowMs - runStartMs;
  updateWeapon(bot.weapon, localNowMs, SERVER_TICK_MS);
  const myPos = bot.current.get(bot.myPid) ?? bot.lastKnown.get(bot.myPid) ?? null;
  const cmd = bot.command;
  cmd.seq = bot.cmdSeq;
  bot.cmdSeq = (bot.cmdSeq + 1) & 0xffff;
  cmd.tick = bot.mirror.tick;
  cmd.switchTo = 1;
  if (myPos === null) {
    cmd.moveX = 0;
    cmd.moveY = 0;
    cmd.buttons = 0;
  } else {
    decide(bot, cmd, myPos, localNowMs);
    if (options.hold) {
      cmd.moveX = 0;
      cmd.moveY = 0;
    }
  }
  if (myPos !== null) {
    stepLocalPlayer(bot.predicted, cmd, MOVE_CONFIG, SERVER_TICK_MS);
  }
  const dropped = rngFloat(bot.rng) < options.loss;
  if (dropped) {
    bot.commandsDropped += 1;
    return;
  }
  if ((cmd.buttons & BUTTON.fire) !== 0) {
    bot.fireCommandsSent += 1;
    if (tryFire(bot.weapon, localNowMs)) {
      bot.shotsSent += 1;
      bot.ammoLedger.noteLocalShot(cmd.seq, bot.weapon.activeSlot);
    }
  }
  if ((cmd.buttons & BUTTON.reload) !== 0) tryStartReload(bot.weapon, localNowMs);
  const size = encodeCommand(cmd, bot.outScratch);
  scheduleOutbound(bot, bot.outScratch.subarray(0, size), nowMs);
  bot.commandsSent += 1;
}

async function connectBot(bot, roomCode) {
  return new Promise((resolveConnect, rejectConnect) => {
    const socket = new WebSocket(options.url);
    socket.binaryType = 'arraybuffer';
    bot.socket = socket;
    const failTimer = setTimeout(
      () => rejectConnect(new Error(bot.name + ': connect timeout')),
      8000,
    );
    socket.on('open', () => {
      clearTimeout(failTimer);
      const frame = new Uint8Array(LIMITS.maxFrameBytes);
      const size = encodeJoin(
        { protocolVersion: PROTOCOL_VERSION, name: bot.name, roomCode },
        frame,
      );
      socket.send(frame.subarray(0, size));
      resolveConnect();
    });
    socket.on('message', (data) => {
      scheduleInbound(bot, new Uint8Array(data), Date.now());
    });
    socket.on('close', (code) => {
      bot.closed = true;
      bot.closeCode = code;
    });
    socket.on('error', (error) => {
      clearTimeout(failTimer);
      if (!bot.started) rejectConnect(error);
      else process.stderr.write('bots: ' + bot.name + ' socket error: ' + String(error) + '\n');
    });
  });
}

function sendReady(bot) {
  const frame = new Uint8Array(16);
  const size = encodeReady({ ready: true, weapon: 1 }, frame);
  scheduleOutbound(bot, frame.subarray(0, size), Date.now());
  bot.readySent = true;
}

function sendSimple(bot, opcode) {
  const frame = new Uint8Array(8);
  const size = encodeSimpleFrame(opcode, frame);
  scheduleOutbound(bot, frame.subarray(0, size), Date.now());
}

function sendPing(bot) {
  const frame = new Uint8Array(16);
  const size = encodePing({ clientTimeMs: clientClockMs(), lastRecvTick: bot.lastTick }, frame);
  scheduleOutbound(bot, frame.subarray(0, size), Date.now());
  bot.pingsSent += 1;
}

function sleep(ms) {
  return new Promise((resolveSleep) => setTimeout(resolveSleep, ms));
}

async function waitFor(predicate, timeoutMs, label) {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    if (predicate()) return true;
    await sleep(50);
  }
  process.stderr.write('bots: timeout waiting for ' + label + '\n');
  return false;
}

async function fetchMetrics() {
  const response = await fetch(httpUrl + '/metrics');
  const text = await response.text();
  const values = new Map();
  for (const line of text.split('\n')) {
    if (line === '' || line.startsWith('#')) continue;
    const space = line.lastIndexOf(' ');
    if (space <= 0) continue;
    const value = Number.parseFloat(line.slice(space + 1));
    if (!Number.isFinite(value)) continue;
    values.set(line.slice(0, space).trim(), value);
  }
  return values;
}

function metricDelta(start, end, key) {
  const a = start.get(key) ?? 0;
  const b = end.get(key) ?? 0;
  return Math.max(0, b - a);
}

const pumpTimer = setInterval(() => pump(Date.now()), PUMP_INTERVAL_MS);
const tickTimer = setInterval(() => {
  const nowMs = Date.now();
  for (const bot of bots) tickBot(bot, nowMs, runStartMs);
}, SERVER_TICK_MS);
const pingTimer = setInterval(() => {
  for (const bot of bots) {
    if (bot.started && !bot.closed) sendPing(bot);
  }
}, PING_INTERVAL_MS);

let runStartMs = Date.now();
let metricsStart = new Map();
let exitCode = 0;
let server = null;

function shutdown() {
  clearInterval(pumpTimer);
  clearInterval(tickTimer);
  clearInterval(pingTimer);
  for (const bot of bots) {
    const socket = bot.socket;
    if (socket !== null && socket.readyState === WebSocket.OPEN) socket.close();
  }
}

process.on('SIGINT', () => {
  shutdown();
  setTimeout(() => process.exit(130), 100);
});

try {
  const timerGranularityMs = await probeTimerGranularityMs();
  if (!options.external) {
    const { startServer } = await import('../packages/server/src/main.ts');
    server = await startServer({
      ...process.env,
      PORT: String(options.port),
      HOST: process.env.HOST ?? '127.0.0.1',
    });
    process.stderr.write(
      'bots: in-process server on ' +
        httpUrl +
        ' (timer granularity ' +
        String(timerGranularityMs) +
        'ms)\n',
    );
  }
  await connectBot(bots[0], NEW_ROOM_CODE);
  await waitFor(() => bots[0].welcome !== null, 5000, 'welcome (room creation)');
  const roomCode = bots[0].roomCode ?? NEW_ROOM_CODE;
  for (let i = 1; i < bots.length; i += 1) {
    await connectBot(bots[i], roomCode);
    await sleep(60);
  }
  await waitFor(
    () => bots.every((bot) => bot.welcome !== null && bot.myPid > 0),
    8000,
    'all welcomes',
  );
  process.stderr.write(
    'bots: room=' + roomCode + ' clients=' + String(bots.length) + ' url=' + options.url + '\n',
  );
  await sleep(300);
  for (const bot of bots) {
    const mine = bot.current.get(bot.myPid) ?? bot.lastKnown.get(bot.myPid);
    if (mine !== undefined) {
      bot.predicted.pos.x = mine.x;
      bot.predicted.pos.z = mine.z;
    }
    sendReady(bot);
  }
  await sleep(400);
  sendSimple(bots[0], OPCODE.startMatch);
  await waitFor(
    () =>
      bots.every((bot) => bot.matchState !== null && bot.matchState.phase >= MATCH_PHASE.loading),
    10000,
    'match start',
  );
  await waitFor(
    () =>
      bots.every((bot) => bot.matchState !== null && bot.matchState.phase === MATCH_PHASE.playing),
    12000,
    'playing phase',
  );

  for (const bot of bots) {
    bot.snapshots = 0;
    bot.snapshotBytes = 0;
    bot.snapshotMaxBytes = 0;
    bot.framesIn = 0;
    bot.bytesIn = 0;
    bot.firstTick = 0;
    bot.arrivalJitters.length = 0;
    bot.lastArrivalMs = 0;
    bot.hits = 0;
    bot.kills = 0;
    bot.shotsSent = 0;
    bot.fireCommandsSent = 0;
    bot.commandsSent = 0;
    bot.commandsDropped = 0;
    bot.rtts.length = 0;
    bot.pongs = 0;
    bot.pingsSent = 0;
    bot.damageTaken = 0;
    bot.reviveActions = 0;
    bot.hitTargets.clear();
    bot.ammoLedger.reset();
    bot.ammoDivergenceMax = 0;
    bot.started = true;
  }
  metricsStart = await fetchMetrics();
  runStartMs = Date.now();
  const durationMs = options.minutes * 60000;
  // O02 §4 任务 6：在 1/3 与 2/3 时刻各采样一次 /metrics，用于「替代判据」的前后段对比
  let metricsThird1 = null;
  let metricsThird2 = null;
  const thirdTimer1 = setTimeout(
    () => {
      void fetchMetrics().then((value) => {
        metricsThird1 = value;
      });
    },
    Math.round(durationMs / 3),
  );
  const thirdTimer2 = setTimeout(
    () => {
      void fetchMetrics().then((value) => {
        metricsThird2 = value;
      });
    },
    Math.round((durationMs * 2) / 3),
  );
  await sleep(durationMs);
  clearTimeout(thirdTimer1);
  clearTimeout(thirdTimer2);
  const elapsedSec = (Date.now() - runStartMs) / 1000;
  const metricsEnd = await fetchMetrics();
  await sleep(250);
  for (const bot of bots) bot.started = false;

  const clientReports = bots.map((bot) => {
    const tickSpanSec = Math.max(0.001, (bot.lastTick - bot.firstTick) * (SERVER_TICK_MS / 1000));
    const snapshotRate = bot.snapshots / elapsedSec;
    const tickRate = (bot.lastTick - bot.firstTick) / elapsedSec;
    const bandwidthKBs = bot.bytesIn / elapsedSec / 1024;
    const rttAvg =
      bot.rtts.length === 0 ? 0 : bot.rtts.reduce((a, b) => a + b, 0) / bot.rtts.length;
    return {
      name: bot.name,
      pid: bot.myPid,
      snapshots: bot.snapshots,
      snapshotRate: round(snapshotRate, 2),
      tickRate: round(tickRate, 2),
      tickSpanSec: round(tickSpanSec, 2),
      avgBytes: round(bot.snapshots === 0 ? 0 : bot.snapshotBytes / bot.snapshots, 1),
      maxBytes: bot.snapshotMaxBytes,
      bandwidthKBs: round(bandwidthKBs, 2),
      rttAvgMs: round(rttAvg, 1),
      rttP95Ms: round(percentile(bot.rtts, 0.95), 1),
      pings: bot.pingsSent,
      pongs: bot.pongs,
      snapshotArrivalJitterP95Ms: round(percentile(bot.arrivalJitters, 0.95), 1),
      commandsSent: bot.commandsSent,
      commandsDropped: bot.commandsDropped,
      shotsSent: bot.shotsSent,
      ammoDivergenceMax: bot.ammoDivergenceMax,
      hits: bot.hits,
      kills: bot.kills,
      damageTaken: round(bot.damageTaken, 0),
      reviveActions: bot.reviveActions,
      events: Object.fromEntries(bot.events),
      errors: Object.fromEntries(bot.errorFrames),
      validation: { ...bot.validation },
    };
  });

  const totalSnapshots = clientReports.reduce((sum, r) => sum + r.snapshots, 0);
  const avgBytesAggregate =
    totalSnapshots === 0
      ? 0
      : bots.reduce((sum, bot) => sum + bot.snapshotBytes, 0) / totalSnapshots;
  const maxAvgBytes = clientReports.reduce((max, r) => Math.max(max, r.avgBytes), 0);
  const bandwidthWorst = clientReports.reduce((max, r) => Math.max(max, r.bandwidthKBs), 0);
  const rateMin = clientReports.reduce(
    (min, r) => Math.min(min, r.tickRate),
    Number.POSITIVE_INFINITY,
  );
  const rateMax = clientReports.reduce((max, r) => Math.max(max, r.tickRate), 0);
  const allRtts = bots.flatMap((bot) => bot.rtts);
  const validation = {
    decodeFailures: bots.reduce((sum, bot) => sum + bot.validation.decodeFailures, 0),
    illegalPositions: bots.reduce((sum, bot) => sum + bot.validation.illegalPositions, 0),
    unknownOpcodes: bots.reduce((sum, bot) => sum + bot.validation.unknownOpcodes, 0),
    hitsWithoutShots: bots.reduce((sum, bot) => sum + bot.validation.hitsWithoutShots, 0),
    killsWithoutHits: bots.reduce((sum, bot) => sum + bot.validation.killsWithoutHits, 0),
    wallPenetrationHits: bots.reduce((sum, bot) => sum + bot.validation.wallPenetrationHits, 0),
    predictionCorrections: bots.reduce((sum, bot) => sum + bot.validation.predictionCorrections, 0),
  };

  const elapsedMinutes = elapsedSec / 60;
  const hardCorrectTotal = metricDelta(metricsStart, metricsEnd, 'ac_hard_correct_total');
  const hardCorrectPerMinutePerClient = hardCorrectTotal / elapsedMinutes / bots.length;
  const poseSuspectTotal = metricDelta(metricsStart, metricsEnd, 'ac_pose_suspect_total');
  const poseSuspectPerMinutePerClient = poseSuspectTotal / elapsedMinutes / bots.length;
  const poseRejectedTotal = metricDelta(metricsStart, metricsEnd, 'ac_pose_rejected_total');
  const serverSnapshots = metricDelta(metricsStart, metricsEnd, 'ac_snapshots_sent_total');
  const serverSnapshotRatePerClient = serverSnapshots / elapsedSec / bots.length;
  // O02：判定改用 schedule error（实际 tick 时刻 − 理想时刻）；interval error 保留作对照。
  const tickIntervalErrorP95 = metricsEnd.get('ac_tick_interval_error_ms_p95') ?? 0;
  const tickJitterP95 = tickIntervalErrorP95;
  const scheduleErrorP95 = metricsEnd.get('ac_tick_schedule_error_ms_p95') ?? 0;
  const scheduleErrorP95Early =
    metricsThird1 === null ? null : (metricsThird1.get('ac_tick_schedule_error_ms_p95') ?? 0);
  const scheduleErrorP95Late =
    metricsThird2 === null ? null : (metricsThird2.get('ac_tick_schedule_error_ms_p95') ?? 0);
  const scheduleErrorP95Delta =
    scheduleErrorP95Early === null || scheduleErrorP95Late === null
      ? null
      : Math.abs(scheduleErrorP95Late - scheduleErrorP95Early);
  const simDriftMsMax = Math.max(
    metricsEnd.get('ac_sim_drift_ms') ?? 0,
    metricsThird1 === null ? 0 : (metricsThird1.get('ac_sim_drift_ms') ?? 0),
    metricsThird2 === null ? 0 : (metricsThird2.get('ac_sim_drift_ms') ?? 0),
  );
  const tickWorkP95 = metricsEnd.get('ac_tick_work_ms_p95') ?? 0;
  const tickWorkP99 = metricsEnd.get('ac_tick_work_ms_p99') ?? 0;
  const roomBudgetExceededTotal = metricDelta(
    metricsStart,
    metricsEnd,
    'ac_room_budget_exceeded_total',
  );

  // 直接判据优先；粒度 > 8ms 时按 O02 §1 的替代判据判定，依据三件套必须写进 note。
  const s524Direct = scheduleErrorP95 <= 8;
  const s524Alt =
    timerGranularityMs > 8 &&
    scheduleErrorP95Delta !== null &&
    scheduleErrorP95Delta <= 2 &&
    simDriftMsMax <= 50;
  const s524Status = s524Direct || s524Alt ? 'pass' : 'fail';
  const s524Note = s524Direct
    ? '直接判据：schedule error p95=' +
      String(round(scheduleErrorP95, 2)) +
      'ms ≤ 8ms（本机定时器粒度 ' +
      String(timerGranularityMs) +
      'ms）；sim_drift max=' +
      String(round(simDriftMsMax, 2)) +
      'ms'
    : s524Alt
      ? '替代判据（docs/优化/O02 §1）：本机定时器粒度实测 ' +
        String(timerGranularityMs) +
        'ms > 8ms；前 1/3 schedule error p95=' +
        String(round(scheduleErrorP95Early ?? 0, 2)) +
        'ms、后 1/3=' +
        String(round(scheduleErrorP95Late ?? 0, 2)) +
        'ms（差 ' +
        String(round(scheduleErrorP95Delta ?? 0, 2)) +
        'ms ≤ 2ms）；|sim_drift| max=' +
        String(round(simDriftMsMax, 2)) +
        'ms ≤ 50ms ⇒ 误差不随运行时间累积'
      : '直接判据与替代判据都不成立：schedule error p95=' +
        String(round(scheduleErrorP95, 2)) +
        'ms，定时器粒度 ' +
        String(timerGranularityMs) +
        'ms，前后段差 ' +
        String(scheduleErrorP95Delta === null ? 'n/a' : round(scheduleErrorP95Delta, 2)) +
        'ms，|sim_drift| max=' +
        String(round(simDriftMsMax, 2)) +
        'ms';
  const serverSnapshotBytesAvg = metricDelta(metricsStart, metricsEnd, 'ac_snapshot_bytes_total');
  const serverBytesAvg =
    serverSnapshots === 0 ? 0 : serverSnapshotBytesAvg / Math.max(1, serverSnapshots);

  const ammoDivergenceMax = bots.reduce((max, bot) => Math.max(max, bot.ammoDivergenceMax), 0);

  const thresholds = [
    {
      id: 'S5.2-1',
      label: '快照字节均值 ≤ 1200B',
      measured: round(Math.max(avgBytesAggregate, maxAvgBytes), 1),
      limit: 1200,
      unit: 'B',
      status: Math.max(avgBytesAggregate, maxAvgBytes) <= 1200 ? 'pass' : 'fail',
    },
    {
      id: 'S5.2-2',
      label: '每客户端带宽 ≤ 40 KB/s',
      measured: round(bandwidthWorst, 2),
      limit: 40,
      unit: 'KB/s',
      status: bandwidthWorst <= 40 ? 'pass' : 'fail',
    },
    {
      id: 'S5.2-3',
      label: '快照速率 19–21/s（按 tick 跨度）',
      measured: round(rateMax, 2),
      limit: '19–21',
      unit: '/s',
      status: rateMin >= 19 && rateMax <= 21 ? 'pass' : 'fail',
    },
    {
      id: 'S5.2-4',
      label: '服务端 tick 调度误差 P95 ≤ 8ms（粒度 >8ms 时用替代判据）',
      measured: round(scheduleErrorP95, 2),
      limit: 8,
      unit: 'ms',
      status: s524Status,
      note: s524Note,
    },
    {
      id: 'S5.2-8',
      label: '硬纠正 ≤ 5/分钟/人',
      measured: round(hardCorrectPerMinutePerClient, 2),
      limit: 5,
      unit: '/min',
      status: hardCorrectPerMinutePerClient <= 5 ? 'pass' : 'fail',
      note:
        'hardCorrectTotal=' +
        String(hardCorrectTotal) +
        '（/metrics 差值，' +
        String(round(elapsedMinutes, 2)) +
        ' 分钟），poseSuspectTotal=' +
        String(poseSuspectTotal) +
        '（派生位移可解释，记录不处置）',
    },
    {
      id: 'S5.2-9',
      label: '隔墙命中 = 0（权威性校验）',
      measured: validation.wallPenetrationHits,
      limit: 0,
      unit: '次',
      status: validation.wallPenetrationHits === 0 ? 'pass' : 'fail',
      note: options.latencyMs >= 200 ? '' : '本次 RTT=' + String(options.latencyMs) + 'ms',
    },
    {
      id: 'S5.2-9b',
      label: '200ms RTT 下回合内无"隔墙命中"',
      measured: options.latencyMs >= 200 ? validation.wallPenetrationHits : 'not-run',
      limit: 0,
      unit: '次',
      status:
        options.latencyMs >= 200
          ? validation.wallPenetrationHits === 0
            ? 'pass'
            : 'fail'
          : 'not-measured',
      note:
        options.latencyMs >= 200
          ? '100% 回合内（--latency 200 --jitter 20 --loss 0.01）'
          : '需以 --latency 200 运行；本次为 ' + String(options.latencyMs) + 'ms',
    },
    {
      id: 'S5.2-10',
      label: '本地弹药账本预测值与权威值之差 ≤ 2',
      measured: ammoDivergenceMax,
      limit: 2,
      unit: '发',
      status: ammoDivergenceMax <= 2 ? 'pass' : 'fail',
      note: '逐帧 |账本 reconcile 显示值 − matchState 权威 mag| 的最大值',
    },
    {
      id: 'AUTH-1',
      label: '无开火即命中 = 0',
      measured: validation.hitsWithoutShots,
      limit: 0,
      unit: '次',
      status: validation.hitsWithoutShots === 0 ? 'pass' : 'fail',
    },
    {
      id: 'AUTH-2',
      label: '无命中即击杀 = 0',
      measured: validation.killsWithoutHits,
      limit: 0,
      unit: '次',
      status: validation.killsWithoutHits === 0 ? 'pass' : 'fail',
    },
    {
      id: 'AUTH-3',
      label: '快照解码失败 = 0',
      measured: validation.decodeFailures,
      limit: 0,
      unit: '次',
      status: validation.decodeFailures === 0 ? 'pass' : 'fail',
    },
    {
      id: 'AUTH-4',
      label: '快照非法位置 = 0',
      measured: validation.illegalPositions,
      limit: 0,
      unit: '次',
      status: validation.illegalPositions === 0 ? 'pass' : 'fail',
    },
  ];

  const report = {
    tool: 'tools/bots.mjs',
    generatedAt: new Date().toISOString(),
    verification: options.external
      ? '已验证（本机实测，外部服务器 ' + options.url + '）'
      : '已验证（本机实测，进程内服务器）',
    environment: {
      node: process.version,
      platform: process.platform + ' ' + process.arch,
      serverMode: options.external ? 'external' : 'in-process',
      url: options.url,
      timerGranularityMs,
    },
    options,
    room: roomCode,
    durationSec: round(elapsedSec, 2),
    clients: clientReports,
    aggregate: {
      players: bots.length,
      snapshotBytesAvg: round(avgBytesAggregate, 1),
      snapshotBytesMaxAvg: round(maxAvgBytes, 1),
      snapshotBytesMax: clientReports.reduce((max, r) => Math.max(max, r.maxBytes), 0),
      bandwidthWorstKBs: round(bandwidthWorst, 2),
      snapshotRatePerSec: round(rateMax, 2),
      snapshotRateMin: round(rateMin, 2),
      serverSnapshotRatePerClient: round(serverSnapshotRatePerClient, 2),
      rttAvgMs: round(
        allRtts.length === 0 ? 0 : allRtts.reduce((a, b) => a + b, 0) / allRtts.length,
        1,
      ),
      rttP95Ms: round(percentile(allRtts, 0.95), 1),
      configuredLatencyMs: options.latencyMs,
      configuredJitterMs: options.jitterMs,
      configuredLoss: options.loss,
      hardCorrectTotal,
      hardCorrectPerMinutePerClient: round(hardCorrectPerMinutePerClient, 2),
      poseSuspectTotal,
      poseSuspectPerMinutePerClient: round(poseSuspectPerMinutePerClient, 2),
      poseRejectedTotal,
      tickJitterP95Ms: round(tickJitterP95, 2),
      tickIntervalErrorP95Ms: round(tickIntervalErrorP95, 2),
      tickScheduleErrorP95Ms: round(scheduleErrorP95, 2),
      tickScheduleErrorP95EarlyMs:
        scheduleErrorP95Early === null ? null : round(scheduleErrorP95Early, 2),
      tickScheduleErrorP95LateMs:
        scheduleErrorP95Late === null ? null : round(scheduleErrorP95Late, 2),
      tickScheduleErrorP95DeltaMs:
        scheduleErrorP95Delta === null ? null : round(scheduleErrorP95Delta, 2),
      simDriftMsMax: round(simDriftMsMax, 2),
      tickWorkP95Ms: round(tickWorkP95, 3),
      tickWorkP99Ms: round(tickWorkP99, 3),
      roomBudgetExceededTotal,
      serverSnapshotBytesAvg: round(serverBytesAvg, 1),
      ammoDivergenceMax,
      shotsFiredLocal: bots.reduce((sum, bot) => sum + bot.shotsSent, 0),
      hitsTotal: clientReports.reduce((sum, r) => sum + r.hits, 0),
      killsTotal: clientReports.reduce((sum, r) => sum + r.kills, 0),
      damageTakenTotal: round(
        clientReports.reduce((sum, r) => sum + r.damageTaken, 0),
        0,
      ),
    },
    serverMetrics: {
      start: Object.fromEntries(metricsStart),
      end: Object.fromEntries(metricsEnd),
    },
    validation,
    thresholds,
    verdict: thresholds.every((threshold) => threshold.status === 'pass') ? 'pass' : 'fail',
  };

  const json = JSON.stringify(report, null, 2);
  if (options.out !== '') {
    const outPath = resolve(rootDir, options.out);
    mkdirSync(dirname(outPath), { recursive: true });
    writeFileSync(outPath, json + '\n');
    process.stderr.write('bots: wrote ' + outPath + '\n');
  } else {
    process.stdout.write(json + '\n');
  }

  const summaryLines = [
    '=== bots summary ===',
    'room=' +
      roomCode +
      ' clients=' +
      String(bots.length) +
      ' duration=' +
      String(round(elapsedSec, 1)) +
      's (已模拟 RTT ' +
      String(options.latencyMs) +
      'ms / 抖动 ' +
      String(options.jitterMs) +
      'ms / 丢包 ' +
      String(options.loss * 100) +
      '%)',
    'snapshot bytes avg=' +
      String(report.aggregate.snapshotBytesAvg) +
      'B max(client avg)=' +
      String(report.aggregate.snapshotBytesMaxAvg) +
      'B max frame=' +
      String(report.aggregate.snapshotBytesMax) +
      'B',
    'snapshot rate=' +
      String(report.aggregate.snapshotRateMin) +
      '–' +
      String(report.aggregate.snapshotRatePerSec) +
      '/s (server per client ' +
      String(report.aggregate.serverSnapshotRatePerClient) +
      '/s)',
    'bandwidth worst=' + String(report.aggregate.bandwidthWorstKBs) + ' KB/s',
    'RTT avg=' +
      String(report.aggregate.rttAvgMs) +
      'ms p95=' +
      String(report.aggregate.rttP95Ms) +
      'ms',
    'tick schedule error P95(/metrics)=' +
      String(report.aggregate.tickScheduleErrorP95Ms) +
      'ms (interval error P95=' +
      String(report.aggregate.tickIntervalErrorP95Ms) +
      'ms)  sim drift max=' +
      String(report.aggregate.simDriftMsMax) +
      'ms',
    'tick work p95=' +
      String(report.aggregate.tickWorkP95Ms) +
      'ms p99=' +
      String(report.aggregate.tickWorkP99Ms) +
      'ms  room budget yields=' +
      String(report.aggregate.roomBudgetExceededTotal),
    'hard corrections total=' +
      String(hardCorrectTotal) +
      ' → ' +
      String(report.aggregate.hardCorrectPerMinutePerClient) +
      '/min/client',
    'pose suspects total=' +
      String(poseSuspectTotal) +
      ' → ' +
      String(report.aggregate.poseSuspectPerMinutePerClient) +
      '/min/client (rejected=' +
      String(poseRejectedTotal) +
      ')',
    'hits=' + String(report.aggregate.hitsTotal) + ' kills=' + String(report.aggregate.killsTotal),
    'shots(fire commands)=' + String(report.aggregate.shotsFiredLocal),
    'validation=' + JSON.stringify(validation),
    'thresholds:',
  ];
  for (const threshold of thresholds) {
    summaryLines.push(
      '  [' +
        threshold.status.toUpperCase() +
        '] ' +
        threshold.id +
        ' ' +
        threshold.label +
        ' measured=' +
        String(threshold.measured) +
        threshold.unit +
        ' limit=' +
        String(threshold.limit),
    );
  }
  summaryLines.push('verdict=' + report.verdict);
  summaryLines.push(
    'strict=' + String(options.strict),
    '退出码语义：默认总是 0（阈值失败只写进报告）；加 --strict 后任一门槛 FAIL → 退出码 1',
  );
  process.stderr.write(summaryLines.join('\n') + '\n');

  shutdown();
  // O10：退出码只看 status==='fail' —— `not-measured`（例如未注入 RTT 时的 S5.2-9b）不是 FAIL，
  // 否则 4 人 2 分钟的 `pnpm check:perf` 永远红，门槛就失去意义。
  const failedThresholds = thresholds.filter((threshold) => threshold.status === 'fail');
  if (report.verdict !== 'pass') {
    process.stderr.write('bots: §5.2 阈值失败：\n');
    for (const threshold of thresholds) {
      if (threshold.status !== 'pass') {
        const tag = threshold.status === 'fail' ? 'FAIL' : 'NOT-MEASURED';
        process.stderr.write(
          '  ' +
            tag +
            ' ' +
            threshold.id +
            ' ' +
            threshold.label +
            (tag === 'FAIL' ? '' : '（不拦人）') +
            '\n',
        );
      }
    }
    if (options.strict && failedThresholds.length > 0) exitCode = 1;
  }
  if (server !== null) await server.close();
  server = null;
} catch (error) {
  process.stderr.write('bots: fatal: ' + String(error) + '\n');
  shutdown();
  if (server !== null) {
    try {
      await server.close();
    } catch {
      // 关闭失败时直接退出，避免残留监听端口
    }
  }
  exitCode = 1;
}

setTimeout(() => process.exit(exitCode), 200);
