#!/usr/bin/env node
// O04 基准：进程内 4 玩家 + N 羊，分段统计 stepWorld / snapshotWorld 的单 tick 耗时与稳态分配。
// 用法：node tools/bench-sim.mjs --sheep 60 --ticks 6000 [--out docs/evidence/bench-sim-60sheep.json --label baseline-o03]
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { cpus } from 'node:os';
import { dirname } from 'node:path';

import {
  ARENA,
  BUTTON,
  LIMITS,
  countActive,
  createCommand,
  createRng,
  createSnapshot,
  createSnapshotBaseline,
  createWorld,
  encodeSnapshot,
  getEntity,
  snapshotWorld,
  spawnEntity,
  stepWorld,
} from '../packages/shared/src/index.ts';

const SLO_STEP_P95_MS = 8;
const SLO_STEP_P99_MS = 12;

function parseArgs(argv) {
  const args = new Map();
  for (let i = 0; i < argv.length; i += 1) {
    const token = argv[i];
    if (token === undefined || !token.startsWith('--')) continue;
    const next = argv[i + 1];
    if (next === undefined || next.startsWith('--')) {
      args.set(token.slice(2), 'true');
      continue;
    }
    args.set(token.slice(2), next);
    i += 1;
  }
  return args;
}

const args = parseArgs(process.argv.slice(2));
const sheepTarget = Number(args.get('sheep') ?? 60);
const measureTicks = Number(args.get('ticks') ?? 6000);
const warmupTicks = Number(args.get('warmup') ?? 500);
const seed = Number(args.get('seed') ?? 2024);
const playerSlots = Number(args.get('players') ?? 4);
const outPath = args.get('out');
const label = args.get('label') ?? 'run';
const strict = args.has('strict');

const world = createWorld(seed);

const herdRng = createRng(seed, 'bench-herd');
const enemySpawns = ARENA.enemySpawnPoints;

function spawnHerdTo(target) {
  let spawned = 0;
  while (countActive(world, 'sheep') < target) {
    const point = enemySpawns[spawned % enemySpawns.length];
    if (point === undefined) break;
    const angle = herdRng() * Math.PI * 2;
    const radius = 1 + herdRng() * 7;
    spawnEntity(
      world,
      'sheep',
      point.x + Math.cos(angle) * radius,
      0,
      point.z + Math.sin(angle) * radius,
    );
    spawned += 1;
    if (spawned > target * 4) break;
  }
}

spawnHerdTo(sheepTarget);

const playerIds = [];
for (const id of world.activeIds) {
  const entity = getEntity(world, id);
  if (entity !== undefined && entity.kind === 'player') playerIds.push(id);
}

const commandRng = createRng(seed, 'bench-command');
const commandPool = [];
for (let i = 0; i < 600; i += 1) {
  const command = createCommand();
  command.seq = i % 65536;
  command.tick = i;
  command.moveX = commandRng() * 2 - 1;
  command.moveY = commandRng() * 2 - 1;
  command.yaw = commandRng() * Math.PI * 2;
  command.pitch = (commandRng() * 2 - 1) * 0.3;
  let buttons = 0;
  if (commandRng() < 0.8) buttons |= BUTTON.fire;
  if (commandRng() < 0.1) buttons |= BUTTON.reload;
  command.buttons = buttons;
  command.switchTo = commandRng() < 0.2 ? 1 : 0;
  commandPool.push(command);
}

const batch = [];
function fillBatch(tick) {
  batch.length = 0;
  for (let slot = 0; slot < playerSlots; slot += 1) {
    const command = commandPool[(tick * playerSlots + slot) % commandPool.length];
    if (command !== undefined) batch.push(command);
  }
}

/** 长跑中保持负载稳定：补足羊数、让被打倒的玩家继续输出伤害（否则 300s 后负载衰减，基准不可比）。 */
function stabilize() {
  if (countActive(world, 'sheep') < sheepTarget) spawnHerdTo(sheepTarget);
  for (let i = 0; i < playerIds.length; i += 1) {
    const player = getEntity(world, playerIds[i] ?? 0);
    if (player === undefined || !player.active) continue;
    if (player.hp < player.maxHp * 0.5) player.hp = player.maxHp;
    if (player.combat.downed.downed) player.combat.downed.downed = false;
  }
}

