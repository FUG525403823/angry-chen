import type { Command } from '../command.ts';
import {
  FRIENDLY_FIRE,
  HIT_PART,
  RAGE,
  REVIVE,
  SHEEP_ELITE_STATE,
  partForHeight,
  partForThresholds,
  type HitPart,
} from '../config/combat.ts';
import { BUTTON } from '../config/input.ts';
import { SHEEP_HIT, SHEEP_ORDER } from '../config/sheep.ts';
import { signedJitter, type WeaponDef } from '../config/weapons.ts';
import { createVec3, wrapAngle, yawPitchToDirection } from '../math.ts';
import { HIT_FLAG } from '../net/protocol.ts';
import { createSampledPose, samplePoseAgo, type PoseHistory } from '../sim/history.ts';
import { pushEvent } from '../sim/events.ts';
import {
  despawnEntity,
  getEntity,
  type Entity,
  type EntityId,
  type EntityKind,
  type World,
} from '../world.ts';
import { computeDamage, createDamageResult } from './damage.ts';
import {
  REVIVE_OUTCOME,
  markDowned,
  reviveRatio,
  reviveStep,
  reviveTo,
  type DownedState,
} from './downed.ts';
import { activateRage, addKillRage, isRageActive, noteCombat, updateRage } from './rage.ts';
import { createRayHit, rayVsAabb, rayVsCapsule } from './raycast.ts';
import { activeWeaponDef, switchSlot, tryFire, tryStartReload, updateWeapon } from './weapon.ts';

export const SHOT_MAX_DISTANCE_M = 100;
export const DEG_TO_RAD = Math.PI / 180;
export const PITCH_LIMIT_RAD = 1.5533;

export interface CombatCounters {
  shotsFired: number;
  hits: number;
  damage: number;
}

export interface CombatContext {
  history: PoseHistory | null;
  counters: CombatCounters | null;
  rewindMsFor: ((shooterId: EntityId) => number) | null;
}

export function createCombatContext(): CombatContext {
  return { history: null, counters: null, rewindMsFor: null };
}

export interface ShotTrace {
  hit: boolean;
  targetId: EntityId;
  targetKind: EntityKind;
  part: HitPart;
  distanceM: number;
  x: number;
  y: number;
  z: number;
}

export function createShotTrace(): ShotTrace {
  return {
    hit: false,
    targetId: 0,
    targetKind: 'sheep',
    part: HIT_PART.torso,
    distanceM: 0,
    x: 0,
    y: 0,
    z: 0,
  };
}

const rayScratch = createRayHit();
const targetHitScratch = createRayHit();
const headHitScratch = createRayHit();
const poseScratch = createSampledPose();
const directionScratch = createVec3();
const damageScratch = createDamageResult();
const traceScratch = createShotTrace();

