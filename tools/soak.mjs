#!/usr/bin/env node
// P10 §3/§4：长跑 soak。每 10 秒采样一次服务器进程 process.memoryUsage() 与 /metrics，
// 断言 RSS 增长斜率 < 1MB/分钟、房间数回落、无未捕获异常，并产出 Markdown 报告。
// 默认在**本进程内**启动服务器（因此 process.memoryUsage() 就是服务器进程的内存），
// 用 --url 指向外部服务器时无法读取对端 RSS，对应门槛标注为 not-measured。
import { spawn } from 'node:child_process';
import { existsSync, mkdirSync, writeFileSync } from 'node:fs';
import { cpus } from 'node:os';
import { dirname, resolve } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

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
const options = {
  minutes: Math.max(1, Number.parseFloat(args.get('minutes') ?? '30') || 30),
  sampleSec: Math.max(1, Number.parseFloat(args.get('sample') ?? '10') || 10),
  players: Math.min(4, Math.max(1, Number.parseInt(args.get('players') ?? '4', 10) || 4)),
  latencyMs: Math.max(0, Number.parseFloat(args.get('latency') ?? '80') || 0),
  jitterMs: Math.max(0, Number.parseFloat(args.get('jitter') ?? '20') || 0),
  loss: Math.min(1, Math.max(0, Number.parseFloat(args.get('loss') ?? '0.01') || 0)),
  port: Number.parseInt(args.get('port') ?? process.env.PORT ?? '8787', 10) || 8787,
  host: args.get('host') ?? process.env.HOST ?? '127.0.0.1',
  url: args.get('url') ?? '',
  external: args.has('url'),
  noBots: args.has('no-bots'),
  serverEntry: args.get('server-entry') ?? 'packages/server/src/main.ts',
  out: args.get('out') ?? '',
  json: args.get('json') ?? '',
  drainMaxSec: Math.max(30, Number.parseFloat(args.get('drain-max') ?? '150') || 150),
  warmupSec: Math.max(0, Number.parseFloat(args.get('warmup') ?? '60') || 0),
};

const wsUrl =
  options.url !== '' ? options.url : 'ws://' + options.host + ':' + String(options.port);
const httpUrl = wsUrl.replace(/^ws/, 'http').replace(/\/$/, '');

const uncaught = { exceptions: 0, rejections: 0, last: '' };
process.on('uncaughtException', (error) => {
  uncaught.exceptions += 1;
  uncaught.last = String(error && error.stack ? error.stack : error);
});
process.on('unhandledRejection', (reason) => {
  uncaught.rejections += 1;
  uncaught.last = String(reason);
});

function sleep(ms) {
  return new Promise((resolveSleep) => setTimeout(resolveSleep, ms));
}

function round(value, digits) {
  const factor = 10 ** digits;
  return Math.round(value * factor) / factor;
}

function percentile(values, p) {
  if (values.length === 0) return 0;
  const sorted = [...values].sort((a, b) => a - b);
  return sorted[Math.min(sorted.length - 1, Math.floor(sorted.length * p))] ?? 0;
}

function slopePerMinute(samples, key) {
  const n = samples.length;
  if (n < 3) return 0;
  let sumX = 0;
  let sumY = 0;
  let sumXy = 0;
  let sumXx = 0;
  for (const sample of samples) {
    const x = sample.tSec / 60;
    const y = sample[key];
    sumX += x;
    sumY += y;
    sumXy += x * y;
    sumXx += x * x;
  }
  const denominator = n * sumXx - sumX * sumX;
  if (Math.abs(denominator) < 1e-9) return 0;
  return (n * sumXy - sumX * sumY) / denominator;
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
  return Math.max(0, (end.get(key) ?? 0) - (start.get(key) ?? 0));
}