const out = createSnapshot();
const stepMs = new Float64Array(measureTicks);
const snapMs = new Float64Array(measureTicks);
const totalMs = new Float64Array(measureTicks);

for (let i = 0; i < warmupTicks; i += 1) {
  fillBatch(i);
  stepWorld(world, batch, 50);
  snapshotWorld(world, out);
  stabilize();
}

// O05：编码段 —— 与房间一样按会话各持一份 baseline，每 tick 编 4 条差分帧（periodicFull 同房间策略）。
const encodeSessions = 4;
const encodeBuffer = new Uint8Array(LIMITS.maxFrameBytes);
const encodeBaselines = [];
for (let i = 0; i < encodeSessions; i += 1) encodeBaselines.push(createSnapshotBaseline());
const encodeMs = new Float64Array(measureTicks);

const hasGc = typeof globalThis.gc === 'function';

for (let i = 0; i < measureTicks; i += 1) {
  fillBatch(warmupTicks + i);
  const started = process.hrtime.bigint();
  stepWorld(world, batch, 50);
  const stepped = process.hrtime.bigint();
  snapshotWorld(world, out);
  const realized = process.hrtime.bigint();
  stepMs[i] = Number(stepped - started) / 1e6;
  snapMs[i] = Number(realized - stepped) / 1e6;
  totalMs[i] = Number(realized - started) / 1e6;
  const periodicFull = world.tick % LIMITS.fullSnapshotIntervalTicks === 0;
  const encodeStart = process.hrtime.bigint();
  for (const baseline of encodeBaselines) {
    encodeSnapshot(out, baseline, 0, encodeBuffer, baseline.tick === 0 || periodicFull);
  }
  const encodeEnd = process.hrtime.bigint();
  encodeMs[i] = Number(encodeEnd - encodeStart) / 1e6;
  stabilize();
}

// 稳态分配：单窗口 raw 受 GC 时机影响极大，这里跑多个窗口取最小值（最小 ≈ 真实新增）。
const allocWindows = Number(args.get('alloc-windows') ?? 3);
const allocTicks = Number(args.get('alloc-ticks') ?? 2000);
if (hasGc) globalThis.gc();
const heapBeforeAlloc = process.memoryUsage().heapUsed;
const rawSamples = [];
for (let w = 0; w < allocWindows; w += 1) {
  if (hasGc) globalThis.gc();
  const before = process.memoryUsage().heapUsed;
  for (let i = 0; i < allocTicks; i += 1) {
    fillBatch(warmupTicks + measureTicks + w * allocTicks + i);
    stepWorld(world, batch, 50);
    snapshotWorld(world, out);
    stabilize();
  }
  rawSamples.push((process.memoryUsage().heapUsed - before) / allocTicks);
}
const heapRawMin = Math.min(...rawSamples);
if (hasGc) globalThis.gc();
const heapRetained = process.memoryUsage().heapUsed;

function stats(values) {
  const sorted = Float64Array.from(values);
  sorted.sort();
  const at = (q) =>
    sorted[Math.min(sorted.length - 1, Math.max(0, Math.round(q * (sorted.length - 1))))] ?? 0;
  let sum = 0;
  for (let i = 0; i < sorted.length; i += 1) sum += sorted[i] ?? 0;
  return {
    p50Ms: at(0.5),
    p95Ms: at(0.95),
    p99Ms: at(0.99),
    maxMs: sorted[sorted.length - 1] ?? 0,
    meanMs: sorted.length > 0 ? sum / sorted.length : 0,
  };
}