export function traceRay(
  world: World,
  history: PoseHistory | null,
  shooterId: EntityId,
  originX: number,
  originY: number,
  originZ: number,
  dx: number,
  dy: number,
  dz: number,
  maxDistanceM: number,
  rewindMs: number,
  out: ShotTrace,
): ShotTrace {
  out.hit = false;
  out.targetId = 0;
  out.distanceM = 0;
  out.x = 0;
  out.y = 0;
  out.z = 0;

  const arena = world.config.arena;
  const barn = arena.barn;
  rayVsAabb(
    originX,
    originY,
    originZ,
    dx,
    dy,
    dz,
    barn.minX,
    barn.minY,
    barn.minZ,
    barn.maxX,
    barn.maxY,
    barn.maxZ,
    maxDistanceM,
    rayScratch,
  );
  let bestT = rayScratch.hit ? rayScratch.t : maxDistanceM;

  const ids = world.activeIds;
  const radiusByKind = world.config.entity.radiusByKind;
  const heightByKind = world.config.entity.heightByKind;
  const horizontalSq = dx * dx + dz * dz;
  for (let i = 0; i < ids.length; i += 1) {
    const id = ids[i] ?? 0;
    const target = getEntity(world, id);
    if (target === undefined || !target.active || target.id === shooterId) continue;
    if (target.kind !== 'player' && target.kind !== 'sheep') continue;

    let tx = target.pos.x;
    let ty = target.pos.y;
    let tz = target.pos.z;
    if (history !== null && target.kind === 'player') {
      samplePoseAgo(history, rewindMs, target.id, poseScratch);
      if (poseScratch.found) {
        tx = poseScratch.x;
        ty = poseScratch.y;
        tz = poseScratch.z;
      }
    }

    // 命中体口径：羊用 SHEEP_HIT（= 渲染模型的躯干盒 + 前伸头盒，在羊的局部坐标系里求交），
    // 玩家沿用 ENTITY 的竖直胶囊。两条路径分开写，避免两种口径互相污染。
    const sheepProfile =
      target.kind === 'sheep' ? SHEEP_HIT[SHEEP_ORDER[target.ai.sheepKind] ?? 'grunt'] : undefined;
    if (sheepProfile !== undefined) {
      // 世界 → 羊局部（绕 Y 轴反向旋转；局部 +Z = 羊的正前方）。
      const sinYaw = Math.sin(target.yaw);
      const cosYaw = Math.cos(target.yaw);
      const relX = originX - tx;
      const relZ = originZ - tz;
      const lox = cosYaw * relX - sinYaw * relZ;
      const loz = sinYaw * relX + cosYaw * relZ;
      const ldx = cosYaw * dx - sinYaw * dz;
      const ldz = sinYaw * dx + cosYaw * dz;
      const relY = originY - ty;
      // 廉价早退（不改变命中结果）：盒体包围球之外必然打不中。
      const reach = Math.hypot(sheepProfile.halfWidthM, sheepProfile.halfDepthM);
      if (horizontalSq > 1e-12) {
        const along = ((tx - originX) * dx + (tz - originZ) * dz) / horizontalSq;
        const clamped = along < 0 ? 0 : along > maxDistanceM ? maxDistanceM : along;
        const gapX = originX + dx * clamped - tx;
        const gapZ = originZ + dz * clamped - tz;
        if (gapX * gapX + gapZ * gapZ > reach * reach) continue;
      }
      rayVsAabb(
        lox,
        relY,
        loz,
        ldx,
        dy,
        ldz,
        -sheepProfile.halfWidthM,
        0,
        -sheepProfile.halfDepthM,
        sheepProfile.halfWidthM,
        sheepProfile.topM,
        sheepProfile.halfDepthM,
        maxDistanceM,
        targetHitScratch,
      );
      let hitT = targetHitScratch.hit ? targetHitScratch.t : Number.POSITIVE_INFINITY;
      let part: HitPart = HIT_PART.limb;
      if (targetHitScratch.hit) {
        part = partForThresholds(
          ty,
          sheepProfile.headMinM,
          sheepProfile.torsoMinM,
          originY + dy * hitT,
        );
      }
      rayVsAabb(
        lox,
        relY,
        loz,
        ldx,
        dy,
        ldz,
        -sheepProfile.headHalfWidthM,
        sheepProfile.headMinYM,
        sheepProfile.headMinZM,
        sheepProfile.headHalfWidthM,
        sheepProfile.headMaxYM,
        sheepProfile.headMaxZM,
        maxDistanceM,
        headHitScratch,
      );
      if (headHitScratch.hit && headHitScratch.t < hitT) {
        hitT = headHitScratch.t;
        part = HIT_PART.head;
      }
      if (hitT >= bestT) continue;
      bestT = hitT;
      out.hit = true;
      out.targetId = target.id;
      out.targetKind = target.kind;
      out.part = part;
      out.distanceM = hitT;
      out.x = originX + dx * hitT;
      out.y = originY + dy * hitT;
      out.z = originZ + dz * hitT;
      continue;
    }

    // —— 玩家：ENTITY 口径的竖直胶囊（配合历史姿态回滚）——
    const radius = radiusByKind[target.kind];
    const height = heightByKind[target.kind];
    // 廉价早退（不改变命中结果）：胶囊竖直，水平距离 > 半径必然打不中；
    // 最近可能命中参数 minAlong - radius 已超过当前最优命中时也不可能反超。
    if (horizontalSq > 1e-12) {
      const along = ((tx - originX) * dx + (tz - originZ) * dz) / horizontalSq;
      const clamped = along < 0 ? 0 : along > maxDistanceM ? maxDistanceM : along;
      const gapX = originX + dx * clamped - tx;
      const gapZ = originZ + dz * clamped - tz;
      if (gapX * gapX + gapZ * gapZ > radius * radius) continue;
    }
    const axisY = dy >= 0 ? ty + radius : ty + height - radius;
    const minAlong = (tx - originX) * dx + (axisY - originY) * dy + (tz - originZ) * dz;
    if (minAlong - radius > bestT) continue;
    rayVsCapsule(
      originX,
      originY,
      originZ,
      dx,
      dy,
      dz,
      tx,
      ty + radius,
      tz,
      tx,
      ty + height - radius,
      tz,
      radius,
      maxDistanceM,
      targetHitScratch,
    );
    if (!targetHitScratch.hit || targetHitScratch.t >= bestT) continue;
    bestT = targetHitScratch.t;
    out.hit = true;
    out.targetId = target.id;
    out.targetKind = target.kind;
    out.part = partForHeight(ty, height, targetHitScratch.y);
    out.distanceM = targetHitScratch.t;
    out.x = targetHitScratch.x;
    out.y = targetHitScratch.y;
    out.z = targetHitScratch.z;
  }
  return out;
}

