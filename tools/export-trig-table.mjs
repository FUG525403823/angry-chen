#!/usr/bin/env node
// 共享角度表生成/校验（S02 §5.4 与 C02 §5.6 共用同一份产物）。
//   node tools/export-trig-table.mjs          生成 docs/evidence/fixtures/trig-table.json
//   node tools/export-trig-table.mjs --check  只校验不写盘（表缺失即失败）
// 采样与 CRC 规则集中在 tools/lib/trig-table.mjs；三个键 sin/atanUnits/asinUnits 全部输出。
import { existsSync, mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

import { SCALE, UNITS, FROZEN_CRCS, buildTables, frozenMismatch, tableCrcs, toHex } from './lib/trig-table.mjs';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const TARGET = join(ROOT, 'docs', 'evidence', 'fixtures', 'trig-table.json');
// ADR-017：同一份表还要作为 Unity TextAsset 随包（出包 player 里没有仓库根）。
const MIRROR = join(ROOT, 'client', 'Assets', 'Resources', 'trig-table.json');

function fail(message) {
  console.log('FAIL：' + message);
  process.exit(1);
}

const tables = buildTables();
const crcs = tableCrcs(tables);
const mismatch = frozenMismatch(crcs);
if (mismatch !== null) {
  // §8 风险对策：重生成后 CRC 不等于冻结值即停手排查采样口径，绝不写盘。
  fail('采样口径与冻结值不符：' + mismatch);
}

if (process.argv.includes('--check')) {
  if (!existsSync(TARGET)) fail('缺少 ' + TARGET);
  const stored = JSON.parse(readFileSync(TARGET, 'utf8'));
  if (stored.scale !== SCALE || stored.units !== UNITS) fail('trig-table.json 的 scale/units 不符');
  for (const key of ['sin', 'atanUnits', 'asinUnits']) {
    if (!Array.isArray(stored[key]) || stored[key].length !== tables[key].length) {
      fail(key + ' 长度不是 ' + tables[key].length);
    }
    const bad = stored[key].findIndex((value, i) => value !== tables[key][i]);
    if (bad !== -1) {
      fail(key + '[' + bad + '] 期望 ' + tables[key][bad] + ' 实际 ' + stored[key][bad]);
    }
  }
  console.log('trig-table.json OK (' + tables.sin.length + ' entries) sin=' + toHex(crcs.sin) +
    ' atan=' + toHex(crcs.atanUnits) + ' asin=' + toHex(crcs.asinUnits));
  // ADR-017：随包那份必须与仓库这份**逐字节相同**（出包 player 里没有仓库根，角度表只能是资产）。
  const mirrorText = canonical();
  if (!existsSync(MIRROR)) fail('缺少随包镜像 ' + MIRROR + '（跑一次 node tools/export-trig-table.mjs）');
  if (readFileSync(MIRROR, 'utf8') !== mirrorText) {
    fail('随包镜像与仓库那份不一致：' + MIRROR + '（跑一次 node tools/export-trig-table.mjs）');
  }
  console.log('镜像 OK：' + MIRROR + ' 与 ' + TARGET + ' 逐字节相同');
  process.exit(0);
}

function canonical() {
  return JSON.stringify({
    scale: SCALE,
    units: UNITS,
    sin: tables.sin,
    atanUnits: tables.atanUnits,
    asinUnits: tables.asinUnits,
  });
}

mkdirSync(dirname(TARGET), { recursive: true });
mkdirSync(dirname(MIRROR), { recursive: true });
writeFileSync(TARGET, canonical());
writeFileSync(MIRROR, canonical());
console.log('写入 ' + TARGET +
  '\n  sin       ' + tables.sin.length + ' 项 crc=' + toHex(crcs.sin) +
  '\n  atanUnits ' + tables.atanUnits.length + ' 项 crc=' + toHex(crcs.atanUnits) +
  '\n  asinUnits ' + tables.asinUnits.length + ' 项 crc=' + toHex(crcs.asinUnits) +
  '\n  冻结值    sin=' + toHex(FROZEN_CRCS.sin) + ' atan=' + toHex(FROZEN_CRCS.atanUnits) +
  ' asin=' + toHex(FROZEN_CRCS.asinUnits) + '（一致）' +
  '\n写入镜像 ' + MIRROR + '（ADR-017：出包 player 读它）');
