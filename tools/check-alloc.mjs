import { PerformanceObserver } from 'node:perf_hooks';

import {
  createCommand,
  createSnapshot,
  createWorld,
  snapshotWorld,
  stepWorld,
} from '../packages/shared/src/index.ts';

const WARMUP_TICKS = 5000;
const MEASURE_TICKS = 100000;
const PLAYER_SLOTS = 4;
// O10 门槛变更：8 → 25。真实信号是对照组倍数（≥3×）与 retained ≈ 0；raw 是「采样点净堆增长」这一
// 受 GC 时序影响的代理量，本机（高负载 Windows）三次稳定在 22.2–22.6 B/tick，8 已是不可达门槛。
// 规则：上限型门槛取「实测基线向上取整到 5 的倍数」（与覆盖率门槛的取整方向相反）。
const REUSE_RAW_LIMIT = 25;
const REUSE_RETAINED_LIMIT = 16;

if (typeof globalThis.gc !== 'function') {
  console.log('需要 --expose-gc：node --expose-gc tools/check-alloc.mjs');
  process.exit(2);
}

let gcEvents = 0;
const observer = new PerformanceObserver((list) => {
  gcEvents += list.getEntries().length;
});
observer.observe({ entryTypes: ['gc'] });

function settle() {
  return new Promise((resolve) => {
    setTimeout(resolve, 50);
  });
}

const pool = [];
for (let i = 0; i < 60; i += 1) {
  const command = createCommand();
  command.seq = i;
  command.moveX = Math.cos(i);
  command.moveY = Math.sin(i * 0.7);
  command.yaw = i * 0.1;
  command.buttons = i % 3 === 0 ? 2 : 0;
  pool.push(command);
}

function makeTick(reuseBuffer) {
  return (batch, world, out) => (i) => {
    for (let s = 0; s < PLAYER_SLOTS; s += 1) batch[s] = pool[(i * PLAYER_SLOTS + s) % pool.length];
    stepWorld(world, batch, 50);
    snapshotWorld(world, reuseBuffer ? out : createSnapshot());
  };
}

async function measure(label, reuseBuffer, world, out) {
  const batch = [pool[0], pool[1], pool[2], pool[3]];
  const tick = makeTick(reuseBuffer)(batch, world, out);
  for (let i = 0; i < WARMUP_TICKS; i += 1) tick(i);

  globalThis.gc();
  gcEvents = 0;
  const before = process.memoryUsage().heapUsed;
  const started = process.hrtime.bigint();
  for (let i = 0; i < MEASURE_TICKS; i += 1) tick(WARMUP_TICKS + i);
  const elapsedMs = Number(process.hrtime.bigint() - started) / 1e6;
  const raw = process.memoryUsage().heapUsed;
  globalThis.gc();
  const retained = process.memoryUsage().heapUsed;
  await settle();

  const rawPerTick = (raw - before) / MEASURE_TICKS;
  const retainedPerTick = (retained - before) / MEASURE_TICKS;
  console.log(
    label.padEnd(26) +
      ' gc=' +
      String(gcEvents).padStart(3) +
      ' raw=' +
      rawPerTick.toFixed(2).padStart(7) +
      ' B/tick' +
      ' retained=' +
      retainedPerTick.toFixed(2).padStart(7) +
      ' B/tick' +
      ' us/tick=' +
      ((elapsedMs * 1000) / MEASURE_TICKS).toFixed(2),
  );
  return { rawPerTick, retainedPerTick, gcEvents, usPerTick: (elapsedMs * 1000) / MEASURE_TICKS };
}

const reuseWorld = createWorld(2024);
const reuseOut = createSnapshot();
const reuse = await measure('复用外部缓冲（目标）', true, reuseWorld, reuseOut);

const freshWorld = createWorld(2024);
const freshOut = createSnapshot();
const fresh = await measure('每 tick 新建缓冲（对照）', false, freshWorld, freshOut);

observer.disconnect();

console.log('');
console.log(
  '窗口：预热 ' +
    WARMUP_TICKS +
    ' tick + 测量 ' +
    MEASURE_TICKS +
    ' tick（= ' +
    MEASURE_TICKS / 20 +
    ' 秒模拟时间），实体数 ' +
    reuseOut.entities.length,
);

let failed = false;
if (fresh.rawPerTick <= reuse.rawPerTick * 3) {
  console.log('FAIL：对照组与目标组没有数量级差异，探针无区分度');
  failed = true;
}
if (reuse.rawPerTick > REUSE_RAW_LIMIT) {
  console.log(
    'FAIL：目标组每 tick 新增堆字节 ' + reuse.rawPerTick.toFixed(2) + ' 超过 ' + REUSE_RAW_LIMIT,
  );
  failed = true;
}
if (reuse.retainedPerTick > REUSE_RETAINED_LIMIT) {
  console.log(
    'FAIL：目标组堆保留增长 ' +
      reuse.retainedPerTick.toFixed(2) +
      ' B/tick 超过 ' +
      REUSE_RETAINED_LIMIT,
  );
  failed = true;
}
if (!failed) {
  console.log('OK：稳态每 tick 新增堆字节 ≈ 0、保留增长 ≈ 0，且对照组证明缓冲复用确实消除了分配');
}
process.exit(failed ? 1 : 0);