function fireRateMultiplierFor(entity: Entity, nowMs: number): number {
  return isRageActive(entity.combat.rage, nowMs) ? RAGE.fireRateMultiplier : 1;
}

function fireWeapon(
  world: World,
  shooter: Entity,
  command: Command,
  rage: boolean,
  history: PoseHistory | null,
  rewindMs: number,
  counters: CombatCounters | null,
  out: ShotTrace,
): void {
  const def = activeWeaponDef(shooter.weapon);
  if (!tryFire(shooter.weapon, world.timeMs, rage ? RAGE.fireRateMultiplier : 1)) return;
  if (counters !== null) counters.shotsFired += 1;

  const eyeY = shooter.pos.y + world.config.player.eyeHeight;
  const spreadDeg = def.spreadDeg + shooter.weapon.spreadDeg;
  for (let pellet = 0; pellet < def.pellets; pellet += 1) {
    const yawJitter = spreadDeg * signedJitter(command.seq + pellet * 13, 0x9e3) * DEG_TO_RAD;
    const pitchJitter = spreadDeg * signedJitter(command.seq + pellet * 29, 0x51f) * DEG_TO_RAD;
    const pitch = command.pitch + pitchJitter;
    yawPitchToDirection(
      directionScratch,
      wrapAngle(command.yaw + yawJitter),
      pitch > PITCH_LIMIT_RAD
        ? PITCH_LIMIT_RAD
        : pitch < -PITCH_LIMIT_RAD
          ? -PITCH_LIMIT_RAD
          : pitch,
    );
    traceRay(
      world,
      history,
      shooter.id,
      shooter.pos.x,
      eyeY,
      shooter.pos.z,
      directionScratch.x,
      directionScratch.y,
      directionScratch.z,
      SHOT_MAX_DISTANCE_M,
      rewindMs,
      out,
    );
    if (!out.hit) continue;
    applyHit(world, shooter, out, def, rage, counters);
  }
}

