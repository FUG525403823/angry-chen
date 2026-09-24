#!/usr/bin/env node
// 由共享角度表 JSON 生成 server/src/core/trig_table.hpp（S02 §4 任务 5 / §5.4）。
//   node tools/export-trig-table.mjs && node server/tools/gen-trig-table.mjs
// 采样规则、冻结 CRC 与 CRC 算法都来自 tools/lib/trig-table.mjs（与客户端链共用同一份）。
// 生成物含三张表、三个 CRC 常量与全部查表入口；禁止手改 trig_table.hpp。

import { readFileSync, writeFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

import {
  SCALE,
  UNITS,
  FROZEN_CRCS,
  EXPECTED_LENGTHS,
  buildTables,
  crcOfInt32,
  frozenMismatch,
  tableCrcs,
  toHex,
} from '../../tools/lib/trig-table.mjs';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..', '..');
const SOURCE = join(ROOT, 'docs', 'evidence', 'fixtures', 'trig-table.json');
const TARGET = join(ROOT, 'server', 'src', 'core', 'trig_table.hpp');
const VALUES_PER_LINE = 12;

function fail(message) {
  console.log('FAIL：' + message);
  process.exit(1);
}

// 1) 采样口径本身必须落在冻结 CRC 上（即使 JSON 被重新生成过）。
const freshMismatch = frozenMismatch(tableCrcs(buildTables()));
if (freshMismatch !== null) fail('本机采样口径与冻结值不符：' + freshMismatch);

// 2) 入库的 JSON 必须与冻结值一致。
const table = JSON.parse(readFileSync(SOURCE, 'utf8'));
if (table.scale !== SCALE || table.units !== UNITS) {
  fail('trig-table.json 的 scale/units 不是 ' + SCALE + '/' + UNITS);
}
for (const key of Object.keys(EXPECTED_LENGTHS)) {
  const values = table[key];
  if (!Array.isArray(values) || values.length !== EXPECTED_LENGTHS[key]) {
    fail(key + ' 长度不是 ' + EXPECTED_LENGTHS[key]);
  }
  for (const value of values) {
    if (!Number.isInteger(value) || value < -2147483648 || value > 2147483647) {
      fail(key + ' 含非 int32 值：' + value);
    }
  }
  const crc = crcOfInt32(values);
  if (crc !== FROZEN_CRCS[key]) {
    fail(key + ' 的 CRC32C 是 ' + toHex(crc) + '，冻结值是 ' + toHex(FROZEN_CRCS[key]));
  }
}

function emitArray(name, values, comment) {
  const lines = ['// ' + comment, 'inline constexpr int32_t ' + name + '[' + values.length + '] = {'];
  for (let i = 0; i < values.length; i += VALUES_PER_LINE) {
    lines.push('    ' + values.slice(i, i + VALUES_PER_LINE).join(', ') + ',');
  }
  lines.push('};');
  return lines.join('\n');
}

const header = [
  '#pragma once',
  '// 本文件由 server/tools/gen-trig-table.mjs 从 docs/evidence/fixtures/trig-table.json 生成，禁止手改。',
  '// 重新生成：node tools/export-trig-table.mjs && node server/tools/gen-trig-table.mjs',
  '//',
  '// S02 §5.4：ADR-010 下唯一允许的角度入口。运行时禁止 sin/cos/atan2/asin 等超越函数（ADR-010 §2），',
  '// 所有角度运算走本文件的查表函数。表与客户端链共用同一份 JSON，逐位一致由三个 CRC32C 门禁保证。',
  '',
  '#include <cmath>',
  '#include <cstddef>',
  '#include <cstdint>',
  '',
  'namespace ac {',
  '',
  'inline constexpr double kSinScale = 1073741824.0;  // 2^30，表幅值',
  'inline constexpr uint32_t kUnitsPerTurn = 65536u;  // ADR-009 角度单位（每圈）',
  'inline constexpr uint32_t kQuarterTurn = 16384u;   // 四分之一圈 = PI/2',
  'inline constexpr double kTwoPi = 6.28318530717958647692528676655900577;',
  '',
  emitArray('kSinQ30', table.sin, 'sin(2*PI*k/65536) * 2^30，k = 角度单位。'),
  '',
  emitArray('kAtanUnits', table.atanUnits, 'atan(i/1024) / (2*PI) * 65536，i = 比例 * 1024。'),
  '',
  emitArray('kAsinUnits', table.asinUnits, 'asin(i/1024) / (2*PI) * 65536，i = 比例 * 1024。'),
  '',
  '// 表长必须正好是每圈的角度单位数（math.hpp 里还有 kTwoPi == 2*kPi 的静态断言）。',
  'static_assert(kUnitsPerTurn == sizeof(kSinQ30) / sizeof(kSinQ30[0]), "kSinQ30 必须正好 kUnitsPerTurn 项");',
  'static_assert(sizeof(kAtanUnits) == sizeof(kAsinUnits), "两张比例表必须等长");',
  '',
  '// 三张表的 CRC32C（int32 小端字节流），与 trig-table.json 同源，是跨语言一致性的机器门禁。',
  'inline constexpr uint32_t kSinQ30Crc32c = ' + toHex(FROZEN_CRCS.sin) + 'u;',
  'inline constexpr uint32_t kAtanUnitsCrc32c = ' + toHex(FROZEN_CRCS.atanUnits) + 'u;',
  'inline constexpr uint32_t kAsinUnitsCrc32c = ' + toHex(FROZEN_CRCS.asinUnits) + 'u;',
  '',
  'namespace detail {',
  '',
  '// 比例 -> 表下标：最近点，不插值；负数与 NaN 归 0（调用方负责符号）。',
  'inline std::size_t ratioIndex(double ratio) noexcept {',
  '  if (!(ratio > 0.0)) return 0u;',
  '  const double scaled = std::floor(ratio * 1024.0 + 0.5);',
  '  if (scaled >= 1024.0) return 1024u;',
  '  return static_cast<std::size_t>(scaled);',
  '}',
  '',
  '}  // namespace detail',
  '',
  'inline double sinUnits(uint16_t units) noexcept {',
  '  return static_cast<double>(kSinQ30[units]) * (1.0 / kSinScale);',
  '}',
  '',
  'inline double cosUnits(uint16_t units) noexcept {',
  '  const uint32_t shifted = (static_cast<uint32_t>(units) + kQuarterTurn) & 0xFFFFu;',
  '  return static_cast<double>(kSinQ30[shifted]) * (1.0 / kSinScale);',
  '}',
  '',
  'inline double radiansFromUnits(uint16_t units) noexcept {',
  '  return static_cast<double>(units) * (kTwoPi / 65536.0);',
  '}',
  '',
  '// 角度单位 -> atan2(dx, dz) 的整数实现（八分圆算法，只用一次除法与一次乘法）。',
  '// 精度：相对 atan2 误差 <= 6 单位（实测 dx=0.3,dz=0.7 差 1 单位、dx=-0.9,dz=0.2 差 4 单位；20000 个方向最坏 5 单位）。',
  'inline uint16_t angleUnitsFromVector(double dx, double dz) noexcept {',
  '  if (!std::isfinite(dx) || !std::isfinite(dz)) return 0u;',
  '  const double ax = dx < 0.0 ? -dx : dx;',
  '  const double az = dz < 0.0 ? -dz : dz;',
  '  const bool isSwapped = ax > az;  // 用 atan 表覆盖 [0, 1] 比例',
  '  const double num = isSwapped ? az : ax;',
  '  const double den = isSwapped ? ax : az;',
  '  const std::size_t index = den == 0.0 ? 0u : detail::ratioIndex(num / den);',
  '  int32_t a = isSwapped ? (16384 - kAtanUnits[index]) : kAtanUnits[index];',
  '  if (dx < 0.0) a = 65536 - a;',
  '  if (dz < 0.0) a = (32768 - a) & 0xFFFF;',
  '  return static_cast<uint16_t>(a & 0xFFFF);',
  '}',
  '',
  '// 比例 -> 角度单位：kAsinUnits 上取最近点（不插值），+-1 直接取端点，非有限输入归 0。',
  '// 表只有 1025 点且 asin 在 |r| -> 1 处导数发散，所以 |r| 接近 1 时误差可达数百单位',
  '// （实测 r=0.9995 差 326 单位）；需要更高精度须先按 S02 §8 改契约（2049 点 + 两侧同一插值规则）。',
  'inline uint16_t angleUnitsFromRatio(double ratio) noexcept {',
  '  if (!std::isfinite(ratio)) return 0u;',
  '  if (ratio <= -1.0) return static_cast<uint16_t>(65536u - kQuarterTurn);',
  '  if (ratio >= 1.0) return static_cast<uint16_t>(kQuarterTurn);',
  '  const bool isNegative = ratio < 0.0;',
  '  const double magnitude = isNegative ? -ratio : ratio;',
  '  const int32_t units = kAsinUnits[detail::ratioIndex(magnitude)];',
  '  const int32_t a = isNegative ? (65536 - units) : units;',
  '  return static_cast<uint16_t>(a & 0xFFFF);',
  '}',
  '',
  '}  // namespace ac',
  '',
].join('\n');

writeFileSync(TARGET, header);
console.log('写入 ' + TARGET + '（' + header.length + ' 字节）\n' +
  '  sin crc=' + toHex(FROZEN_CRCS.sin) + ' atan crc=' + toHex(FROZEN_CRCS.atanUnits) +
  ' asin crc=' + toHex(FROZEN_CRCS.asinUnits) + '（与冻结值一致）');