const step = stats(stepMs);
const snapshot = stats(snapMs);
const encode = stats(encodeMs);
const total = stats(totalMs);
const run = {
  label,
  measuredAt: new Date().toISOString(),
  options: {
    sheep: sheepTarget,
    ticks: measureTicks,
    warmup: warmupTicks,
    seed,
    players: playerSlots,
  },
  scenario: {
    entities: out.entities.length,
    liveEntities: world.liveCount,
    players: playerIds.length,
    sheepAlive: countActive(world, 'sheep'),
    ticks: world.tick,
  },
  step,
  snapshot,
  encode,
  total,
  alloc: {
    // rawPerTick：多窗口最小值（B/tick）；retainedPerTick：全部窗口 gc 后的净增长。
    rawPerTick: heapRawMin,
    rawSamples,
    allocWindows,
    allocTicks,
    retainedPerTick: (heapRetained - heapBeforeAlloc) / (allocWindows * allocTicks),
    gcAvailable: hasGc,
  },
  slo: { stepP95Ms: SLO_STEP_P95_MS, stepP99Ms: SLO_STEP_P99_MS },
  verdict: total.p95Ms <= SLO_STEP_P95_MS && total.p99Ms <= SLO_STEP_P99_MS ? 'pass' : 'fail',
};
const environment = {
  node: process.version,
  platform: process.platform + ' ' + process.arch,
  cpuModel: cpus()[0]?.model ?? 'unknown',
  cpuCount: cpus().length,
};

console.log('bench-sim：' + playerSlots + ' 玩家 + ' + sheepTarget + ' 羊，seed=' + seed);
console.log(
  '  运行：预热 ' +
    warmupTicks +
    ' tick + 测量 ' +
    measureTicks +
    ' tick（' +
    measureTicks / 20 +
    's 模拟时间）',
);
console.log('  实体：' + out.entities.length + '（羊 ' + run.scenario.sheepAlive + '）');
console.log(
  '  stepWorld  ：p50 ' +
    step.p50Ms.toFixed(3) +
    ' / p95 ' +
    step.p95Ms.toFixed(3) +
    ' / p99 ' +
    step.p99Ms.toFixed(3) +
    ' / max ' +
    step.maxMs.toFixed(3) +
    ' ms',
);
console.log(
  '  encodeSnapshot（4 会话）：p50 ' +
    encode.p50Ms.toFixed(3) +
    ' / p95 ' +
    encode.p95Ms.toFixed(3) +
    ' / p99 ' +
    encode.p99Ms.toFixed(3) +
    ' / max ' +
    encode.maxMs.toFixed(3) +
    ' ms',
);
console.log(
  '  snapshotWorld：p50 ' +
    snapshot.p50Ms.toFixed(3) +
    ' / p95 ' +
    snapshot.p95Ms.toFixed(3) +
    ' / p99 ' +
    snapshot.p99Ms.toFixed(3) +
    ' / max ' +
    snapshot.maxMs.toFixed(3) +
    ' ms',
);
console.log(
  '  单 tick 合计：p50 ' +
    total.p50Ms.toFixed(3) +
    ' / p95 ' +
    total.p95Ms.toFixed(3) +
    ' / p99 ' +
    total.p99Ms.toFixed(3) +
    ' / max ' +
    total.maxMs.toFixed(3) +
    ' ms（SLO p95 ≤ ' +
    SLO_STEP_P95_MS +
    '、p99 ≤ ' +
    SLO_STEP_P99_MS +
    '）',
);
console.log(
  '  稳态分配：raw ' +
    run.alloc.rawPerTick.toFixed(2) +
    ' B/tick（' +
    allocWindows +
    ' 窗口取最小：' +
    rawSamples.map((v) => v.toFixed(1)).join('/') +
    '）、retained ' +
    run.alloc.retainedPerTick.toFixed(2) +
    ' B/tick（gc=' +
    (hasGc ? 'yes' : 'no') +
    '）',
);
console.log('  判定：' + run.verdict);

if (outPath !== undefined) {
  let file = { tool: 'tools/bench-sim.mjs', environment, runs: {} };
  try {
    file = JSON.parse(readFileSync(outPath, 'utf8'));
  } catch {
    // 新文件
  }
  file.tool = 'tools/bench-sim.mjs';
  file.generatedAt = new Date().toISOString();
  file.environment = environment;
  file.runs = { ...(file.runs ?? {}), [label]: run };
  mkdirSync(dirname(outPath), { recursive: true });
  writeFileSync(outPath, JSON.stringify(file, null, 2) + '\n');
  console.log('  已写入：' + outPath + '（runs.' + label + '）');
}

process.exit(strict && run.verdict !== 'pass' ? 1 : 0);