function applyHit(
  world: World,
  shooter: Entity,
  trace: ShotTrace,
  def: WeaponDef,
  rage: boolean,
  counters: CombatCounters | null,
): void {
  const target = getEntity(world, trace.targetId);
  if (target === undefined || !target.active) return;
  if (target.id === shooter.id) return;
  if (!FRIENDLY_FIRE && target.team === shooter.team) return;

  const wasDowned = target.kind === 'player' && target.combat.downed.downed;
  const damage = computeDamage(def, trace.part, trace.distanceM, rage, target.armor, damageScratch);
  const total = damage.hpDamage + damage.armorDamage;
  if (counters !== null) {
    counters.hits += 1;
    counters.damage += total;
  }

  target.armor = Math.max(0, target.armor - damage.armorDamage);
  target.hp = Math.max(0, target.hp - damage.hpDamage);
  if (target.kind === 'player') noteCombat(target.combat.rage, world.timeMs);

  let flags = 0;
  if (damage.isHeadshot) flags |= HIT_FLAG.headshot;
  if (wasDowned) flags |= HIT_FLAG.downed;

  let killed = false;
  if (target.hp <= 0) {
    if (target.kind === 'sheep') {
      killed = true;
      flags |= HIT_FLAG.killed;
    } else if (!wasDowned) {
      flags |= HIT_FLAG.killed;
      markDowned(target.combat.downed, world.timeMs);
      pushEvent(
        world,
        'playerDowned',
        0,
        target.id,
        0,
        target.pos.x,
        target.pos.y,
        target.pos.z,
        0,
      );
    }
  }

  pushEvent(world, 'playerHit', flags, shooter.id, target.id, trace.x, trace.y, trace.z, total);

  if (!killed) return;
  const isElite = target.state === SHEEP_ELITE_STATE;
  addKillRage(shooter.combat.rage, isElite, damage.isHeadshot, world.timeMs);
  pushEvent(
    world,
    'sheepKilled',
    flags,
    shooter.id,
    target.id,
    target.pos.x,
    target.pos.y,
    target.pos.z,
    total,
  );
  despawnEntity(world, target.id);
}

function updateRevives(world: World, dtMs: number, nowMs: number): void {
  const ids = world.activeIds;
  for (let i = 0; i < ids.length; i += 1) {
    const victim = getEntity(world, ids[i] ?? 0);
    if (victim === undefined || !victim.active || victim.kind !== 'player') continue;
    const downed = victim.combat.downed;
    if (!downed.downed) continue;

    let reviverId = 0;
    for (let j = 0; j < ids.length; j += 1) {
      const other = getEntity(world, ids[j] ?? 0);
      if (other === undefined || !other.active || other.kind !== 'player') continue;
      if (other.id === victim.id) continue;
      if (other.combat.downed.downed || other.team !== victim.team) continue;
      if (!other.combat.interactHeld) continue;
      const speed = Math.sqrt(other.vel.x * other.vel.x + other.vel.z * other.vel.z);
      if (speed > REVIVE.reviverMaxSpeed) continue;
      const dx = other.pos.x - victim.pos.x;
      const dz = other.pos.z - victim.pos.z;
      if (dx * dx + dz * dz > REVIVE.rangeM * REVIVE.rangeM) continue;
      reviverId = other.id;
      break;
    }

    const outcome = reviveStep(downed, nowMs, dtMs, reviverId, reviverId !== 0);
    const ratio = reviveRatio(downed);
    if (outcome === REVIVE_OUTCOME.progress || outcome === REVIVE_OUTCOME.done) {
      if (
        outcome === REVIVE_OUTCOME.done ||
        ratio - downed.lastEventRatio >= REVIVE.progressEventStepRatio
      ) {
        downed.lastEventRatio = ratio;
        pushEvent(
          world,
          'reviveProgress',
          0,
          reviverId,
          victim.id,
          victim.pos.x,
          victim.pos.y,
          victim.pos.z,
          Math.round(ratio * 1000),
        );
      }
    } else if (outcome === REVIVE_OUTCOME.interrupted) {
      pushEvent(
        world,
        'reviveProgress',
        0,
        0,
        victim.id,
        victim.pos.x,
        victim.pos.y,
        victim.pos.z,
        Math.round(ratio * 1000),
      );
    }

    if (outcome !== REVIVE_OUTCOME.done) continue;
    reviveTo(victim, downed, REVIVE.revivedHpRatio);
    pushEvent(
      world,
      'reviveDone',
      0,
      reviverId,
      victim.id,
      victim.pos.x,
      victim.pos.y,
      victim.pos.z,
      victim.hp,
    );
  }
}

