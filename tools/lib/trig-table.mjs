#!/usr/bin/env node
// 共享角度表的唯一实现：tools/export-trig-table.mjs（生成/校验）与
// server/tools/gen-trig-table.mjs（生成 C++ 头）都从这里取采样规则、CRC 与冻结值，避免两处漂移。
// 三个冻结 CRC 是跨语言一致性的门禁（S02 §5.4）：改动采样口径必须先改这里，并同步两侧。

export const SCALE = 1073741824; // 2^30，表幅值
export const UNITS = 65536; // ADR-009 角度单位（每圈）
export const RATIO_POINTS = 1025; // 比例表点数（i/1024）

export const FROZEN_CRCS = {
  sin: 0x8bd9f737,
  atanUnits: 0x197c3a8d,
  asinUnits: 0xad2bd35e,
};

export const EXPECTED_LENGTHS = {
  sin: UNITS,
  atanUnits: RATIO_POINTS,
  asinUnits: RATIO_POINTS,
};

const CRC_TABLE = (() => {
  const table = new Uint32Array(256);
  for (let i = 0; i < 256; i += 1) {
    let c = i;
    for (let k = 0; k < 8; k += 1) c = c & 1 ? 0x82f63b78 ^ (c >>> 1) : c >>> 1;
    table[i] = c >>> 0;
  }
  return table;
})();

export function crc32c(bytes) {
  let crc = 0xffffffff;
  for (const byte of bytes) crc = CRC_TABLE[(crc ^ byte) & 0xff] ^ (crc >>> 8);
  return (crc ^ 0xffffffff) >>> 0;
}

export function crcOfInt32(list) {
  const buffer = Buffer.alloc(list.length * 4);
  for (let i = 0; i < list.length; i += 1) buffer.writeInt32LE(list[i], i * 4);
  return crc32c(buffer);
}

export function toHex(value) {
  return '0x' + value.toString(16).toUpperCase().padStart(8, '0');
}

export function buildTables() {
  const sin = new Array(UNITS);
  for (let k = 0; k < UNITS; k += 1) {
    sin[k] = Math.round(Math.sin(2 * Math.PI * k / UNITS) * SCALE);
  }
  const atanUnits = new Array(RATIO_POINTS);
  const asinUnits = new Array(RATIO_POINTS);
  for (let i = 0; i < RATIO_POINTS; i += 1) {
    atanUnits[i] = Math.round(Math.atan(i / 1024) / (2 * Math.PI) * UNITS);
    asinUnits[i] = Math.round(Math.asin(i / 1024) / (2 * Math.PI) * UNITS);
  }
  return { sin, atanUnits, asinUnits };
}

export function tableCrcs(tables) {
  return {
    sin: crcOfInt32(tables.sin),
    atanUnits: crcOfInt32(tables.atanUnits),
    asinUnits: crcOfInt32(tables.asinUnits),
  };
}

// 返回第一个与冻结值不一致的键（没有则返回 null）。
export function frozenMismatch(crcs) {
  for (const key of Object.keys(FROZEN_CRCS)) {
    if (crcs[key] !== FROZEN_CRCS[key]) {
      return key + ' 的 CRC32C 是 ' + toHex(crcs[key]) + '，冻结值是 ' + toHex(FROZEN_CRCS[key]);
    }
  }
  return null;
}