function startBotsChild(minutes) {
  const botsPath = resolve(rootDir, 'tools', 'bots.mjs');
  const child = spawn(
    process.execPath,
    [
      botsPath,
      '--players',
      String(options.players),
      '--minutes',
      String(minutes),
      '--latency',
      String(options.latencyMs),
      '--jitter',
      String(options.jitterMs),
      '--loss',
      String(options.loss),
      '--url',
      wsUrl,
    ],
    { cwd: rootDir, stdio: ['ignore', 'pipe', 'pipe'], windowsHide: true },
  );
  let stderrTail = '';
  child.stdout.on('data', () => undefined);
  child.stderr.on('data', (chunk) => {
    stderrTail = (stderrTail + chunk.toString()).slice(-4000);
  });
  const exited = new Promise((resolveExit) => {
    child.on('exit', (code) => resolveExit(code ?? -1));
  });
  return { child, exited, stderrTail: () => stderrTail };
}

let server = null;
let botsChild = null;
const startedAtMs = Date.now();

try {
  if (options.external) {
    process.stderr.write('soak: 外部服务器模式（--url），无法读取对端 RSS\n');
  } else {
    const rawEntry = existsSync(options.serverEntry)
      ? options.serverEntry
      : resolve(rootDir, options.serverEntry);
    const { startServer } = await import(pathToFileURL(rawEntry).href);
    server = await startServer({
      ...process.env,
      PORT: String(options.port),
      HOST: options.host,
    });
    process.stderr.write('soak: in-process server on ' + httpUrl + '\n');
  }

  const healthProbe = await fetch(httpUrl + '/metrics');
  if (!healthProbe.ok) throw new Error('/metrics returned ' + String(healthProbe.status));
  const metricsFirst = await fetchMetrics();
  if (!options.noBots) {
    botsChild = startBotsChild(options.minutes);
    process.stderr.write(
      'soak: bots child pid=' +
        String(botsChild.child.pid ?? 0) +
        ' for ' +
        String(options.minutes) +
        'min\n',
    );
  }

  const samples = [];
  const runDeadlineMs = startedAtMs + options.minutes * 60000;
  let lastCpu = process.cpuUsage();
  let lastCpuAtMs = Date.now();
  let botsExitCode = null;
  let botsExitedAtMs = 0;
  let drainDeadlineMs = 0;
  let roomsFinal = -1;

  const takeSample = async () => {
    const nowMs = Date.now();
    const memory = process.memoryUsage();
    const cpu = process.cpuUsage();
    const cpuDeltaUs = cpu.user - lastCpu.user + (cpu.system - lastCpu.system);
    const wallDeltaMs = Math.max(1, nowMs - lastCpuAtMs);
    lastCpu = cpu;
    lastCpuAtMs = nowMs;
    let metrics = new Map();
    let metricsOk = true;
    try {
      metrics = await fetchMetrics();
    } catch (error) {
      metricsOk = false;
      process.stderr.write('soak: /metrics sample failed: ' + String(error) + '\n');
    }
    const sample = {
      tSec: round((nowMs - startedAtMs) / 1000, 1),
      rssMb: round(memory.rss / (1024 * 1024), 2),
      heapUsedMb: round(memory.heapUsed / (1024 * 1024), 2),
      externalMb: round(memory.external / (1024 * 1024), 2),
      cpuPercent: round((cpuDeltaUs / (wallDeltaMs * 1000)) * 100, 2),
      rooms: metrics.get('ac_rooms') ?? -1,
      players: metrics.get('ac_players') ?? -1,
      connections: metrics.get('ac_connections') ?? -1,
      sheepAlive: metrics.get('ac_sheep_alive') ?? -1,
      maxEntitiesObserved: metrics.get('ac_max_entities_observed') ?? -1,
      tickJitterP95Ms: metrics.get('ac_tick_jitter_ms_p95') ?? -1,
      snapshotBytesAvg: metrics.get('ac_snapshot_bytes_avg') ?? -1,
      tickSkips: metrics.get('ac_tick_skips_total') ?? -1,
      hardCorrectTotal: metrics.get('ac_hard_correct_total') ?? -1,
      speedViolationsTotal: metrics.get('ac_speed_violations_total') ?? -1,
      damageTotal: metrics.get('ac_damage_total') ?? -1,
      metricsOk,
    };
    samples.push(sample);
    roomsFinal = sample.rooms;
    return sample;
  };

  if (botsChild !== null) {
    void botsChild.exited.then((code) => {
      botsExitCode = code;
      botsExitedAtMs = Date.now();
      drainDeadlineMs = botsExitedAtMs + options.drainMaxSec * 1000;
      process.stderr.write('soak: bots child exited code=' + String(code) + '\n');
    });
  }

  process.stderr.write('soak: sampling every ' + String(options.sampleSec) + 's\n');
  await takeSample();
  while (true) {
    await sleep(options.sampleSec * 1000);
    const before = samples.length;
    await takeSample();
    if (samples.length !== before + 1) break;
    const nowMs = Date.now();
    if (botsChild !== null) {
      if (botsExitCode !== null) {
        if (roomsFinal === 0) break;
        if (drainDeadlineMs !== 0 && nowMs >= drainDeadlineMs) break;
      }
      continue;
    }
    if (nowMs >= runDeadlineMs) {
      if (roomsFinal === 0) break;
      if (drainDeadlineMs === 0) drainDeadlineMs = runDeadlineMs + options.drainMaxSec * 1000;
      if (nowMs >= drainDeadlineMs) break;
    }
  }

  const metricsLast = await fetchMetrics();
  const elapsedSec = (Date.now() - startedAtMs) / 1000;
  const roomsMax = samples.reduce((max, sample) => Math.max(max, sample.rooms), 0);
  const rssStart = samples[0]?.rssMb ?? 0;
  const rssEnd = samples[samples.length - 1]?.rssMb ?? 0;
  const steadySamples = samples.filter((sample) => sample.tSec >= options.warmupSec);
  const rssSlopeFull = slopePerMinute(samples, 'rssMb');
  const rssSlope =
    steadySamples.length >= 3 ? slopePerMinute(steadySamples, 'rssMb') : rssSlopeFull;
  const heapSlope = slopePerMinute(samples, 'heapUsedMb');
  const cpuValues = samples.map((sample) => sample.cpuPercent).filter((value) => value >= 0);
  const cpuAvg =
    cpuValues.length === 0 ? 0 : cpuValues.reduce((a, b) => a + b, 0) / cpuValues.length;
  const sheepPeak = samples.reduce((max, sample) => Math.max(max, sample.sheepAlive), 0);
  const hardCorrectSamples = samples.map((sample) => sample.hardCorrectTotal).filter((v) => v >= 0);
  const metricsFailures = samples.filter((sample) => !sample.metricsOk).length;

  const botsExitOk = botsExitCode === null || botsExitCode === 0;
  const thresholds = [
    {
      id: 'S5.2-5',
      label: '4 人 60 羊单核 CPU ≤ 30%',
      measured: round(cpuAvg, 2),
      limit: 30,
      unit: '%',
      status:
        options.players >= 4 && sheepPeak >= 40 ? (cpuAvg <= 30 ? 'pass' : 'fail') : 'not-measured',
      note:
        options.players >= 4 && sheepPeak >= 40
          ? '实测 sheep peak=' + String(sheepPeak)
          : 'players=' + String(options.players) + ' sheep peak=' + String(sheepPeak),
    },
    {
      id: 'S5.2-6',
      label:
        '服务端 RSS 增长（稳态，扣除前 ' + String(options.warmupSec) + 's 启动预热）< 1MB/分钟',
      measured: round(rssSlope, 3),
      limit: 1,
      unit: 'MB/min',
      status: options.external ? 'not-measured' : rssSlope < 1 ? 'pass' : 'fail',
      note: options.external
        ? '外部服务器无法读取 RSS'
        : 'rss ' +
          String(rssStart) +
          '→' +
          String(rssEnd) +
          ' MB；全窗口斜率 ' +
          String(round(rssSlopeFull, 3)) +
          ' MB/min；稳态窗口 ' +
          (steadySamples.length >= 3
            ? String(steadySamples[0]?.tSec ?? 0) +
              's 起 ' +
              String(steadySamples.length) +
              ' 个样本'
            : '样本不足，回退为全窗口'),
    },
    {
      id: 'S5.2-6b',
      label: 'RSS 全窗口斜率（含启动预热，参考值）',
      measured: round(rssSlopeFull, 3),
      limit: 1,
      unit: 'MB/min',
      status: 'info',
      note: '短窗口下启动预热会抬高该值；泄漏判定以 S5.2-6 稳态斜率为准',
    },
    {
      id: 'S5.2-7a',
      label: '无未捕获异常 / 未处理拒绝',
      measured: uncaught.exceptions + uncaught.rejections,
      limit: 0,
      unit: '次',
      status: uncaught.exceptions + uncaught.rejections === 0 ? 'pass' : 'fail',
      note: uncaught.last.slice(0, 160),
    },
    {
      id: 'S5.2-7b',
      label: '房间数在 bot 离开后回落到 0（无泄漏）',
      measured: roomsFinal,
      limit: 0,
      unit: '间',
      status: roomsFinal === 0 ? 'pass' : 'fail',
      note: 'room peak=' + String(roomsMax),
    },
    {
      id: 'S5.2-extra',
      label: '/metrics 采样失败 = 0',
      measured: metricsFailures,
      limit: 0,
      unit: '次',
      status: metricsFailures === 0 ? 'pass' : 'fail',
      note: '',
    },
    {
      id: 'S5.2-bots',
      label: 'soak 内嵌 bots 子进程退出码为 0',
      measured: botsExitCode === null ? 'not-run' : botsExitCode,
      limit: 0,
      unit: '',
      status: botsExitCode === null ? 'not-measured' : botsExitOk ? 'pass' : 'fail',
      note: botsChild === null ? '--no-bots' : '',
    },
  ];

  const report = {
    tool: 'tools/soak.mjs',
    generatedAt: new Date().toISOString(),
    verification:
      options.external || options.minutes < 30
        ? '已验证（本机实测，' +
          String(options.minutes) +
          ' 分钟抽检；§6 的 30 分钟全量 soak 未验证）'
        : '已验证（本机实测，30 分钟）',
    options,
    environment: {
      node: process.version,
      platform: process.platform + ' ' + process.arch,
      cpuModel: cpus()[0]?.model ?? 'unknown',
      cpuCount: cpus().length,
      serverMode: options.external ? 'external' : 'in-process',
      serverEntry: options.external ? '' : options.serverEntry,
      url: wsUrl,
    },
    durationSec: round(elapsedSec, 1),
    samples,
    aggregate: {
      rssStartMb: rssStart,
      rssEndMb: rssEnd,
      rssMinMb: samples.reduce(
        (min, sample) => Math.min(min, sample.rssMb),
        Number.POSITIVE_INFINITY,
      ),
      rssMaxMb: samples.reduce((max, sample) => Math.max(max, sample.rssMb), 0),
      rssSlopeMbPerMinute: round(rssSlope, 3),
      rssSlopeFullMbPerMinute: round(rssSlopeFull, 3),
      rssWarmupSec: options.warmupSec,
      heapUsedSlopeMbPerMinute: round(heapSlope, 3),
      heapUsedAvgMb: round(
        samples.reduce((sum, sample) => sum + sample.heapUsedMb, 0) / Math.max(1, samples.length),
        2,
      ),
      cpuAvgPercent: round(cpuAvg, 2),
      cpuP95Percent: round(percentile(cpuValues, 0.95), 2),
      roomsMax,
      roomsFinal,
      sheepAlivePeak: sheepPeak,
      maxEntitiesObserved: samples.reduce(
        (max, sample) => Math.max(max, sample.maxEntitiesObserved),
        0,
      ),
      tickJitterP95Ms: metricsLast.get('ac_tick_jitter_ms_p95') ?? 0,
      snapshotBytesAvg: metricsLast.get('ac_snapshot_bytes_avg') ?? 0,
      ticksTotal: metricDelta(metricsFirst, metricsLast, 'ac_ticks_total'),
      tickSkips: metricDelta(metricsFirst, metricsLast, 'ac_tick_skips_total'),
      matchesFinished: metricDelta(metricsFirst, metricsLast, 'ac_matches_finished_total'),
      roomsCreated: metricDelta(metricsFirst, metricsLast, 'ac_rooms_created_total'),
      roomsReclaimed: metricDelta(metricsFirst, metricsLast, 'ac_rooms_reclaimed_total'),
      malformedFrames: metricDelta(metricsFirst, metricsLast, 'ac_malformed_frames_total'),
      hardCorrectTotal: metricDelta(metricsFirst, metricsLast, 'ac_hard_correct_total'),
      speedViolationsTotal: metricDelta(metricsFirst, metricsLast, 'ac_speed_violations_total'),
      damageTotal: round(metricDelta(metricsFirst, metricsLast, 'ac_damage_total'), 1),
      hardCorrectPerMinute: round(
        metricDelta(metricsFirst, metricsLast, 'ac_hard_correct_total') /
          Math.max(0.001, elapsedSec / 60),
        1,
      ),
      hardCorrectPerMinutePerClient: round(
        metricDelta(metricsFirst, metricsLast, 'ac_hard_correct_total') /
          Math.max(0.001, elapsedSec / 60) /
          Math.max(1, options.players),
        2,
      ),
      hardCorrectSamples:
        hardCorrectSamples.length === 0
          ? []
          : [hardCorrectSamples[0], hardCorrectSamples[hardCorrectSamples.length - 1]],
      botsExitCode,
      botStderrTail: botsChild === null ? '' : botsChild.stderrTail().slice(-1200),
    },
    uncaught: { ...uncaught },
    thresholds,
    verdict: thresholds.every((threshold) => threshold.status !== 'fail') ? 'pass' : 'fail',
  };

  const sampleRows = samples
    .map(
      (sample) =>
        '| ' +
        String(sample.tSec) +
        ' | ' +
        String(sample.rssMb) +
        ' | ' +
        String(sample.heapUsedMb) +
        ' | ' +
        String(sample.cpuPercent) +
        ' | ' +
        String(sample.rooms) +
        ' | ' +
        String(sample.sheepAlive) +
        ' |',
    )
    .join('\n');

  const thresholdRows = thresholds
    .map(
      (threshold) =>
        '| ' +
        threshold.id +
        ' | ' +
        threshold.label +
        ' | ' +
        String(threshold.measured) +
        ' ' +
        threshold.unit +
        ' | ' +
        String(threshold.limit) +
        ' | ' +
        threshold.status +
        ' | ' +
        (threshold.note ?? '') +
        ' |',
    )
    .join('\n');

  const markdown = [
    '# soak 报告（tools/soak.mjs）',
    '',
    '- 生成时间：' + report.generatedAt,
    '- 验证状态：**' + report.verification + '**',
    '- 时长：' +
      String(report.durationSec) +
      's（采样间隔 ' +
      String(options.sampleSec) +
      's，' +
      String(samples.length) +
      ' 个样本）',
    '- 服务器：' +
      report.environment.serverMode +
      ' @ ' +
      wsUrl +
      '，Node ' +
      report.environment.node,
    '- 负载：' +
      String(options.players) +
      ' bots，模拟 RTT ' +
      String(options.latencyMs) +
      'ms / 抖动 ' +
      String(options.jitterMs) +
      'ms / 丢包 ' +
      String(options.loss * 100) +
      '%',
    '',
    '## 门槛判定（P10 §5.2）',
    '',
    '| 门槛 | 说明 | 实测 | 阈值 | 判定 | 备注 |',
    '| --- | --- | --- | --- | --- | --- |',
    thresholdRows,
    '',
    '## 聚合指标',
    '',
    '- RSS：' +
      String(rssStart) +
      ' → ' +
      String(rssEnd) +
      ' MB（min ' +
      String(report.aggregate.rssMinMb) +
      ' / max ' +
      String(report.aggregate.rssMaxMb) +
      '）；稳态斜率（扣除前 ' +
      String(options.warmupSec) +
      's 预热）**' +
      String(report.aggregate.rssSlopeMbPerMinute) +
      ' MB/分钟**；全窗口斜率 ' +
      String(report.aggregate.rssSlopeFullMbPerMinute) +
      ' MB/分钟',
    '- heapUsed 均值：' +
      String(report.aggregate.heapUsedAvgMb) +
      ' MB，斜率 ' +
      String(report.aggregate.heapUsedSlopeMbPerMinute) +
      ' MB/分钟',
    '- CPU：平均 ' +
      String(report.aggregate.cpuAvgPercent) +
      '%，P95 ' +
      String(report.aggregate.cpuP95Percent) +
      '%（单核百分比）',
    '- 房间：峰值 ' +
      String(roomsMax) +
      '，结束时 ' +
      String(roomsFinal) +
      '；created=' +
      String(report.aggregate.roomsCreated) +
      ' reclaimed=' +
      String(report.aggregate.roomsReclaimed),
    '- 羊峰值：' +
      String(sheepPeak) +
      '；实体峰值：' +
      String(report.aggregate.maxEntitiesObserved),
    '- tick：' +
      String(report.aggregate.ticksTotal) +
      '，skip=' +
      String(report.aggregate.tickSkips) +
      '；抖动 P95=' +
      String(round(report.aggregate.tickJitterP95Ms, 2)) +
      'ms；快照均值=' +
      String(round(report.aggregate.snapshotBytesAvg, 1)) +
      'B',
    '- 硬纠正累计：' +
      String(report.aggregate.hardCorrectTotal) +
      '（≈' +
      String(report.aggregate.hardCorrectPerMinutePerClient) +
      '/min/人）；位置/速度违规累计：' +
      String(report.aggregate.speedViolationsTotal) +
      '；累计伤害：' +
      String(report.aggregate.damageTotal) +
      '；非法帧累计：' +
      String(report.aggregate.malformedFrames),
    '- 未捕获异常：' + String(uncaught.exceptions) + '，未处理拒绝：' + String(uncaught.rejections),
    botsChild === null
      ? '- bots 子进程：未运行（--no-bots）'
      : '- bots 子进程退出码：' + String(botsExitCode),
    '',
    '## 采样明细',
    '',
    '| t(s) | RSS(MB) | heapUsed(MB) | CPU(%) | rooms | sheep |',
    '| --- | --- | --- | --- | --- | --- |',
    sampleRows,
    '',
    '## 判定',
    '',
    report.verdict === 'pass' ? '全部已测量门槛通过。' : '存在失败门槛，见上表（status=fail）。',
    '',
    '> 验证状态标注：本机实测数据（可复现命令见 docs/evidence/README.md）。30 分钟全量 soak 仅在 `--minutes 30` 时成立；本次为 ' +
      String(options.minutes) +
      ' 分钟，30 分钟门槛未验证。',
    '',
  ].join('\n');

  if (options.out !== '') {
    const outPath = resolve(rootDir, options.out);
    mkdirSync(dirname(outPath), { recursive: true });
    writeFileSync(outPath, markdown);
    process.stderr.write('soak: wrote ' + outPath + '\n');
  } else {
    process.stdout.write(markdown);
  }
  if (options.json !== '') {
    const jsonPath = resolve(rootDir, options.json);
    mkdirSync(dirname(jsonPath), { recursive: true });
    writeFileSync(jsonPath, JSON.stringify(report, null, 2) + '\n');
    process.stderr.write('soak: wrote ' + jsonPath + '\n');
  }

  process.stderr.write(
    'soak: verdict=' +
      report.verdict +
      ' rssSlope=' +
      String(report.aggregate.rssSlopeMbPerMinute) +
      'MB/min cpuAvg=' +
      String(report.aggregate.cpuAvgPercent) +
      '% roomsFinal=' +
      String(roomsFinal) +
      ' uncaught=' +
      String(uncaught.exceptions + uncaught.rejections) +
      '\n',
  );

  if (botsChild !== null && botsChild.child.exitCode === null) botsChild.child.kill();
  if (server !== null) await server.close();
  process.exit(report.verdict === 'pass' ? 0 : 1);
} catch (error) {
  process.stderr.write('soak: fatal: ' + String(error) + '\n');
  if (botsChild !== null) botsChild.child.kill();
  if (server !== null) {
    try {
      await server.close();
    } catch {
      // 关闭失败时直接退出，避免残留监听端口
    }
  }
  process.exit(1);
}