export function reviveDownedForWaveClear(world: World): number {
  let revived = 0;
  const ids = world.activeIds;
  for (let i = 0; i < ids.length; i += 1) {
    const entity = getEntity(world, ids[i] ?? 0);
    if (entity === undefined || !entity.active || entity.kind !== 'player') continue;
    if (!entity.combat.downed.downed) continue;
    reviveTo(entity, entity.combat.downed, REVIVE.waveReviveHpRatio);
    revived += 1;
    pushEvent(
      world,
      'reviveDone',
      0,
      0,
      entity.id,
      entity.pos.x,
      entity.pos.y,
      entity.pos.z,
      entity.hp,
    );
  }
  return revived;
}

export function hasDownedTeammateInRange(world: World, entity: Entity, rangeM: number): boolean {
  const ids = world.activeIds;
  for (let i = 0; i < ids.length; i += 1) {
    const other = getEntity(world, ids[i] ?? 0);
    if (other === undefined || !other.active || other.kind !== 'player') continue;
    if (other.id === entity.id || other.team !== entity.team) continue;
    if (!other.combat.downed.downed) continue;
    const dx = other.pos.x - entity.pos.x;
    const dz = other.pos.z - entity.pos.z;
    if (dx * dx + dz * dz <= rangeM * rangeM) return true;
  }
  return false;
}

export function resolveCombat(
  world: World,
  commands: readonly Command[],
  dtMs: number,
  ctx: CombatContext | null,
): void {
  const history = ctx === null ? null : ctx.history;
  const counters = ctx === null ? null : ctx.counters;
  const rewindMsFor = ctx === null ? null : ctx.rewindMsFor;
  const nowMs = world.timeMs;
  const ids = world.activeIds;

  for (let i = 0; i < ids.length; i += 1) {
    const entity = getEntity(world, ids[i] ?? 0);
    if (entity === undefined || !entity.active || entity.kind !== 'player') continue;
    updateWeapon(entity.weapon, nowMs, dtMs, fireRateMultiplierFor(entity, nowMs));
  }

  let slot = 0;
  for (let i = 0; i < ids.length; i += 1) {
    const entity = getEntity(world, ids[i] ?? 0);
    if (entity === undefined || !entity.active || entity.kind !== 'player') continue;
    const raw = commands[slot];
    slot += 1;
    if (raw === undefined) continue;
    entity.combat.interactHeld = (raw.buttons & BUTTON.interact) !== 0;
    if (entity.combat.downed.downed) continue;

    const rage = isRageActive(entity.combat.rage, nowMs);
    if ((raw.buttons & BUTTON.switchWeapon) !== 0) switchSlot(entity.weapon, raw.switchTo, nowMs);
    if ((raw.buttons & BUTTON.reload) !== 0) tryStartReload(entity.weapon, nowMs);
    if ((raw.buttons & BUTTON.rage) !== 0 && activateRage(entity.combat.rage, nowMs)) {
      pushEvent(
        world,
        'rageActivated',
        0,
        entity.id,
        entity.id,
        entity.pos.x,
        entity.pos.y,
        entity.pos.z,
        RAGE.durationMs,
      );
    }
    if ((raw.buttons & BUTTON.fire) === 0) continue;
    const rewindMs = rewindMsFor === null ? 0 : rewindMsFor(entity.id);
    fireWeapon(world, entity, raw, rage, history, rewindMs, counters, traceScratch);
  }

  for (let i = 0; i < ids.length; i += 1) {
    const entity = getEntity(world, ids[i] ?? 0);
    if (entity === undefined || !entity.active || entity.kind !== 'player') continue;
    updateRage(entity.combat.rage, nowMs, dtMs);
  }

  updateRevives(world, dtMs, nowMs);
}

export function reviveStateOf(entity: Entity): DownedState {
  return entity.combat.downed;
}
