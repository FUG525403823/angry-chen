#!/usr/bin/env node
// S07 §4：从冻结的 v1 实现（packages/shared）导出跨语言对拍向量（fixture）。
// 纪律：源仓库 D:/projects/tmp/angry-chen-bak 全程只读；补丁只作用于可写派生副本；产物只写 --out。
//
// 用法：
//   node tools/export-fixtures.mjs                                 写盘（默认派生副本 + docs/evidence/fixtures）
//   node tools/export-fixtures.mjs --check                         只比较，不写盘（幂等校验）
//   node tools/export-fixtures.mjs --list                          清单：name / 字节数 / SHA256
//   node tools/export-fixtures.mjs --only <name[,name]>             只渲染选中的子集（体积门按本次选中的批次判定）
//   node tools/export-fixtures.mjs --root <副本> --out <目录>
import { createHash } from 'node:crypto';
import { cpSync, existsSync, mkdirSync, readdirSync, readFileSync, statSync, writeFileSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

// CRC32C 与角度表常量复用 tools/lib/trig-table.mjs 的唯一实现（S02 §5.4 冻结；C++ 侧
// ac::crc32c 与它逐位一致，另写一份迟早漂移）。
import { RATIO_POINTS, SCALE, UNITS, crc32c } from './lib/trig-table.mjs';

const REPO_ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const READONLY_SOURCE = 'D:/projects/tmp/angry-chen-bak';
const DEFAULT_ROOT = 'D:/projects/tmp/angry-chen-fixture';
const V1_ENTRY = 'packages/shared/src/index.ts';
const V1_CONFIG = 'packages/shared/src/config/index.ts';
const V1_RAGE = 'packages/shared/src/combat/rage.ts';
const V1_WEAPON = 'packages/shared/src/combat/weapon.ts';
const V1_WEAPONS_CONFIG = 'packages/shared/src/config/weapons.ts';
const V1_COMBAT_CONFIG = 'packages/shared/src/config/combat.ts';
const V1_SHEEP_CONFIG = 'packages/shared/src/config/sheep.ts';
const V1_RESOLVE = 'packages/shared/src/combat/resolve.ts';
const V1_SHEEP_BRAIN = 'packages/shared/src/ai/sheepBrain.ts';
const V1_FLOCKING = 'packages/shared/src/ai/flocking.ts';
const V1_SHEEP_ATTACK = 'packages/shared/src/ai/sheepAttack.ts';
const V1_KING_PHASES = 'packages/shared/src/ai/kingPhases.ts';
const V1_DIRECTOR = 'packages/shared/src/ai/director.ts';
const V1_CODEC = 'packages/shared/src/net/codec.ts';
const V1_SNAPSHOT = 'packages/shared/src/snapshot.ts';
const V1_WAVES_CONFIG = 'packages/shared/src/config/waves.ts';
// S08 的 configHash 分组：导入结果在 main 里填，configHashText 只读它。
let s08 = null;
// S09 的 configHash 分组：同样在 main 里填。
let s09 = null;
const TRIG_TABLE_PATH = 'docs/evidence/fixtures/trig-table.json';
const DEFAULT_OUT = 'docs/evidence/fixtures';
// 体积门：已裁决改为「对拍向量」口径（README §6 / ADR-010 §8）——每帧只落 8 字节哈希链值 + 少量关键帧，
// 旧的「14 份 < 2 MB 全量投影」门随口径作废（每 tick 全量投影实测 ≈ 950 B，14 份结构性放不进 2 MB）。
const SIZE_GATE_FILE_BYTES = 64 * 1024;    // 单份 fixture ≤ 64 KiB
const SIZE_GATE_TOTAL_BYTES = 512 * 1024;  // 本批（14 份）合计 ≤ 512 KiB
const SCHEMA_VERSION = 2;
const TICK_MS = 50;
// 临时诊断入口：--trace <name> [--trace-ticks N] 逐 tick 打印实体与事件（只用于调场景，不写盘）。
let TRACE = null;
let TRACE_TEXT = false;

function parseArgs(argv) {
  const opts = { root: DEFAULT_ROOT, out: DEFAULT_OUT, check: false, list: false, help: false, only: null,
    trace: null, traceTicks: 400, traceText: false };
  for (let i = 0; i < argv.length; i += 1) {
    const arg = argv[i];
    if (arg === '--root') opts.root = argv[i += 1];
    else if (arg === '--out') opts.out = argv[i += 1];
    else if (arg === '--only') opts.only = (argv[i += 1] ?? '').split(',').filter((name) => name !== '');
    else if (arg === '--trace') { opts.trace = argv[i += 1]; opts.only = [opts.trace]; }
    else if (arg === '--trace-ticks') opts.traceTicks = Number(argv[i += 1]);
    else if (arg === '--trace-text') opts.traceText = true;
    else if (arg === '--check') opts.check = true;
    else if (arg === '--list') opts.list = true;
    else if (arg === '--allow-patched-copy') opts.allowPatchedCopy = true;
    else if (arg === '--help' || arg === '-h') opts.help = true;
    else throw new Error('unknown argument: ' + arg);
  }
  return opts;
}

// ---------------- CRC32C：实现复用 tools/lib/trig-table.mjs（见文件头 import） ----------------
function crc32cText(text) {
  return crc32c(Buffer.from(text, 'utf8'));
}

function assertCrcSelfTest() {
  const got = crc32cText('123456789');
  if (got !== 0xe3069283) throw new Error('crc32c self-test failed: 0x' + got.toString(16));
}

// ---------------- %.17g（与 C 的 printf 逐字节同口径） ----------------
// C 的 printf 在 tie（有效位第 18 位起恰好是 5 后面全 0）上取「就近取偶」，而 JS 的 toExponential/
// toPrecision 在 tie 上取远离 0 —— 本仓库实测到过 1 例（0x42f8ceb15abbf812：C 给 ...073.12，JS 给
// ...073.13）。configHash 是两侧文本逐字节比较，所以 tie 必须按 C 的规则精确处理：先用
// toExponential(17) 探测 tie，命中时用 BigInt 精确展开十进制再按偶舍入；未命中时 toExponential(16)
// 的 17 位就是唯一正确结果（快路径，覆盖 99.9% 的值）。
function doubleFromBits(bits) {
  const view = new DataView(new ArrayBuffer(8));
  view.setBigUint64(0, bits);
  return view.getFloat64(0);
}

function exactDecimal(value) {
  const view = new DataView(new ArrayBuffer(8));
  view.setFloat64(0, value);
  const bits = view.getBigUint64(0);
  const exponentBits = Number((bits >> 52n) & 0x7ffn);
  const fractionBits = bits & 0xfffffffffffffn;
  const mantissa = exponentBits === 0 ? fractionBits : fractionBits | (1n << 52n);
  const exponent = exponentBits === 0 ? -1074 : exponentBits - 1075;
  if (exponent >= 0) return { integer: (mantissa << BigInt(exponent)).toString(), fraction: '' };
  const shift = -exponent;
  const scaled = (mantissa * 5n ** BigInt(shift)).toString();
  if (scaled.length <= shift) return { integer: '0', fraction: '0'.repeat(shift - scaled.length) + scaled };
  return { integer: scaled.slice(0, scaled.length - shift), fraction: scaled.slice(scaled.length - shift) };
}

function g17Tie(value) {
  const exact = exactDecimal(value);
  const all = exact.integer + exact.fraction;
  let lead = 0;
  while (lead < all.length && all[lead] === '0') lead += 1;
  let keep = all.slice(lead, lead + 17);
  while (keep.length < 17) keep += '0';
  const rest = all.slice(lead + 17);
  let exponent = exact.integer.length - 1 - lead;
  let roundUp = false;
  let index = 0;
  while (index < rest.length && rest[index] === '0') index += 1;
  if (index < rest.length) {
    if (rest[index] > '5') roundUp = true;
    else if (rest[index] === '5') {
      let hasTail = false;
      for (let i = index + 1; i < rest.length; i += 1) {
        if (rest[i] !== '0') { hasTail = true; break; }
      }
      roundUp = hasTail ? true : Number(keep[16]) % 2 === 1;
    }
  }
  if (roundUp) {
    let incremented = '';
    let carry = 1;
    for (let i = 16; i >= 0; i -= 1) {
      const digit = Number(keep[i]) + carry;
      if (digit === 10) { incremented = '0' + incremented; carry = 1; } else { incremented = String(digit) + incremented; carry = 0; }
    }
    if (carry === 1) { keep = '1' + incremented.slice(0, 16); exponent += 1; } else { keep = incremented; }
  }
  return { digits: keep, exponent };
}

function formatG17(digits, exponent) {
  let trimmed = digits.replace(/0+$/, '');
  if (trimmed.length === 0) trimmed = '0';
  if (exponent < -4 || exponent >= 17) {
    const mantissa = trimmed.length > 1 ? trimmed[0] + '.' + trimmed.slice(1) : trimmed[0];
    const absExponent = exponent < 0 ? -exponent : exponent;
    return mantissa + 'e' + (exponent < 0 ? '-' : '+') + (absExponent < 10 ? '0' : '') + absExponent;
  }
  if (exponent >= 0) {
    if (trimmed.length <= exponent + 1) return trimmed + '0'.repeat(exponent + 1 - trimmed.length);
    return trimmed.slice(0, exponent + 1) + '.' + trimmed.slice(exponent + 1);
  }
  return '0.' + '0'.repeat(-exponent - 1) + trimmed;
}
function g17(value) {
  if (!Number.isFinite(value)) throw new Error('g17: non-finite value');
  if (value === 0) return Object.is(value, -0) ? '-0' : '0';
  const negative = value < 0;
  const abs = negative ? -value : value;
  const probe = abs.toExponential(17);
  const probeIndex = probe.indexOf('e');
  const probeDigits = probe[0] + probe.slice(2, probeIndex);
  let text;
  if (probeDigits[17] === '5') {
    const tie = g17Tie(abs);
    text = formatG17(tie.digits, tie.exponent);
  } else {
    const fast = abs.toExponential(16);
    const fastIndex = fast.indexOf('e');
    text = formatG17(fast[0] + fast.slice(2, fastIndex), Number(fast.slice(fastIndex + 1)));
  }
  return negative ? '-' + text : text;
}

function assertG17SelfTest() {
  // 期望值全部来自 C 侧 printf("%.17g", v) 的实测（本机 g++ 15.2.0），不是 JS 自己算的：
  // 前两条是 tie（有效位第 18 位起恰好 5 后全 0），必须走 BigInt 精确路径；其余走快路径。
  const cases = [
    [doubleFromBits(0x42f8ceb15abbf812n), '436416285491073.12'],
    [doubleFromBits(0x4350000000000001n), '18014398509481988'],
    [4.5, '4.5'],
    [40, '40'],
    [0.1, '0.10000000000000001'],
    [doubleFromBits(0x3eb0c6f7a0b5ed8dn), '9.9999999999999995e-07'],
    [doubleFromBits(0x4043accccccccccdn), '39.350000000000001'],
    [doubleFromBits(0xc043accccccccccdn), '-39.350000000000001'],
    [doubleFromBits(0x3ee4f8b588e368f1n), '1.0000000000000001e-05'],
    [1e17, '1e+17'],
    [1e20, '1e+20'],
  ];
  for (const pair of cases) {
    const got = g17(pair[0]);
    if (got !== pair[1]) throw new Error('g17 self-test failed: want ' + pair[1] + ' got ' + got);
  }
  if (g17(-0) !== '-0') throw new Error('g17 self-test failed: -0');
}

// ---------------- 共享整数表（S02 §5.4 的 C++ 实现 1:1 移植） ----------------
const TWO_PI = 6.283185307179586;
const PI = 3.141592653589793;
const QUARTER_TURN = UNITS / 4;
const RATIO_SCALE = RATIO_POINTS - 1;

function makeTrig(table) {
  const sinTable = table.sin;
  const atanUnits = table.atanUnits;
  const asinUnits = table.asinUnits;
  const kSinScale = 1 / SCALE;

  function wrapAngle(radians) {
    if (!Number.isFinite(radians)) return 0;
    const turns = Math.floor(radians / TWO_PI + 0.5);
    let a = radians - TWO_PI * turns;
    if (a > PI) a -= TWO_PI;
    else if (a <= -PI) a += TWO_PI;
    if (a === 0 && radians < 0) a = -0;
    return a;
  }

  function quantizeAngle(radians) {
    const wrapped = wrapAngle(radians);
    const units = Math.floor(wrapped / TWO_PI * UNITS + 0.5);
    let a = units % UNITS;
    if (a < 0) a += UNITS;
    return a >>> 0;
  }

  function radiansFromUnits(units) {
    return (units >>> 0) * (TWO_PI / UNITS);
  }

  function sinUnits(units) {
    return sinTable[(units >>> 0) & (UNITS - 1)] * kSinScale;
  }

  function cosUnits(units) {
    return sinTable[((units >>> 0) + QUARTER_TURN) & (UNITS - 1)] * kSinScale;
  }

  function ratioIndex(ratio) {
    if (!(ratio > 0)) return 0;
    const scaled = Math.floor(ratio * RATIO_SCALE + 0.5);
    if (scaled >= RATIO_SCALE) return RATIO_SCALE;
    return scaled;
  }

  function angleUnitsFromVector(dx, dz) {
    if (!Number.isFinite(dx) || !Number.isFinite(dz)) return 0;
    const ax = dx < 0 ? -dx : dx;
    const az = dz < 0 ? -dz : dz;
    const swapped = ax > az;
    const numerator = swapped ? az : ax;
    const denominator = swapped ? ax : az;
    const index = denominator === 0 ? 0 : ratioIndex(numerator / denominator);
    let a = swapped ? QUARTER_TURN - atanUnits[index] : atanUnits[index];
    if (dx < 0) a = UNITS - a;
    if (dz < 0) a = (UNITS / 2 - a) & (UNITS - 1);
    return a & (UNITS - 1);
  }

  function angleUnitsFromRatio(ratio) {
    if (!Number.isFinite(ratio)) return 0;
    if (ratio <= -1) return UNITS - QUARTER_TURN;
    if (ratio >= 1) return QUARTER_TURN;
    const negative = ratio < 0;
    const magnitude = negative ? -ratio : ratio;
    const units = asinUnits[ratioIndex(magnitude)];
    return (negative ? UNITS - units : units) & (UNITS - 1);
  }

  return { wrapAngle, quantizeAngle, radiansFromUnits, sinUnits, cosUnits, angleUnitsFromVector, angleUnitsFromRatio };
}

// v1 的 Math.sin/cos/atan2/asin 换成共享整数表：只在进程内替换，源仓库与副本文件都不改。
function patchMath(trig) {
  Math.sin = (radians) => trig.sinUnits(trig.quantizeAngle(radians));
  Math.cos = (radians) => trig.cosUnits(trig.quantizeAngle(radians));
  Math.atan2 = (y, x) => trig.radiansFromUnits(trig.angleUnitsFromVector(y, x));
  Math.asin = (ratio) => trig.radiansFromUnits(trig.angleUnitsFromRatio(ratio));
}

// ---------------- configHash（S07 §5.6；C++ 侧逐组同样重算） ----------------
function g17List(values) {
  return values.map(g17).join(',');
}

function configHashText(config) {
  const arena = config.arena;
  const entity = config.entity;
  const player = config.player;
  const input = config.input;
  const lines = [];
  lines.push('arena.size=' + g17List([arena.halfSize, arena.fence.height, arena.fence.thickness,
    arena.barn.minX, arena.barn.maxX, arena.barn.minY, arena.barn.maxY, arena.barn.minZ, arena.barn.maxZ]));
  lines.push('arena.playerSpawns=' + g17List(arena.playerSpawnPoints.flatMap((p) => [p.x, p.z])));
  lines.push('arena.enemySpawns=' + g17List(arena.enemySpawnPoints.flatMap((p) => [p.x, p.z])));
  lines.push('entity.limits=' + g17List([entity.maxEntities, entity.separationPasses, entity.separationEpsilon]));
  lines.push('entity.radiusByKind=' + g17List([entity.radiusByKind.player, entity.radiusByKind.sheep,
    entity.radiusByKind.projectile, entity.radiusByKind.pickup]));
  lines.push('entity.heightByKind=' + g17List([entity.heightByKind.player, entity.heightByKind.sheep,
    entity.heightByKind.projectile, entity.heightByKind.pickup]));
  lines.push('entity.baseStats=' + g17List([entity.baseStats.player.hp, entity.baseStats.player.armor,
    entity.baseStats.sheep.hp, entity.baseStats.sheep.armor,
    entity.baseStats.projectile.hp, entity.baseStats.projectile.armor,
    entity.baseStats.pickup.hp, entity.baseStats.pickup.armor]));
  lines.push('player=' + g17List([player.moveSpeed, player.sprintSpeed, player.jumpSpeed, player.gravity,
    player.maxHp, player.maxArmor, player.armorAbsorbRatio, player.radius, player.height, player.eyeHeight]));
  lines.push('input=' + g17List([input.moveAxisLimit, input.pitchLimitRad, input.buttonMask,
    input.buttons.fire, input.buttons.sprint, input.buttons.jump, input.buttons.reload,
    input.buttons.interact, input.buttons.rage, input.buttons.switchWeapon]));
  if (s08 !== null) {
    // ---- S08 §5：武器/散布/弹道/伤害/怒气/救援/命中盒（C++ 侧逐组同样重算）----
    const weaponRows = s08.slotOrder.flatMap((slot) => {
      const w = s08.weapons[slot];
      return [w.damage, w.pellets, w.rpm, w.auto ? 1 : 0, w.mag, w.reloadMs, w.spreadDeg,
        w.falloffStartM, w.falloffPerM, w.headshotMultiplier];
    });
    lines.push('weapons=' + g17List(weaponRows));
    lines.push('weapon.rules=' + g17List([s08.w.RESERVE_AMMO_INITIAL, s08.w.SPREAD_GROWTH_PER_SHOT_DEG,
      s08.w.SPREAD_MAX_DEG, s08.w.SPREAD_DECAY_DELAY_MS, s08.w.SPREAD_DECAY_PER_SECOND_DEG,
      s08.w.RECOIL_PITCH_PER_SHOT_DEG, s08.w.RECOIL_YAW_JITTER_DEG]));
    // 弹丸步长/抖动盐是 v1 resolve.ts 的内联字面量（:310-311 附近），没有导出，只能在这里抄一份：
    // 它们改了 hash 不会变，但 C++ 侧那两处也钉死在同一份计划 §5.3 上。
    lines.push('shot=' + g17List([s08.resolve.SHOT_MAX_DISTANCE_M, s08.resolve.DEG_TO_RAD,
      s08.resolve.PITCH_LIMIT_RAD, 13, 29, 0x9e3, 0x51f]));
    lines.push('damage=' + g17List([s08.c.HEAD_MIN_HEIGHT_RATIO, s08.c.TORSO_MIN_HEIGHT_RATIO,
      s08.c.BODY_PART_MULTIPLIER.limb, s08.c.ARMOR_ABSORB_RATIO, s08.c.ARMOR_MAX, s08.c.HEALTH_MAX,
      s08.c.FALLOFF_MIN_MULTIPLIER, s08.c.FRIENDLY_FIRE ? 1 : 0, s08.c.SHEEP_ELITE_STATE]));
    const rage = s08.c.RAGE;
    lines.push('rage=' + g17List([rage.max, rage.perKill, rage.perEliteKill, rage.headshotKillMultiplier,
      rage.idleDecayDelayMs, rage.decayPerSecond, rage.durationMs, rage.damageMultiplier,
      rage.fireRateMultiplier, rage.moveSpeedMultiplier]));
    const revive = s08.c.REVIVE;
    lines.push('revive=' + g17List([revive.rangeM, revive.durationMs, revive.resetDelayMs,
      revive.reviverMaxSpeed, revive.revivedHpRatio, revive.waveReviveHpRatio,
      revive.progressEventStepRatio]));
    const hitRows = s08.sheepOrder.flatMap((kind) => {
      const h = s08.sheepHit[kind];
      return [h.halfWidthM, h.halfDepthM, h.topM, h.headHalfWidthM, h.headMinYM, h.headMaxYM,
        h.headMinZM, h.headMaxZM, h.headMinM, h.torsoMinM];
    });
    lines.push('sheepHit=' + g17List(hitRows));
  }
  if (s09 !== null) {
    // ---- S09 §5.1-§5.4：羊形表 / SHEEP_AI / 状态转移表 / 局部常量 / 攻击档案 / 波次常量 ----
    // （C++ 侧 config/sheep.hpp + config/waves.hpp 逐组重算，两条文本必须逐字节一致。）
    const s = s09;
    lines.push('sheep=' + g17List(s.order.flatMap((kind) => {
      const def = s.sheep.SHEEP[kind];
      return [def.hp, def.speed, def.damage, def.price, def.radiusM, def.heightM];
    })));
    lines.push('sheep.ai=' + g17List(Object.values(s.sheep.SHEEP_AI)));
    // 转移表：13 行 × (count + 6 槽)，不足 6 的地方补 0（C++ 是定长数组）。
    const stateCodes = Object.values(s.sheep.SHEEP_STATE);
    const states = [stateCodes.length, 6];
    for (const code of stateCodes) {
      const row = s.sheep.SHEEP_STATE_TRANSITIONS[code] ?? [];
      states.push(row.length);
      for (let slot = 0; slot < 6; slot += 1) states.push(row[slot] ?? 0);
    }
    lines.push('sheep.states=' + g17List(states));
    // 局部常量：能导入的取 v1 导出；其余是 v1 的内联字面量（没有导出），按 文件:行 抄一份钉住。
    lines.push('sheep.local=' + g17List([s.brain.SHEEP_ALERT_MS, s.brain.RAM_CHARGE_TRIGGER_M,
      s.brain.RAM_CHARGE_MAX_MS, s.brain.GRAZE_REPICK_MS, s.brain.ELITE_STRAFE_MS,
      0.5,  // ELITE_STRAFE_SPEED_RATIO sheepBrain.ts:221
      0.4,  // GRAZE_SPEED_RATIO sheepBrain.ts:180
      0.5,  // GRAZE_OBSTACLE_RADIUS_RATIO sheepBrain.ts:189
      2,    // ARRIVE_SLOW_RADIUS_M sheepBrain.ts:180
      s.flockWeight.separation, s.flockWeight.alignment, s.flockWeight.cohesion,
      0.1,  // FLOCK_COHESION_SCALE flocking.ts:233
      0.5,  // FLOCK_BLEND_RATIO sheepBrain.ts:328
      s.attack.BITE_KNOCKBACK_M, s.attack.CHARGE_KNOCKBACK_M, s.attack.BOLT_LIFE_MS,
      s.attack.BOLT_RADIUS_M,
      0.6,  // BOLT_SPAWN_HEIGHT_M sheepAttack.ts:165
      0.6,  // TARGET_EYE_HEIGHT_M targeting.ts:82
      0.9,  // VICTIM_CHEST_HEIGHT_M targeting.ts:42 / sheepAttack.ts:90
      2.6,  // KING_SUMMON_RADIUS_M kingPhases.ts:39
      0.3,  // KING_SUMMON_JITTER_M kingPhases.ts:43
      s.king.KING_PHASE_1_MIN_RATIO, s.king.KING_PHASE_2_MIN_RATIO,
      config.arena.halfSize - config.arena.fence.thickness]));  // FIELD_EDGE_LIMIT_M sheepBrain.ts:257
    const sheepAttack = s.order.flatMap((kind) => {
      const w = s.sheep.SHEEP_ATTACK_PROFILE[kind];
      return [w.damage, w.pellets, w.rpm, w.auto ? 1 : 0, w.mag, w.reloadMs, w.spreadDeg,
        w.falloffStartM, w.falloffPerM, w.headshotMultiplier];
    });
    lines.push('sheep.attack=' + g17List(sheepAttack));
    const waveRows = [s.waves.WAVE_MAX, s.waves.WAVE_INTERMISSION_MS, s.waves.WAVE_INTERMISSION_MIN_MS,
      s.waves.BUDGET_SCALE_PER_EXTRA_PLAYER, s.waves.SPEED_SCALE_PER_EXTRA_PLAYER,
      s.waves.MAX_SPAWNS_PER_TICK, s.waves.MAX_ACTIVE_SPAWN_POINTS, s.waves.MIN_SPAWN_DISTANCE_M];
    for (let wave = 1; wave <= s.waves.WAVE_MAX; wave += 1) waveRows.push(s.waves.waveBaseBudget(wave));
    for (let players = 1; players <= 4; players += 1) waveRows.push(s.waves.sheepSpeedMultiplier(players));
    lines.push('waves=' + g17List(waveRows));
    lines.push('waves.scaling=' + g17List([s.waves.waveBudget(1, 4), s.waves.waveBudget(5, 4),
      s.waves.firstWaveFor('ram'), s.waves.firstWaveFor('elite'), s.waves.firstWaveFor('king'),
      s.waves.firstWaveFor('grunt'), s.waves.isBossWave(5) ? 1 : 0, s.waves.isBossWave(4) ? 1 : 0,
      s.director.DIRECTOR_KIND_COUNT]));
  }
  const text = lines.join('\n');
  return { text, hash: crc32cText(text).toString(16).padStart(8, '0') };
}

// ---------------- RNG 状态口径（S07 §5.2） ----------------
function mulberry32Probe(seed) {
  let a = seed >>> 0;
  return {
    next() {
      a = (a + 0x6d2b79f5) >>> 0;
      let t = a;
      t = Math.imul(t ^ (t >>> 15), t | 1);
      t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
      return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
    },
    state() {
      return a >>> 0;
    },
  };
}

const STREAM_ID = { ai: 1, spawn: 2, fx: 3 };

function makeRngProbe(seed) {
  const taps = { ai: 0, spawn: 0, fx: 0 };
  function derivedState(stream) {
    const derived = (Math.imul(seed >>> 0, 2654435761) + STREAM_ID[stream]) >>> 0;
    const probe = mulberry32Probe(derived);
    for (let i = 0; i < taps[stream]; i += 1) probe.next();
    return probe.state();
  }
  function wrap(world, stream) {
    const original = world.rng[stream];
    world.rng[stream] = () => {
      taps[stream] += 1;
      return original();
    };
  }
  return { taps, wrap, stateOf: (stream) => derivedState(stream) };
}

// ---------------- 场景（S07 §5.3；未交付的场景见 docs/evidence/fixtures/README.md §5） ----------------
// §5.1 的命令按钮位表（v1 config/input.ts 的 BUTTON 逐值一致）。
const BUTTON = { fire: 1, sprint: 2, jump: 4, reload: 8, interact: 16, rage: 32, switchWeapon: 64 };

// 初态（setup）随向量一起入库：`createWorld` 之后、第一个 tick 之前执行一次。两侧（本脚本与
// server/tests/fixture_test.cpp 的 createFixtureWorld）读**同一份数据**，不再维护「逐字同表」的隐含约定：
// 先按 id 覆盖玩家初态（hp/armor），再按数组顺序生成羊群。生成原语与 v1 ai/director.ts 的生成路径同形：
// spawnEntity('sheep') + applySheepKind + state=graze（C++ 侧对应 waves::spawnSheepAt）。
function sheepAt(kind, x, z) {
  return { kind, x, z };
}

function playerHp(id, hp, armor) {
  return { id, hp, armor };
}

// 玩家出生点（arena.playerSpawnPoints，id = 下标 + 1）：把 yaw 指向某个世界坐标。
// Math.atan2 已被 patchMath 换成共享整数表，因此这个角度与 C++ 侧同口径。
function aimAtPlayerSpawn(context, id, targetX, targetZ, pitch) {
  const spawn = context.config.arena.playerSpawnPoints[id - 1];
  return { yaw: Math.atan2(targetX - spawn.x, targetZ - spawn.z), pitch };
}

function spawnSheep(world, v1, kind, x, z) {
  const spawned = v1.spawnEntity(world, 'sheep', x, 0, z);
  if (!spawned.ok) throw new Error('spawnEntity(sheep) 失败：' + spawned.reason);
  const entity = v1.getEntity(world, spawned.id);
  v1.applySheepKind(entity, kind);
  entity.state = v1.SHEEP_STATE.graze;
  return spawned.id;
}

function applySetup(world, v1, setup) {
  for (const override of setup.players) {
    const entity = v1.getEntity(world, override.id);
    if (entity === undefined) throw new Error('setup.players：找不到玩家 ' + override.id);
    entity.hp = override.hp;
    entity.armor = override.armor;
  }
  for (const entry of setup.sheep) spawnSheep(world, v1, entry.kind, entry.x, entry.z);
}

// 导演的 playerIds：活动、非 idle 的玩家按 activeIds 升序（v1 sim.ts collectPlayerIds；
// 房间层再叠加「已连接」过滤，向量里没有连接概念）。
function collectPlayerIds(v1, world) {
  const out = [];
  for (const id of world.activeIds) {
    const entity = v1.getEntity(world, id);
    if (entity === undefined || !entity.active || entity.kind !== 'player' || entity.idle) continue;
    out.push(entity.id);
  }
  return out;
}

function normalizeSetup(scenario) {
  const setup = scenario.setup ?? {};
  return { players: setup.players ?? [], sheep: setup.sheep ?? [] };
}

// 关键帧 tick：默认 1 / 25% / 50% / 75% / 100%（末尾必须是关键帧：反例自检改末帧必须能报出字段）。
function keyframeTicksFor(scenario) {
  if (scenario.keyframeTicks !== undefined) return scenario.keyframeTicks.slice();
  const ticks = new Set();
  for (const fraction of [0, 0.25, 0.5, 0.75, 1]) {
    const tick = fraction === 0 ? 1 : Math.ceil(scenario.ticks * fraction);
    ticks.add(Math.max(1, Math.min(scenario.ticks, tick)));
  }
  return [...ticks].sort((a, b) => a - b);
}

const SCENARIOS = [
  {
    name: 'still-60t',
    seed: 7,
    ticks: 60,
    commandsFor(tick, ids) {
      if (tick <= 30) return [];
      return ids.map((id) => ({ id, moveX: 0, moveY: 0, yaw: 0, pitch: 0, buttons: 0 }));
    },
  },
  {
    name: 'straight-line-240t',
    seed: 11,
    ticks: 240,
    commandsFor(tick, ids) {
      const sprint = tick > 120;
      const yaw = sprint ? -PI / 2 : PI / 2;
      return ids.map((id) => ({ id, moveX: 1, moveY: 0, yaw, pitch: 0, buttons: sprint ? 2 : 0 }));
    },
  },
  {
    name: 'barn-collision-400t',
    seed: 13,
    ticks: 400,
    commandsFor(tick, ids) {
      return ids.map((id) => ({ id, moveX: 1, moveY: 0, yaw: PI, pitch: 0, buttons: 0 }));
    },
  },
  {
    name: 'fence-bounds-400t',
    seed: 17,
    ticks: 400,
    commandsFor(tick, ids) {
      return ids.map((id, index) => ({ id, moveX: 1, moveY: 0, yaw: index < 2 ? 0 : -PI / 2, pitch: 0, buttons: 0 }));
    },
  },
  {
    // §5.3 连射命中：1 号玩家第 1 tick 切到 2 号槽（switchWeapon + switchTo=1 → 步枪），
    // tick 2–60 按住开火（rpm 600 → 每 2 tick 一发，正好打空 30 发弹匣）、tick 61–100 换弹（2000ms）、
    // tick 101–120 再开火；目标是一只保持 15–25m 距离的问界羊（会横向移动、会射问号弹）与一只冲上来的咩咩兵。
    // 俯角 -0.03 rad：v1 的射击起点是眼高 1.6m，而羊的命中盒顶只有 1.15–1.29m，平射打不到。
    name: 'rifle-burst-hit-120t',
    seed: 23,
    ticks: 120,
    setup: { sheep: [sheepAt('elite', -4.5, -15), sheepAt('grunt', -9, -2), sheepAt('grunt', 35, -35)] },
    commandsFor(tick, ids) {
      return ids.map((id) => {
        if (id === 1) {
          if (tick === 1) {
            return { id, moveX: 0, moveY: 0, yaw: PI, pitch: -0.03, buttons: BUTTON.switchWeapon, switchTo: 1 };
          }
          const firing = tick <= 60 || tick > 100;
          return { id, moveX: 0, moveY: 0, yaw: PI, pitch: -0.03, buttons: firing ? BUTTON.fire : BUTTON.reload };
        }
        if (id === 2) return { id, moveX: 1, moveY: 0, yaw: 0, pitch: 0, buttons: 0 };
        if (id === 4) return { id, moveX: 1, moveY: 0, yaw: -PI / 2, pitch: 0, buttons: 0 };
        return { id, moveX: 0, moveY: 0, yaw: PI, pitch: 0, buttons: 0 };
      });
    },
  },
  {
    // §5.3 霰弹散布：第 1 tick 切到 3 号槽（switchWeapon + switchTo=2），之后每 tick 按开火；
    // rpm 70 → 60 tick 内只有 3 次击发，每次 8 弹丸，各自带 seq/pellet 派生的抖动（±4°）。
    name: 'shotgun-spread-60t',
    seed: 29,
    ticks: 60,
    setup: { sheep: [sheepAt('grunt', -4.5, -2), sheepAt('grunt', -7, -3)] },
    commandsFor(tick, ids) {
      return ids.map((id) => {
        if (id === 1) {
          if (tick === 1) {
            return { id, moveX: 0, moveY: 0, yaw: PI, pitch: -0.05, buttons: BUTTON.switchWeapon, switchTo: 2 };
          }
          return { id, moveX: 0, moveY: 0, yaw: PI, pitch: -0.05, buttons: BUTTON.fire };
        }
        if (id === 2) return { id, moveX: 1, moveY: 0, yaw: -PI / 2, pitch: 0, buttons: BUTTON.sprint };
        return { id, moveX: 0, moveY: 0, yaw: PI, pitch: 0, buttons: 0 };
      });
    },
  },
  {
    // §5.3 倒地救援：1 号玩家初态 hp=8 / armor=0（README §2 的初态约定）→ 咩咩兵第一口即倒地；
    // 2 号玩家走近后按住 interact 推进救援，中途松手一次（中断 → 5s 后进度清零），再次按住直到完成。
    name: 'downed-revive-140t',
    seed: 31,
    ticks: 140,
    setup: { players: [playerHp(1, 8, 0)], sheep: [sheepAt('grunt', -4.5, 5.8)] },
    commandsFor(tick, ids) {
      return ids.map((id) => {
        if (id === 2) {
          if (tick <= 8) return { id, moveX: 1, moveY: 0, yaw: -PI / 2, pitch: 0, buttons: 0 };
          const holding = tick <= 28 || tick > 33;
          return { id, moveX: 0, moveY: 0, yaw: -PI / 2, pitch: 0, buttons: holding ? BUTTON.interact : 0 };
        }
        return { id, moveX: 0, moveY: 0, yaw: PI, pitch: 0, buttons: 0 };
      });
    },
  },
  {
    // 咩咩兵 AI 与聚集：三只咩咩兵贴在 1 号玩家正面 12–14m 处（视野 35m 内 → 警戒 → 追击 → 撕咬），
    // 彼此间距 2–4m 覆盖分离/对齐/聚拢三项群集权重；远处（57m，视野外）一只全程吃草的咩咩兵抽 `ai` 流。
    name: 'sheep-grunt-ai-600t',
    seed: 37,
    ticks: 600,
    setup: {
      sheep: [sheepAt('grunt', -4.5, -6), sheepAt('grunt', -6.5, -5.5), sheepAt('grunt', -2.5, -6.5),
        sheepAt('grunt', 35, -35)],
    },
    commandsFor() {
      return [];
    },
  },
  {
    // 冲撞羊冲锋：冲撞羊在 15m 处（> RAM_CHARGE_TRIGGER_M=12 → 先追击），进入 12m 后蓄力 → 冲锋 →
    // 撞上静止的 1 号玩家（撞击击退 + 硬直），随后回到追击。远处吃草的咩咩兵负责抽 `ai` 流。
    name: 'sheep-ram-charge-300t',
    seed: 41,
    ticks: 300,
    setup: { sheep: [sheepAt('ram', -4.5, -8), sheepAt('grunt', 35, -35)] },
    commandsFor() {
      return [];
    },
  },
  {
    // 问界羊保距与问号弹：17m 处（eliteKeep 带 15–25m 内）横向换位（ELITE_STRAFE_MS=1200ms）
    // 并按 eliteBoltCooldownMs 射问号弹；静止的 4 名玩家吃弹，覆盖投射物生成/飞行/命中/回收。
    // 注意 v1 的投射物寿命在两侧都被各扣一次（sim.ts 的 aliveMs += dtMs 与 advanceProjectiles 里的
    // 同一段累加），所以 BOLT_LIFE_MS=3000 实际只有 30 tick × 14m/s ≈ 21m 射程 —— 起始距离必须在 21m 内，
    // 否则弹丸在到达玩家前就自毁（本条被记账在 README §5：两侧同口径地"短射程"，不是我们的偏差）。
    name: 'sheep-elite-bolt-300t',
    seed: 43,
    ticks: 300,
    setup: { sheep: [sheepAt('elite', -4.5, -9), sheepAt('grunt', 22, 0)] },
    commandsFor() {
      return [];
    },
  },
  {
    // 羊王阶段：羊王在 (22,2)（玩家 4 距离 18.2m，视野 35m 内；射线不经过谷仓 AABB x∈[-4,4]/z∈[-4,4]），
    // 4 名玩家第 1 tick 切步枪并瞄准羊王，之后同时单发点射压住伤害速率 →
    // 跨过 0.66 / 0.33 两条阶段阈值，且阶段 2 与阶段 3 各活过 1 个召唤周期（8s）。
    name: 'sheep-king-phases-900t',
    seed: 47,
    ticks: 900,
    setup: { sheep: [sheepAt('king', 22, 2), sheepAt('grunt', 35, -35)] },
    keyframeTicks: [1, 450, 900],
    commandsFor(tick, ids, context) {
      if (tick === 1) {
        return ids.map((id) => {
          const aim = aimAtPlayerSpawn(context, id, 22, 2, -0.03);
          return { id, moveX: 0, moveY: 0, yaw: aim.yaw, pitch: aim.pitch, buttons: BUTTON.switchWeapon, switchTo: 1 };
        });
      }
      // 4 人同时单发点射：每 24 tick 全按 1 tick（步枪冷却 2 tick → 各 1 发），
      // 合计 0.167 发/tick → 羊王 900 tick 内走完 1→2→3 三个阶段、且阶段 2 与阶段 3 各活过 8s（两次召唤）。
      // 4 人同相位（而不是错相）是为了让命令脚本折叠成「每 24 tick 两段」，不把体积撑到 64 KiB 以上。
      if ((tick - 3) % 24 !== 0) return [];
      return ids.map((id) => {
        const aim = aimAtPlayerSpawn(context, id, 22, 2, -0.03);
        return { id, moveX: 0, moveY: 0, yaw: aim.yaw, pitch: aim.pitch, buttons: BUTTON.fire };
      });
    },
  },
  {
    // 波次导演（1→5 波）：外部每 tick 驱动（DirectorState 在 stepWorld 之外，README §2；
    // 与 S09/S10 已裁决的 B2 一致）。4 名玩家原地不动、各自从 0°/90°/180°/270° 起每 75 tick 转 22.5°
    // 扫射（12 个生成点四面来敌），按「打 60 tick / 换弹 40 tick」压制 →
    // 覆盖计划波次预算、组队顺序、生成点选择、出生抖动、波次开始事件。
    // 1200 tick 到不了第 5 波（4 次波间 20s = 1600 tick 就已超预算，见 README §5 未交付清单）。
    name: 'wave-director-1to5-1200t',
    seed: 53,
    ticks: 1200,
    director: { startWave: 1 },
    keyframeTicks: [1, 600, 1200],
    commandsFor(tick, ids, context) {
      // 4 名玩家各自从 0°/90°/180°/270° 起，每 75 tick 转 22.5°，1200 tick 转满一圈：
      // 羊从 12 个生成点四面过来，扫射能覆盖到多数来向。命令脚本仍然按 75 tick 折叠。
      const sweep = (2 * PI * Math.floor((tick - 1) / 75)) / 16;
      return ids.map((id, index) => {
        const reloading = (tick - 1) % 100 >= 60;
        return { id, moveX: 0, moveY: 0, yaw: index * (PI / 2) + sweep, pitch: -0.06,
          buttons: reloading ? BUTTON.reload : BUTTON.fire };
      });
    },
  },
  {
    // 快照 round-trip：量化字段「编码 → 解码后位型等价」。1 号玩家切步枪压制、2 号玩家横向走位，
    // 两只咩咩兵冲上来（覆盖 hpRatio / state / kindFlags / 位置 / 角度的量化边界）；
    // 在 5 个关键 tick 上记录「编码字节的哈希 + 解码后投影的哈希」（两侧各自 encode/decode 后比对）。
    name: 'snapshot-roundtrip-240t',
    seed: 59,
    ticks: 240,
    snapshotTicks: [1, 60, 120, 180, 240],
    setup: { sheep: [sheepAt('grunt', -4.5, -6), sheepAt('grunt', -7, -3)] },
    commandsFor(tick, ids) {
      return ids.map((id) => {
        if (id === 1) {
          if (tick === 1) {
            return { id, moveX: 0, moveY: 0, yaw: PI, pitch: -0.03, buttons: BUTTON.switchWeapon, switchTo: 1 };
          }
          const firing = tick <= 60 || tick > 100;
          return { id, moveX: 0, moveY: 0, yaw: PI, pitch: -0.03, buttons: firing ? BUTTON.fire : BUTTON.reload };
        }
        if (id === 2) return { id, moveX: 1, moveY: 0, yaw: -PI / 2, pitch: 0, buttons: BUTTON.sprint };
        return { id, moveX: 0, moveY: 0, yaw: PI, pitch: 0, buttons: 0 };
      });
    },
  },
  {
    // 三流归属：羊王跨阶段（阶段 2 起每 8s 召唤 4 只咩咩兵 → `spawn` 流）+ 远处吃草羊（`ai` 流），
    // 4 名玩家切步枪压制以在 600 tick 内打穿 0.66 阈值；`fx` 流按 ADR-010 §5 不参与模拟（恒 0 次抽取）。
    name: 'rng-streams-600t',
    seed: 61,
    ticks: 600,
    setup: { sheep: [sheepAt('king', 22, 2), sheepAt('grunt', 35, -35), sheepAt('grunt', -35, 35)] },
    commandsFor(tick, ids, context) {
      return ids.map((id) => {
        const aim = aimAtPlayerSpawn(context, id, 22, 2, -0.03);
        const reloading = tick > 1 && (tick - 2) % 100 >= 60;
        if (tick === 1) {
          return { id, moveX: 0, moveY: 0, yaw: aim.yaw, pitch: aim.pitch, buttons: BUTTON.switchWeapon, switchTo: 1 };
        }
        // 同 sheep-king-phases-900t：只有 1 号玩家开火，保证 600 tick 内能等到一次召唤（spawn 流）。
        if (id > 1) return { id, moveX: 0, moveY: 0, yaw: aim.yaw, pitch: aim.pitch, buttons: 0 };
        return { id, moveX: 0, moveY: 0, yaw: aim.yaw, pitch: aim.pitch,
          buttons: reloading ? BUTTON.reload : BUTTON.fire };
      });
    },
  },
];

function toV1Command(command, v1) {
  const raw = v1.createCommand();
  raw.seq = command.seq;
  raw.tick = command.clientTick;  // v1 的 Command.tick 即 clientTick
  raw.moveX = command.moveX;
  raw.moveY = command.moveY;
  raw.yaw = command.yaw;
  raw.pitch = command.pitch;
  raw.buttons = command.buttons;
  raw.switchTo = command.switchTo;
  return raw;
}

// 命令脚本的 RLE 判据：同一 tick 的命令集合逐字段相同就并进上一段（无损，只是折叠重复的 tick）。
function scriptKey(commands) {
  return commands.map((command) => [command.id, g17(command.moveX), g17(command.moveY), g17(command.yaw),
    g17(command.pitch), command.buttons, command.switchTo].join(':')).join('|');
}

// 快照字段组：编码（15 字节实体记录，按 activeIds 顺序拼接）→ 解码，两侧各自算两个哈希。
function snapshotEntry(world, v1, tick) {
  const snapshot = v1.snapshotWorld(world);
  const width = v1.SNAPSHOT_RECORD_BYTES;
  const encoded = new Uint8Array(snapshot.entities.length * width);
  const scratch = new Uint8Array(width);
  const record = v1.createSnapshotMirror().record;
  const decoded = [];
  for (let i = 0; i < snapshot.entities.length; i += 1) {
    v1.quantizeSnapshotEntity(snapshot.entities[i], scratch, 0);
    encoded.set(scratch, i * width);
    v1.readSnapshotRecord(encoded, i * width, record);
    decoded.push({ id: record.id, kind: v1.ENTITY_KIND_CODE[record.kind], flags: record.flags, xCm: record.xCm,
      yCm: record.yCm, zCm: record.zCm, yawUnits: record.yawUnits, pitchUnits: record.pitchUnits,
      hpRatioUnits: record.hpRatioUnits, state: record.state });
  }
  return { tick, records: snapshot.entities.length,
    encodeHash: hash64Hex(fnv1a64(encoded, FNV_OFFSET_BASIS)),
    decodeHash: hash64Hex(fnv1a64Text(snapshotDecodeText(decoded), FNV_OFFSET_BASIS)) };
}

function jsonString(text) {
  if (text.indexOf('"') >= 0 || text.indexOf(String.fromCharCode(92)) >= 0) throw new Error('jsonString: needs escaping');
  return '"' + text + '"';
}

// ---------------- 「对拍向量」的字节口径（README §4） ----------------
// 每 tick 的**全量投影** = 下面这份字段级文本：字段与顺序沿用 v1 冻结 schema（§5.1），double 一律 %.17g。
// 旧盘上 hp / event.value 用 JS 最短往返表示，也是无损的；统一到 %.17g 不降低逐位强度。
// 逐帧哈希链：h_i = fnv1a64(投影_i, h_{i-1})，h_0 = FNV 偏移基准。链上只落 8 字节/tick（16 位十六进制）。
const FNV_OFFSET_BASIS = 0xcbf29ce484222325n;
const FNV_PRIME = 0x100000001b3n;
const U64_MASK = 0xffffffffffffffffn;

function fnv1a64(bytes, seed) {
  let hash = seed & U64_MASK;
  for (let i = 0; i < bytes.length; i += 1) {
    hash = ((hash ^ BigInt(bytes[i])) * FNV_PRIME) & U64_MASK;
  }
  return hash;
}

function fnv1a64Text(text, seed) {
  return fnv1a64(Buffer.from(text, 'utf8'), seed);
}

function hash64Hex(value) {
  return (value & U64_MASK).toString(16).padStart(16, '0');
}

function assertFnvSelfTest() {
  // 公开的 FNV-1a 64 测试向量（外部来源，不是本实现自产）：钉住算法本身，防实现漂移。
  if (fnv1a64Text('', FNV_OFFSET_BASIS) !== FNV_OFFSET_BASIS) throw new Error('fnv self-test: empty');
  if (hash64Hex(fnv1a64Text('a', FNV_OFFSET_BASIS)) !== 'af63dc4c8601ec8c') throw new Error('fnv self-test: a');
  if (fnv1a64Text('foobar', FNV_OFFSET_BASIS) !== 0x85944171f73967e8n) throw new Error('fnv self-test: foobar');
}

function projectionText(tickNumber, commands, entities, events, rng) {
  const lines = [];
  lines.push('tick=' + tickNumber);
  lines.push('dtMs=' + TICK_MS);
  for (const command of commands) {
    lines.push('cmd=' + command.id + ',' + command.seq + ',' + command.clientTick + ',' + g17(command.moveX) + ',' +
      g17(command.moveY) + ',' + g17(command.yaw) + ',' + g17(command.pitch) + ',' + command.buttons + ',' +
      command.switchTo);
  }
  for (const entity of entities) {
    lines.push('ent=' + entity.id + ',' + entity.kind + ',' + g17(entity.pos[0]) + ',' + g17(entity.pos[1]) + ',' +
      g17(entity.pos[2]) + ',' + g17(entity.yaw) + ',' + g17(entity.pitch) + ',' + g17(entity.hp) + ',' + entity.flags);
  }
  for (const event of events) {
    lines.push('evt=' + event.tick + ',' + event.type + ',' + event.flags + ',' + event.subjectId + ',' +
      event.targetId + ',' + g17(event.x) + ',' + g17(event.y) + ',' + g17(event.z) + ',' + g17(event.value));
  }
  lines.push('rng=' + rng.ai + ',' + rng.spawn + ',' + rng.fx);
  return lines.join('\n') + '\n';
}

// 快照字段组（snapshot-roundtrip-240t）：编码字节的哈希 + 解码后投影的哈希。
// 编码口径 = S03 §5.4 的 15 字节实体记录（v1 quantizeSnapshotEntity / C++ net::EntityRecord 1:1）；
// 解码后投影文本逐字段 = id,kind,flags,xCm,yCm,zCm,yawUnits,pitchUnits,hpRatioUnits,state。
function snapshotDecodeText(records) {
  const lines = [];
  for (const record of records) {
    lines.push('rec=' + record.id + ',' + record.kind + ',' + record.flags + ',' + record.xCm + ',' + record.yCm + ',' +
      record.zCm + ',' + record.yawUnits + ',' + record.pitchUnits + ',' + record.hpRatioUnits + ',' + record.state);
  }
  return lines.join('\n') + '\n';
}

function setupPlayerLine(entry) {
  return '{ "id": ' + entry.id + ', "hp": ' + g17(entry.hp) + ', "armor": ' + g17(entry.armor) + ' }';
}

function setupSheepLine(entry) {
  return '{ "kind": ' + jsonString(entry.kind) + ', "x": ' + g17(entry.x) + ', "z": ' + g17(entry.z) + ' }';
}

function entityLine(entity) {
  return '{ "id": ' + entity.id + ', "kind": ' + jsonString(entity.kind) + ', "pos": [' + g17(entity.pos[0]) + ', ' +
    g17(entity.pos[1]) + ', ' + g17(entity.pos[2]) + '], "yaw": ' + g17(entity.yaw) + ', "pitch": ' + g17(entity.pitch) +
    ', "hp": ' + g17(entity.hp) + ', "flags": ' + entity.flags + ' }';
}

function eventLine(event) {
  return '{ "tick": ' + event.tick + ', "type": ' + jsonString(event.type) + ', "flags": ' + event.flags +
    ', "subjectId": ' + event.subjectId + ', "targetId": ' + event.targetId + ', "x": ' + g17(event.x) +
    ', "y": ' + g17(event.y) + ', "z": ' + g17(event.z) + ', "value": ' + g17(event.value) + ' }';
}

// 命令脚本只记「实际发出的玩家命令」：id / 轴 / 角 / 按钮 / 切枪槽；seq 与 clientTick 由 tick 派生
// （seq = tick % 65536，clientTick = tick，与 v1 applyCommands 的槽位语义同源，两侧同式）。
function scriptCommandLine(entry) {
  return '{ "id": ' + entry.id + ', "moveX": ' + g17(entry.moveX) + ', "moveY": ' + g17(entry.moveY) +
    ', "yaw": ' + g17(entry.yaw) + ', "pitch": ' + g17(entry.pitch) + ', "buttons": ' + entry.buttons +
    ', "switchTo": ' + entry.switchTo + ' }';
}

function serializeFixture(fixture) {
  const lines = [];
  lines.push('{');
  lines.push('  "name": ' + jsonString(fixture.name) + ',');
  lines.push('  "version": ' + fixture.version + ',');
  lines.push('  "seed": ' + fixture.seed + ',');
  lines.push('  "dtMs": ' + TICK_MS + ',');
  lines.push('  "configHash": ' + jsonString(fixture.configHash) + ',');
  lines.push('  "ticks": ' + fixture.ticks + ',');
  lines.push('  "setup": {');
  lines.push('    "players": [');
  fixture.setup.players.forEach((entry, index) => {
    lines.push('      ' + setupPlayerLine(entry) + (index + 1 < fixture.setup.players.length ? ',' : ''));
  });
  lines.push('    ],');
  lines.push('    "sheep": [');
  fixture.setup.sheep.forEach((entry, index) => {
    lines.push('      ' + setupSheepLine(entry) + (index + 1 < fixture.setup.sheep.length ? ',' : ''));
  });
  lines.push('    ]');
  lines.push('  },');
  lines.push('  "director": { "startWave": ' + fixture.director.startWave + ' },');
  lines.push('  "script": [');
  fixture.script.forEach((run, runIndex) => {
    lines.push('    {');
    lines.push('      "from": ' + run.from + ',');
    lines.push('      "to": ' + run.to + ',');
    lines.push('      "commands": [');
    run.commands.forEach((command, commandIndex) => {
      lines.push('        ' + scriptCommandLine(command) + (commandIndex + 1 < run.commands.length ? ',' : ''));
    });
    lines.push('      ]');
    lines.push('    }' + (runIndex + 1 < fixture.script.length ? ',' : ''));
  });
  lines.push('  ],');
  lines.push('  "keyframes": [');
  fixture.keyframes.forEach((frame, frameIndex) => {
    lines.push('    {');
    lines.push('      "tick": ' + frame.tick + ',');
    lines.push('      "entities": [');
    frame.entities.forEach((entity, entityIndex) => {
      lines.push('        ' + entityLine(entity) + (entityIndex + 1 < frame.entities.length ? ',' : ''));
    });
    lines.push('      ],');
    lines.push('      "events": [');
    frame.events.forEach((event, eventIndex) => {
      lines.push('        ' + eventLine(event) + (eventIndex + 1 < frame.events.length ? ',' : ''));
    });
    lines.push('      ],');
    lines.push('      "rngState": { "ai": ' + frame.rng.ai + ', "spawn": ' + frame.rng.spawn + ', "fx": ' +
      frame.rng.fx + ' }');
    lines.push('    }' + (frameIndex + 1 < fixture.keyframes.length ? ',' : ''));
  });
  lines.push('  ],');
  lines.push('  "snapshot": [');
  fixture.snapshot.forEach((entry, index) => {
    lines.push('    { "tick": ' + entry.tick + ', "records": ' + entry.records + ', "encodeHash": ' +
      jsonString(entry.encodeHash) + ', "decodeHash": ' + jsonString(entry.decodeHash) + ' }' +
      (index + 1 < fixture.snapshot.length ? ',' : ''));
  });
  lines.push('  ],');
  lines.push('  "hashChain": [');
  fixture.hashChain.forEach((hash, index) => {
    lines.push('    ' + jsonString(hash) + (index + 1 < fixture.hashChain.length ? ',' : ''));
  });
  lines.push('  ]');
  lines.push('}');
  return lines.join('\n') + '\n';
}

// §5.1 的 flags 位表（与 server/tests/fixture_io.hpp 的 kFlag* 同名同值）。charging/fading 只出现在
// v1 的快照位表（net.ts）里，本批不产出 -> 等 S12 的快照 round-trip 场景一起补。
const FLAGS = { downed: 1, rageMode: 2, reloading: 4, charging: 8, fading: 16, idle: 32 };

function flagsOf(entity, nowMs, v1) {
  let flags = 0;
  if (entity.combat.downed.downed) flags |= FLAGS.downed;
  if (v1.isRageActive(entity.combat.rage, nowMs)) flags |= FLAGS.rageMode;
  if (v1.isReloading(entity.weapon)) flags |= FLAGS.reloading;
  if (entity.idle) flags |= FLAGS.idle;
  return flags;
}

function runScenario(scenario, v1, configHash) {
  const world = v1.createWorld(scenario.seed, v1.CONFIG);
  applySetup(world, v1, normalizeSetup(scenario));
  const probe = makeRngProbe(scenario.seed);
  probe.wrap(world, 'ai');
  probe.wrap(world, 'spawn');
  probe.wrap(world, 'fx');

  // 外部每 tick 驱动导演（README §2；S09/S10 已裁决的 B2）：DirectorState 在 stepWorld 之外，
  // stepWorld 之后调用 updateDirector，playerCount = 4、rng = world.rng.spawn、playerIds = 非 idle 玩家升序。
  const director = scenario.director === undefined
    ? null
    : { state: v1.createDirectorState(), startWave: scenario.director.startWave };

  // 命令槽位只覆盖玩家（v1 applyCommands 按升序玩家位置取槽），羊群/投射物不参与命令分发。
  const ids = world.activeIds.filter((id) => {
    const entity = v1.getEntity(world, id);
    return entity !== undefined && entity.kind === 'player';
  });
  const playerCount = ids.length;
  const context = { config: v1.CONFIG, world };
  if (director !== null) v1.planWave(director.state, director.startWave, playerCount);

  const keyframeTicks = new Set(keyframeTicksFor(scenario));
  const snapshotTicks = scenario.snapshotTicks ?? [];
  const script = [];
  const keyframes = [];
  const snapshot = [];
  const hashChain = [];
  const types = {};
  let chain = FNV_OFFSET_BASIS;
  let eventCount = 0;
  const flagSeen = new Set();

  for (let tick = 1; tick <= scenario.ticks; tick += 1) {
    const entries = scenario.commandsFor(tick, ids, context);
    const slots = new Array(ids.length).fill(undefined);
    const commands = [];
    for (const entry of entries) {
      const index = ids.indexOf(entry.id);
      if (index < 0) throw new Error(scenario.name + ': command for unknown id ' + entry.id);
      const command = { id: entry.id, seq: tick % 65536, clientTick: tick, moveX: entry.moveX, moveY: entry.moveY,
        yaw: entry.yaw, pitch: entry.pitch, buttons: entry.buttons, switchTo: entry.switchTo ?? 0 };
      slots[index] = toV1Command(command, v1);
      commands.push(command);
    }
    v1.stepWorld(world, slots, TICK_MS, null);
    if (director !== null) {
      const playerIds = collectPlayerIds(v1, world);
      v1.updateDirector(world, director.state, playerCount, world.rng.spawn, playerIds);
    }

    const entities = [];
    for (const id of world.activeIds) {
      const entity = v1.getEntity(world, id);
      if (entity === undefined) continue;
      const flags = flagsOf(entity, world.timeMs, v1);
      flagSeen.add(flags);
      entities.push({ id, kind: entity.kind, pos: [entity.pos.x, entity.pos.y, entity.pos.z],
        yaw: entity.yaw, pitch: entity.pitch, hp: entity.hp, flags });
    }
    const events = world.events.map((event) => ({ tick: event.tick, type: event.type, flags: event.flags,
      subjectId: event.subjectId, targetId: event.targetId, x: event.x, y: event.y, z: event.z, value: event.value }));
    eventCount += events.length;
    for (const event of events) types[event.type] = (types[event.type] ?? 0) + 1;
    const rng = { ai: probe.stateOf('ai'), spawn: probe.stateOf('spawn'), fx: probe.stateOf('fx') };

    chain = fnv1a64Text(projectionText(tick, commands, entities, events, rng), chain);
    hashChain.push(hash64Hex(chain));
    if (keyframeTicks.has(tick)) keyframes.push({ tick, entities, events, rng });
    if (snapshotTicks.indexOf(tick) >= 0) snapshot.push(snapshotEntry(world, v1, tick));
    if (TRACE !== null && TRACE.name === scenario.name && tick <= TRACE.ticks) {
      if (TRACE_TEXT) {
        console.log('[ptext] t' + tick + '|' + projectionText(tick, commands, entities, events, rng).replace(/\n/g, '|'));
      }
      const dump = world.activeIds.map((id) => {
        const entity = v1.getEntity(world, id);
        if (entity === undefined) return id + '?';
        const kind = entity.kind === 'projectile' ? 'bolt' : entity.kind;
        return kind + id + '@' + g17(entity.pos.x) + ',' + g17(entity.pos.z) + ':hp' + g17(entity.hp) +
          (entity.kind === 'sheep' ? ':st' + entity.state + ':fl' + entity.ai.flock.count + ':v' +
            g17(entity.vel.x) + ',' + g17(entity.vel.z) : ':v' + g17(entity.vel.x) + ',' + g17(entity.vel.z) +
            ':knock' + g17(entity.kind === 'player' ? entity.combat.knockMs : 0));
      }).join(' ');
      console.log('[trace] t' + tick + ' ' + dump + (events.length === 0 ? '' : ' EVT ' +
        events.map((event) => event.type + '(' + event.subjectId + '->' + event.targetId + ',' + g17(event.value) + ')').join(',')) +
        ' rng=' + rng.ai + ',' + rng.spawn + ',' + rng.fx);
    }

    const previous = script[script.length - 1];
    if (previous !== undefined && previous.scriptKey === scriptKey(commands)) {
      previous.to = tick;
    } else {
      script.push({ from: tick, to: tick, commands, scriptKey: scriptKey(commands) });
    }
  }

  const last = keyframes[keyframes.length - 1];
  return {
    fixture: {
      name: scenario.name,
      version: SCHEMA_VERSION,
      seed: scenario.seed,
      ticks: scenario.ticks,
      configHash,
      setup: normalizeSetup(scenario),
      director: { startWave: director === null ? 0 : director.startWave },
      script: script.map((run) => ({ from: run.from, to: run.to, commands: run.commands })),
      keyframes,
      snapshot,
      hashChain,
    },
    evidence: { entities: last.entities.length, events: eventCount, flags: [...flagSeen].sort((a, b) => a - b),
      draws: { ai: probe.taps.ai, spawn: probe.taps.spawn, fx: probe.taps.fx },
      first: last.entities[0], last: last.entities, runs: script.length, snapshotTicks: snapshot.length, types,
      keyframeTrace: keyframes.map((frame) => {
        const sheep = frame.entities.filter((entity) => entity.kind === 'sheep');
        const top = sheep.reduce((best, entity) => (best === null || entity.hp > best.hp ? entity : best), null);
        return frame.tick + ':' + sheep.length + (top === null ? '' : ':' + Math.round(top.hp));
      }),
      director: director === null ? null : { wave: director.state.wave, planned: director.state.planned,
        spawned: director.state.spawned, totalSpawns: director.state.totalSpawns, finished: director.state.finished } },
  };
}

// ---------------- 主流程 ----------------
function usage() {
  console.log('用法：node tools/export-fixtures.mjs [--root <v1 派生副本>] [--out <目录>] [--only <name[,name]>] [--check] [--list] [--allow-patched-copy]');
}

// 「只读源没被写」与「派生副本 == 只读源」都是可判定的：前者比 (文件数, 总字节, 最新 mtime) 指纹，
// 后者逐文件比内容。S07 §7 的 DoD 要求前者，§8 的风险表要求基线只认只读源（副本被改过必须显式放行）。
function listFiles(root) {
  const found = [];
  const walk = (dir) => {
    for (const entry of readdirSync(dir, { withFileTypes: true })) {
      const full = path.join(dir, entry.name);
      if (entry.isDirectory()) walk(full);
      else found.push(full);
    }
  };
  walk(root);
  return found.sort();
}

function sourceFingerprint() {
  const dir = path.join(READONLY_SOURCE, 'packages/shared/src');
  const files = listFiles(dir);
  let bytes = 0;
  let newest = 0;
  for (const file of files) {
    const info = statSync(file);
    bytes += info.size;
    newest = Math.max(newest, info.mtimeMs);
  }
  return { files: files.length, bytes, newest: Math.round(newest) };
}

function compareTrees(copyTree, sourceTree) {
  const copyFiles = listFiles(copyTree);
  const sourceFiles = listFiles(sourceTree);
  if (copyFiles.length !== sourceFiles.length) {
    return '文件数不同：' + copyFiles.length + ' vs ' + sourceFiles.length;
  }
  for (let i = 0; i < sourceFiles.length; i += 1) {
    const relative = path.relative(sourceTree, sourceFiles[i]);
    const counterpart = path.join(copyTree, relative);
    if (!existsSync(counterpart)) return '副本缺文件：' + relative;
    if (!readFileSync(sourceFiles[i]).equals(readFileSync(counterpart))) return '内容不同：' + relative;
  }
  return '';
}

function ensureDerived(root) {
  if (path.resolve(root) === path.resolve(READONLY_SOURCE)) {
    throw new Error('--root 指向只读源仓库，只允许可写派生副本：' + READONLY_SOURCE);
  }
  if (existsSync(path.join(root, V1_ENTRY))) return false;
  if (path.resolve(root) !== path.resolve(DEFAULT_ROOT)) {
    throw new Error('--root 下找不到 ' + V1_ENTRY + '：' + root);
  }
  console.log('[export] 派生副本不存在，从只读源复制：' + READONLY_SOURCE + ' -> ' + root);
  cpSync(READONLY_SOURCE, root, {
    recursive: true,
    filter: (source) => {
      const base = path.basename(source);
      return base !== 'node_modules' && base !== '.git';
    },
  });
  return true;
}

async function main() {
  const opts = parseArgs(process.argv.slice(2));
  if (opts.help) {
    usage();
    return;
  }
  assertCrcSelfTest();
  assertG17SelfTest();
  assertFnvSelfTest();
  if (opts.trace !== null) TRACE = { name: opts.trace, ticks: opts.traceTicks };
  TRACE_TEXT = opts.traceText === true;

  const root = path.resolve(opts.root);
  const before = sourceFingerprint();
  ensureDerived(root);
  const sourceTree = path.join(READONLY_SOURCE, 'packages/shared/src');
  const copyDiff = compareTrees(path.join(root, 'packages/shared/src'), sourceTree);
  if (copyDiff !== '') {
    if (!opts.allowPatchedCopy) {
      throw new Error('派生副本与只读源不一致（' + copyDiff + '）：基线只认 ' + READONLY_SOURCE + '，确实打过补丁时显式加 --allow-patched-copy');
    }
    console.log('[export] 警告：派生副本与只读源不一致（' + copyDiff + '），已由 --allow-patched-copy 放行');
  } else {
    console.log('[export] 派生副本与只读源一致（packages/shared/src，' + listFiles(sourceTree).length + ' 个文件）');
  }
  const outDir = path.resolve(REPO_ROOT, opts.out);
  const trig = makeTrig(JSON.parse(readFileSync(path.join(REPO_ROOT, TRIG_TABLE_PATH), 'utf8')));
  patchMath(trig);

  const v1 = await import(pathToFileURL(path.join(root, V1_ENTRY)).href);
  const configModule = await import(pathToFileURL(path.join(root, V1_CONFIG)).href);
  const rageModule = await import(pathToFileURL(path.join(root, V1_RAGE)).href);
  const weaponModule = await import(pathToFileURL(path.join(root, V1_WEAPON)).href);
const weaponsConfig = await import(pathToFileURL(path.join(root, V1_WEAPONS_CONFIG)).href);
const combatConfig = await import(pathToFileURL(path.join(root, V1_COMBAT_CONFIG)).href);
const sheepConfig = await import(pathToFileURL(path.join(root, V1_SHEEP_CONFIG)).href);
const resolveModule = await import(pathToFileURL(path.join(root, V1_RESOLVE)).href);
// combat/resolve.ts 顶层只导出常量，但导入它会把整条战斗依赖链拉进来（都用打补丁后的 Math）。
s08 = {
  weapons: weaponsConfig.WEAPONS,
  slotOrder: weaponsConfig.WEAPON_SLOT_ORDER,
  w: weaponsConfig,
  c: combatConfig,
  sheepHit: sheepConfig.SHEEP_HIT,
  sheepOrder: sheepConfig.SHEEP_ORDER,
  resolve: resolveModule,
};
const sheepBrainModule = await import(pathToFileURL(path.join(root, V1_SHEEP_BRAIN)).href);
const flockingModule = await import(pathToFileURL(path.join(root, V1_FLOCKING)).href);
const sheepAttackModule = await import(pathToFileURL(path.join(root, V1_SHEEP_ATTACK)).href);
const kingModule = await import(pathToFileURL(path.join(root, V1_KING_PHASES)).href);
const directorModule = await import(pathToFileURL(path.join(root, V1_DIRECTOR)).href);
const codecModule = await import(pathToFileURL(path.join(root, V1_CODEC)).href);
const snapshotModule = await import(pathToFileURL(path.join(root, V1_SNAPSHOT)).href);
const wavesConfig = await import(pathToFileURL(path.join(root, V1_WAVES_CONFIG)).href);
s09 = {
  sheep: sheepConfig,
  order: sheepConfig.SHEEP_ORDER,
  brain: sheepBrainModule,
  flockWeight: flockingModule.FLOCK_WEIGHT,
  attack: sheepAttackModule,
  king: kingModule,
  director: directorModule,
  waves: wavesConfig,
};
  const api = { createWorld: v1.createWorld, createCommand: v1.createCommand, stepWorld: v1.stepWorld,
    getEntity: v1.getEntity, CONFIG: v1.CONFIG, isRageActive: rageModule.isRageActive, isReloading: weaponModule.isReloading,
    collectPlayerIds: (world) => collectPlayerIds(v1, world),
    // 场景 setup 的生成原语（与 v1 ai/director.ts 的生成路径同形；C++ 侧对应 waves::spawnSheepAt）。
    spawnEntity: v1.spawnEntity, applySheepKind: sheepBrainModule.applySheepKind, SHEEP_STATE: sheepConfig.SHEEP_STATE,
    // 波次导演：DirectorState 归驱动方（v1 ai/director.ts；C++ 侧对应 waves::DirectorState）。
    createDirectorState: directorModule.createDirectorState, planWave: directorModule.planWave,
    updateDirector: directorModule.updateDirector,
    // 快照 round-trip：S03 §5.4 的 15 字节实体记录（v1 net/codec.ts 与 C++ net::EntityRecord 1:1）。
    snapshotWorld: snapshotModule.snapshotWorld, quantizeSnapshotEntity: codecModule.quantizeSnapshotEntity,
    readSnapshotRecord: codecModule.readSnapshotRecord, createSnapshotMirror: codecModule.createSnapshotMirror,
    ENTITY_KIND_CODE: v1.ENTITY_KIND_CODE,
    SNAPSHOT_RECORD_BYTES: v1.SNAPSHOT_RECORD_BYTES };

  const hashInfo = configHashText(configModule.CONFIG);
  console.log('[export] root = ' + root);
  console.log('[export] configHash = ' + hashInfo.hash);
  console.log('[export] configHash 覆盖组：');
  for (const line of hashInfo.text.split('\n')) console.log('  ' + line);

  // --only 只跑选中的子集（体积门按**本次渲染的批次**判定，便于逐份核对生成物）。
  const selected = opts.only === null
    ? SCENARIOS
    : SCENARIOS.filter((scenario) => opts.only.indexOf(scenario.name) >= 0);
  if (opts.only !== null && selected.length !== opts.only.length) {
    throw new Error('--only 含未登记的场景名');
  }

  if (opts.list) {
    for (const scenario of selected) {
      const file = path.join(outDir, scenario.name + '.json');
      if (!existsSync(file)) throw new Error('--list：缺文件 ' + file);
      const bytes = readFileSync(file);
      const text = bytes.toString('utf8');
      const parsed = JSON.parse(text);
      console.log(scenario.name + ' bytes=' + bytes.length + ' ticks=' + parsed.ticks +
        ' chain=' + parsed.hashChain.length + ' keyframes=' + parsed.keyframes.length +
        ' sha256=' + createHash('sha256').update(bytes).digest('hex'));
    }
    return;
  }

  const rendered = [];
  let totalBytes = 0;
  for (const scenario of selected) {
    const result = runScenario(scenario, api, hashInfo.hash);
    const text = serializeFixture(result.fixture);
    const evidence = result.evidence;
    console.log('[export] ' + scenario.name + ' ticks=' + scenario.ticks + ' entities=' + evidence.entities +
      ' events=' + evidence.events + ' flags=' + JSON.stringify(evidence.flags) +
      ' draws=ai:' + evidence.draws.ai + ',spawn:' + evidence.draws.spawn + ',fx:' + evidence.draws.fx +
      ' runs=' + evidence.runs + ' snapshots=' + evidence.snapshotTicks +
      ' bytes=' + Buffer.byteLength(text) +
      ' types=' + JSON.stringify(evidence.types) +
      ' kf=' + evidence.keyframeTrace.join(',') +
      (evidence.director === null ? '' : ' director=wave' + evidence.director.wave + '/' + evidence.director.planned +
        ' spawned=' + evidence.director.spawned + ' total=' + evidence.director.totalSpawns) +
      '\n[export]   last=' + evidence.last.map((entity) => entity.id + ':' + entity.kind + ':' + g17(entity.pos[0]) + ',' +
        g17(entity.pos[2]) + ':hp' + g17(entity.hp) + ':f' + entity.flags).join(' '));
    totalBytes += Buffer.byteLength(text);
    rendered.push({ name: scenario.name, text, file: path.join(outDir, scenario.name + '.json') });
  }
  // 体积门先于写盘：门失败时不该在盘上留下超限的生成物。
  for (const item of rendered) {
    const bytes = Buffer.byteLength(item.text);
    if (bytes > SIZE_GATE_FILE_BYTES) {
      throw new Error('单份体积门超限：' + item.name + ' = ' + bytes + ' > ' + SIZE_GATE_FILE_BYTES);
    }
  }
  if (totalBytes > SIZE_GATE_TOTAL_BYTES) {
    throw new Error('本批体积门超限（' + selected.length + ' 份）：' + totalBytes + ' > ' + SIZE_GATE_TOTAL_BYTES);
  }
  let written = 0;
  let identical = 0;
  for (const item of rendered) {
    if (opts.check) {
      if (!existsSync(item.file)) throw new Error('--check：缺文件 ' + item.file);
      const disk = readFileSync(item.file, 'utf8');
      if (disk !== item.text) {
        const diskLines = disk.split('\n');
        const newLines = item.text.split('\n');
        for (let i = 0; i < Math.max(diskLines.length, newLines.length); i += 1) {
          if (diskLines[i] !== newLines[i]) {
            throw new Error('--check：' + item.name + ' 第 ' + (i + 1) + ' 行不同\n  盘上: ' + diskLines[i] + '\n  重算: ' + newLines[i]);
          }
        }
        throw new Error('--check：' + item.name + ' 内容不同（行数一致）');
      }
      identical += 1;
    } else {
      mkdirSync(outDir, { recursive: true });
      writeFileSync(item.file, item.text, 'utf8');
      written += 1;
    }
  }
  if (opts.check) {
    console.log('[export] check ok：' + identical + '/' + selected.length + ' 与盘上逐字节一致');
  } else {
    console.log('[export] 写出 ' + written + ' 份');
  }
  console.log('[export] 本批 ' + selected.length + ' 份 = ' + totalBytes + ' B（单份门 ' + SIZE_GATE_FILE_BYTES +
    ' B / 总量门 ' + SIZE_GATE_TOTAL_BYTES + ' B）');
  // §6 的取证命令是 Get-ChildItem <dir> -Recurse -File | Measure-Object Length -Sum：它会把 S02 的
  // trig-table.json 和本目录的 README.md 也算进来 -> 在这一行复现同一个数字（README §6：**只打印、不设门**）。
  let dirBytes = 0;
  let dirFiles = 0;
  if (existsSync(outDir)) {
    const files = listFiles(outDir);
    dirFiles = files.length;
    for (const file of files) dirBytes += statSync(file).size;
  }
  console.log('[export] --out 目录合计 = ' + dirBytes + ' B / ' + dirFiles + ' 个文件' +
    '（含 trig-table.json 与 README.md；本门只按 14 份 *.json 判定）');
  const after = sourceFingerprint();
  if (after.newest !== before.newest || after.bytes !== before.bytes || after.files !== before.files) {
    throw new Error('只读源仓库被写入了：' + JSON.stringify(before) + ' -> ' + JSON.stringify(after));
  }
  console.log('[export] 只读源未写入：packages/shared/src ' + after.files + ' 文件 / ' + after.bytes + ' B / mtime ' + after.newest);
}

main().catch((error) => {
  console.error('[export] 失败：' + (error && error.message ? error.message : String(error)));
  process.exitCode = 1;
});
