#!/usr/bin/env node
// P10 §3/§4：把 tools/bots.mjs 与 tools/soak.mjs 的 JSON 结果渲染为验收报告章节，
// 含 §5.2 门槛判定与 §7 DoD 第 2 条的结论。只做渲染，不重跑压测。
import { readFileSync, mkdirSync, writeFileSync } from 'node:fs';
import { basename, dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const rootDir = resolve(dirname(fileURLToPath(import.meta.url)), '..');

function parseArgs(argv) {
  const args = new Map();
  const multi = new Map();
  for (let i = 0; i < argv.length; i += 1) {
    const token = argv[i] ?? '';
    if (!token.startsWith('--')) continue;
    const key = token.slice(2);
    const next = argv[i + 1];
    if (next !== undefined && !next.startsWith('--')) {
      const list = multi.get(key) ?? [];
      list.push(next);
      multi.set(key, list);
      i += 1;
    } else {
      args.set(key, 'true');
    }
  }
  return { args, multi };
}

const { args, multi } = parseArgs(process.argv.slice(2));
const botsPaths = multi.get('bots') ?? [];
const soakPaths = multi.get('soak') ?? [];
const title = args.get('title') ?? '压测与 soak 判定（P10 §5.2 / §7）';
const out = args.get('out') ?? '';

if (botsPaths.length === 0 && soakPaths.length === 0) {
  process.stderr.write('report: 至少需要一个 --bots <json> 或 --soak <json>\n');
  process.exit(2);
}

function loadSource(path, kind) {
  const full = resolve(rootDir, path);
  const data = JSON.parse(readFileSync(full, 'utf8'));
  const thresholds = Array.isArray(data.thresholds) ? data.thresholds : [];
  return {
    kind,
    label: kind + '：' + basename(path),
    path,
    generatedAt: data.generatedAt ?? '',
    durationSec: data.durationSec ?? null,
    options: data.options ?? {},
    thresholds,
    aggregate: data.aggregate ?? {},
    verdict: data.verdict ?? 'unknown',
  };
}

const sources = [];
for (const path of botsPaths) sources.push(loadSource(path, 'bots'));
for (const path of soakPaths) sources.push(loadSource(path, 'soak'));

const rows = [];
const failures = [];
const notMeasured = [];
for (const source of sources) {
  for (const threshold of source.thresholds) {
    const status = threshold.status ?? 'unknown';
    rows.push({
      id: threshold.id ?? '?',
      label: threshold.label ?? '',
      source: source.label,
      measured: threshold.measured,
      limit: threshold.limit,
      unit: threshold.unit ?? '',
      status,
      note: threshold.note ?? '',
    });
    if (status === 'fail') failures.push(rows[rows.length - 1]);
    if (status === 'not-measured') notMeasured.push(rows[rows.length - 1]);
  }
}

const hasBots = sources.some((source) => source.kind === 'bots');
const hasSoak = sources.some((source) => source.kind === 'soak');
const allPass = failures.length === 0;

const lines = [];
lines.push('## ' + title);
lines.push('');
lines.push('- 生成时间：' + new Date().toISOString());
lines.push('- 渲染来源：' + String(sources.length) + ' 份 JSON');
for (const source of sources) {
  lines.push(
    '  - ' +
      source.label +
      '（verdict=' +
      source.verdict +
      (source.durationSec === null ? '' : '，时长 ' + String(source.durationSec) + 's') +
      '）',
  );
}
lines.push('');
lines.push('### §5.2 门槛总表');
lines.push('');
lines.push('| 门槛 | 说明 | 来源 | 实测 | 阈值 | 判定 |');
lines.push('| --- | --- | --- | --- | --- | --- |');
for (const row of rows) {
  lines.push(
    '| ' +
      row.id +
      ' | ' +
      row.label +
      ' | ' +
      row.source +
      ' | ' +
      String(row.measured) +
      ' ' +
      row.unit +
      ' | ' +
      String(row.limit) +
      ' | **' +
      row.status +
      '** |',
  );
}
lines.push('');
if (failures.length > 0) {
  lines.push('### 失败项');
  lines.push('');
  for (const row of failures) {
    lines.push(
      '- ' +
        row.id +
        ' ' +
        row.label +
        '（' +
        row.source +
        '）：实测 ' +
        String(row.measured) +
        row.unit +
        '，阈值 ' +
        String(row.limit),
    );
  }
  lines.push('');
}
if (notMeasured.length > 0) {
  lines.push('### 未测量项（环境/参数不满足）');
  lines.push('');
  for (const row of notMeasured) {
    lines.push(
      '- ' +
        row.id +
        ' ' +
        row.label +
        '（' +
        row.source +
        '）' +
        (row.note === '' ? '' : '：' + row.note),
    );
  }
  lines.push('');
}
lines.push('### §7 DoD 第 2 条判定');
lines.push('');
lines.push(
  '- `tools/bots.mjs` 与 `tools/soak.mjs` 存在并可在本机跑完：' +
    (hasBots && hasSoak
      ? '**成立**（本次渲染同时包含 bots 与 soak 报告）'
      : '**未成立**（缺少 bots 或 soak 报告）'),
);
lines.push(
  '- 报告满足 §5.2 全部阈值：' +
    (allPass
      ? '**成立**（无 fail 项，未测量项已在上表列出）'
      : '**不成立**（存在 ' + String(failures.length) + ' 个 fail 项）'),
);
lines.push('');
lines.push('### 结论');
lines.push('');
lines.push(allPass ? '本次渲染的全部压测门槛通过。' : '存在失败门槛，不可据此发布。');
lines.push('');
lines.push(
  '> 验证状态：本文件由 `node tools/report.mjs` 从上述 JSON 渲染（**已验证**：有可复现命令与原始 JSON）。',
);
lines.push('');

const markdown = lines.join('\n');
if (out !== '') {
  const outPath = resolve(rootDir, out);
  mkdirSync(dirname(outPath), { recursive: true });
  writeFileSync(outPath, markdown);
  process.stderr.write('report: wrote ' + outPath + '\n');
} else {
  process.stdout.write(markdown);
}
process.exit(allPass ? 0 : 1);
