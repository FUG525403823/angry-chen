#!/usr/bin/env node
// S07 §4：从冻结的 v1 实现（packages/shared）导出跨语言对拍向量（fixture）。
// 纪律：源仓库 D:/projects/tmp/angry-chen-bak 全程只读；补丁只作用于可写派生副本；产物只写 --out。
//
// 用法：
//   node tools/export-fixtures.mjs                                 写盘（默认派生副本 + docs/evidence/fixtures）
//   node tools/export-fixtures.mjs --check                         只比较，不写盘（幂等校验）
//   node tools/export-fixtures.mjs --list                          清单：name / 字节数 / SHA256
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
const V1_WAVES_CONFIG = 'packages/shared/src/config/waves.ts';
// S08 的 configHash 分组：导入结果在 main 里填，configHashText 只读它。
let s08 = null;
// S09 的 configHash 分组：同样在 main 里填。
let s09 = null;
const TRIG_TABLE_PATH = 'docs/evidence/fixtures/trig-table.json';
const DEFAULT_OUT = 'docs/evidence/fixtures';
const SIZE_GATE_BYTES = 2 * 1024 * 1024;
const TICK_MS = 50;

function parseArgs(argv) {
  const opts = { root: DEFAULT_ROOT, out: DEFAULT_OUT, check: false, list: false, help: false };
  for (let i = 0; i < argv.length; i += 1) {
    const arg = argv[i];
    if (arg === '--root') opts.root = argv[i += 1];
    else if (arg === '--out') opts.out = argv[i += 1];
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

// ---------------- 场景（S07 §5.3 的移动类 4 份；战斗/AI/波次见 docs/evidence/fixtures/README.md） ----------------
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
];

function toV1Command(entry, tick, v1) {
  const command = v1.createCommand();
  command.seq = tick % 65536;
  command.tick = tick;
  command.moveX = entry.moveX;
  command.moveY = entry.moveY;
  command.yaw = entry.yaw;
  command.pitch = entry.pitch;
  command.buttons = entry.buttons;
  command.switchTo = 0;
  return command;
}

function jsonString(text) {
  if (text.indexOf('"') >= 0 || text.indexOf(String.fromCharCode(92)) >= 0) throw new Error('jsonString: needs escaping');
  return '"' + text + '"';
}

function entityLine(entity) {
  return '{ "id": ' + entity.id + ', "kind": ' + jsonString(entity.kind) + ', "pos": [' + g17(entity.pos[0]) + ', ' +
    g17(entity.pos[1]) + ', ' + g17(entity.pos[2]) + '], "yaw": ' + g17(entity.yaw) + ', "pitch": ' + g17(entity.pitch) +
    ', "hp": ' + entity.hp + ', "flags": ' + entity.flags + ' }';
}

function eventLine(event) {
  return '{ "tick": ' + event.tick + ', "type": ' + jsonString(event.type) + ', "flags": ' + event.flags +
    ', "subjectId": ' + event.subjectId + ', "targetId": ' + event.targetId + ', "value": ' + event.value + ' }';
}

function commandLine(entry) {
  return '{ "id": ' + entry.id + ', "seq": ' + entry.seq + ', "clientTick": ' + entry.clientTick +
    ', "moveX": ' + g17(entry.moveX) + ', "moveY": ' + g17(entry.moveY) + ', "yaw": ' + g17(entry.yaw) +
    ', "pitch": ' + g17(entry.pitch) + ', "buttons": ' + entry.buttons + ', "switchTo": ' + entry.switchTo + ' }';
}

function serializeFixture(fixture) {
  const lines = [];
  lines.push('{');
  lines.push('  "name": ' + jsonString(fixture.name) + ',');
  lines.push('  "seed": ' + fixture.seed + ',');
  lines.push('  "configHash": ' + jsonString(fixture.configHash) + ',');
  lines.push('  "ticks": [');
  fixture.ticks.forEach((tick, tickIndex) => {
    lines.push('    {');
    lines.push('      "dtMs": ' + tick.dtMs + ',');
    lines.push('      "commands": [');
    tick.commands.forEach((command, commandIndex) => {
      lines.push('        ' + commandLine(command) + (commandIndex + 1 < tick.commands.length ? ',' : ''));
    });
    lines.push('      ],');
    lines.push('      "expected": {');
    lines.push('        "entities": [');
    tick.entities.forEach((entity, entityIndex) => {
      lines.push('          ' + entityLine(entity) + (entityIndex + 1 < tick.entities.length ? ',' : ''));
    });
    lines.push('        ],');
    lines.push('        "events": [');
    tick.events.forEach((event, eventIndex) => {
      lines.push('          ' + eventLine(event) + (eventIndex + 1 < tick.events.length ? ',' : ''));
    });
    lines.push('        ],');
    lines.push('        "rngState": { "ai": ' + tick.rng.ai + ', "spawn": ' + tick.rng.spawn + ', "fx": ' + tick.rng.fx + ' }');
    lines.push('      }');
    lines.push('    }' + (tickIndex + 1 < fixture.ticks.length ? ',' : ''));
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
  const probe = makeRngProbe(scenario.seed);
  probe.wrap(world, 'ai');
  probe.wrap(world, 'spawn');
  probe.wrap(world, 'fx');

  const ids = world.activeIds.slice();
  const ticks = [];
  const flagSeen = new Set();
  let eventCount = 0;

  for (let tick = 1; tick <= scenario.ticks; tick += 1) {
    const entries = scenario.commandsFor(tick, ids);
    const slots = new Array(ids.length).fill(undefined);
    const commands = [];
    for (const entry of entries) {
      const index = ids.indexOf(entry.id);
      if (index < 0) throw new Error(scenario.name + ': command for unknown id ' + entry.id);
      const seq = tick % 65536;
      slots[index] = toV1Command(entry, tick, v1);
      commands.push({ id: entry.id, seq, clientTick: tick, moveX: entry.moveX, moveY: entry.moveY,
        yaw: entry.yaw, pitch: entry.pitch, buttons: entry.buttons, switchTo: 0 });
    }
    v1.stepWorld(world, slots, TICK_MS, null);

    const entities = [];
    for (const id of world.activeIds) {
      const entity = v1.getEntity(world, id);
      const flags = flagsOf(entity, world.timeMs, v1);
      flagSeen.add(flags);
      entities.push({ id, kind: entity.kind, pos: [entity.pos.x, entity.pos.y, entity.pos.z],
        yaw: entity.yaw, pitch: entity.pitch, hp: entity.hp, flags });
    }
    const events = world.events.map((event) => ({ tick: event.tick, type: event.type, flags: event.flags,
      subjectId: event.subjectId, targetId: event.targetId, value: event.value }));
    eventCount += events.length;
    ticks.push({ dtMs: TICK_MS, commands, entities, events,
      rng: { ai: probe.stateOf('ai'), spawn: probe.stateOf('spawn'), fx: probe.stateOf('fx') } });
  }

  const last = ticks[ticks.length - 1];
  return {
    fixture: { name: scenario.name, seed: scenario.seed, configHash, ticks },
    evidence: { entities: last.entities.length, events: eventCount, flags: [...flagSeen].sort((a, b) => a - b),
      draws: { ai: probe.taps.ai, spawn: probe.taps.spawn, fx: probe.taps.fx },
      first: last.entities[0], count: last.entities.length },
  };
}

// ---------------- 主流程 ----------------
function usage() {
  console.log('用法：node tools/export-fixtures.mjs [--root <v1 派生副本>] [--out <目录>] [--check] [--list] [--allow-patched-copy]');
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
    getEntity: v1.getEntity, CONFIG: v1.CONFIG, isRageActive: rageModule.isRageActive, isReloading: weaponModule.isReloading };

  const hashInfo = configHashText(configModule.CONFIG);
  console.log('[export] root = ' + root);
  console.log('[export] configHash = ' + hashInfo.hash);
  console.log('[export] configHash 覆盖组：');
  for (const line of hashInfo.text.split('\n')) console.log('  ' + line);

  if (opts.list) {
    for (const scenario of SCENARIOS) {
      const file = path.join(outDir, scenario.name + '.json');
      if (!existsSync(file)) throw new Error('--list：缺文件 ' + file);
      const bytes = readFileSync(file);
      console.log(scenario.name + ' ' + bytes.length + ' ' + createHash('sha256').update(bytes).digest('hex'));
    }
    return;
  }

  const rendered = [];
  let totalBytes = 0;
  for (const scenario of SCENARIOS) {
    const result = runScenario(scenario, api, hashInfo.hash);
    const text = serializeFixture(result.fixture);
    const evidence = result.evidence;
    console.log('[export] ' + scenario.name + ' ticks=' + scenario.ticks + ' entities=' + evidence.entities +
      ' events=' + evidence.events + ' flags=' + JSON.stringify(evidence.flags) +
      ' draws=ai:' + evidence.draws.ai + ',spawn:' + evidence.draws.spawn + ',fx:' + evidence.draws.fx +
      ' last0=(' + g17(evidence.first.pos[0]) + ',' + g17(evidence.first.pos[1]) + ',' + g17(evidence.first.pos[2]) + ')');
    totalBytes += Buffer.byteLength(text);
    rendered.push({ name: scenario.name, text, file: path.join(outDir, scenario.name + '.json') });
  }
  // 体积门先于写盘：门失败时不该在盘上留下超限的生成物。
  if (totalBytes >= SIZE_GATE_BYTES) {
    throw new Error('体积门超限（本批 ' + SCENARIOS.length + ' 份）：' + totalBytes + ' >= ' + SIZE_GATE_BYTES);
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
    console.log('[export] check ok：' + identical + '/' + SCENARIOS.length + ' 与盘上逐字节一致');
  } else {
    console.log('[export] 写出 ' + written + ' 份');
  }
  console.log('[export] 本批 ' + SCENARIOS.length + ' 份 = ' + totalBytes + ' B（体积门 ' + SIZE_GATE_BYTES + ' B）');
  // §6 的取证命令是 Get-ChildItem <dir> -Recurse -File | Measure-Object Length -Sum：它会把 S02 的
  // trig-table.json 和本目录的 README.md 也算进门限 -> 在这一行复现同一个数字（见 README §8.3 第 9 条）。
  let dirBytes = 0;
  let dirFiles = 0;
  if (existsSync(outDir)) {
    const files = listFiles(outDir);
    dirFiles = files.length;
    for (const file of files) dirBytes += statSync(file).size;
  }
  const overGate = dirBytes >= SIZE_GATE_BYTES ? '，超出目录门限 ' + (dirBytes - SIZE_GATE_BYTES) + ' B' : '';
  console.log('[export] --out 目录合计 = ' + dirBytes + ' B / ' + dirFiles + ' 个文件（含 trig-table.json 与 README.md' + overGate + '）');
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
