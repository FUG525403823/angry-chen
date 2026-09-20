import {
  SHEEP,
  SHEEP_AI,
  SHEEP_STATE,
  SHEEP_STATE_TRANSITIONS,
  type SheepDef,
  type SheepKind,
  type SheepStateCode,
} from '../config/sheep.ts';
import { rngRange } from '../rng.ts';
import { getEntity, type Entity, type EntityId, type World } from '../world.ts';
import {
  createFlockNeighbors,
  flockForce,
  resetFlockNeighbors,
  type FlockNeighbors,
} from './flocking.ts';
import { arrive, createSteeringOut, obstacleAvoid, seek } from './steering.ts';
import { decayAggro, noteHit, selectTarget } from './targeting.ts';
import { createTargetState, type TargetState } from './sheepState.ts';

export const SHEEP_ALERT_MS = 300;
export const RAM_CHARGE_TRIGGER_M = 12;
export const RAM_CHARGE_MAX_MS = 1600;
export const GRAZE_REPICK_MS = 2500;
export const ELITE_STRAFE_MS = 1200;

export const SHEEP_KIND_CODE = Object.freeze({ grunt: 0, ram: 1, elite: 2, king: 3 } as const);

export interface SheepAiState {
  sheepKind: number;
  target: TargetState;
  timerMs: number;
  cooldownMs: number;
  chargeDirX: number;
  chargeDirZ: number;
  grazeX: number;
  grazeZ: number;
  phase: number;
  summonMs: number;
  summoned: number;
  strafeSign: number;
  flock: FlockNeighbors;
}

export function createSheepAiState(): SheepAiState {
  return {
    sheepKind: SHEEP_KIND_CODE.grunt,
    target: createTargetState(),
    timerMs: 0,
    cooldownMs: 0,
    chargeDirX: 0,
    chargeDirZ: 1,
    grazeX: 0,
    grazeZ: 0,
    phase: 1,
    summonMs: 0,
    summoned: 0,
    strafeSign: 1,
    flock: createFlockNeighbors(SHEEP_AI.maxNeighbors),
  };
}

export function resetSheepAiState(state: SheepAiState): void {
  state.timerMs = 0;
  state.cooldownMs = 0;
  state.chargeDirX = 0;
  state.chargeDirZ = 1;
  state.grazeX = 0;
  state.grazeZ = 0;
  state.phase = 1;
  state.summonMs = 0;
  state.summoned = 0;
  state.strafeSign = 1;
  state.target.aggro.fill(0);
  state.target.targetId = 0;
  state.target.targetDistanceM = 0;
  resetFlockNeighbors(state.flock);
}

export function applySheepKind(entity: Entity, kind: SheepKind): void {
  const def: SheepDef = SHEEP[kind];
  entity.ai.sheepKind = SHEEP_KIND_CODE[kind];
  entity.hp = def.hp;
  entity.maxHp = def.hp;
  entity.armor = 0;
}

export function sheepKindOf(entity: Entity): SheepKind {
  const code = entity.ai.sheepKind;
  return code === SHEEP_KIND_CODE.ram
    ? 'ram'
    : code === SHEEP_KIND_CODE.elite
      ? 'elite'
      : code === SHEEP_KIND_CODE.king
        ? 'king'
        : 'grunt';
}

export function canSheepTransition(from: number, to: number): boolean {
  if (from === to) return true;
  const allowed = SHEEP_STATE_TRANSITIONS[from as SheepStateCode];
  if (allowed === undefined) return false;
  return allowed.indexOf(to as SheepStateCode) >= 0;
}

export function setSheepState(entity: Entity, next: SheepStateCode): boolean {
  if (!canSheepTransition(entity.state, next)) return false;
  entity.state = next;
  return true;
}

export function noteSheepHit(entity: Entity, playerIds: readonly EntityId[], pid: EntityId): void {
  noteHit(entity.ai.target, playerIds, pid);
}

export interface SheepIntent {
  x: number;
  z: number;
  speed: number;
  yaw: number;
}

export function createSheepIntent(): SheepIntent {
  return { x: 0, z: 0, speed: 0, yaw: 0 };
}

const steeringScratch = createSteeringOut();
const avoidScratch = createSteeringOut();
const flockScratch = createSteeringOut();

export function updateSheepIntent(
  world: World,
  entity: Entity,
  playerIds: readonly EntityId[],
  speedMultiplier: number,
  dtMs: number,
  out: SheepIntent,
): SheepIntent {
  const ai = entity.ai;
  const kind = sheepKindOf(entity);
  const def = SHEEP[kind];
  const kingBoost = kind === 'king' && ai.phase >= 3 ? SHEEP_AI.kingPhase3SpeedMultiplier : 1;
  const baseSpeed = def.speed * speedMultiplier * kingBoost;
  out.x = 0;
  out.z = 0;
  out.speed = baseSpeed;
  out.yaw = entity.yaw;

  if (ai.timerMs > 0) ai.timerMs = Math.max(0, ai.timerMs - dtMs);
  if (ai.cooldownMs > 0) ai.cooldownMs = Math.max(0, ai.cooldownMs - dtMs);

  if (entity.state === SHEEP_STATE.dead) {
    out.speed = 0;
    return out;
  }

  if (entity.state === SHEEP_STATE.stagger) {
    if (ai.timerMs <= 0) setSheepState(entity, SHEEP_STATE.chase);
    out.speed = 0;
    return out;
  }

  decayAggro(ai.target, playerIds.length);
  const targetId = selectTarget(ai.target, world, entity, playerIds);
  const target = targetId !== 0 ? getEntity(world, targetId) : undefined;
  const hasTarget =
    target !== undefined && target.active && target.kind === 'player' && target.hp > 0;
  const distance = hasTarget ? ai.target.targetDistanceM : 0;

  if (!hasTarget) {
    if (entity.state !== SHEEP_STATE.graze) setSheepState(entity, SHEEP_STATE.graze);
    if (ai.timerMs <= 0) {
      ai.grazeX =
        entity.pos.x + rngRange(world.rng.ai, -SHEEP_AI.grazeRadiusM, SHEEP_AI.grazeRadiusM);
      ai.grazeZ =
        entity.pos.z + rngRange(world.rng.ai, -SHEEP_AI.grazeRadiusM, SHEEP_AI.grazeRadiusM);
      ai.timerMs = GRAZE_REPICK_MS;
    }
    arrive(steeringScratch, entity.pos.x, entity.pos.z, ai.grazeX, ai.grazeZ, baseSpeed * 0.4, 2);
    blendFlock(entity, steeringScratch);
    obstacleAvoid(
      avoidScratch,
      entity.pos.x,
      entity.pos.z,
      steeringScratch.x,
      steeringScratch.z,
      world.config.arena,
      def.radiusM * 0.5,
    );
    out.x = avoidScratch.x;
    out.z = avoidScratch.z;
    return out;
  }

  const targetX = target.pos.x;
  const targetZ = target.pos.z;

  if (entity.state === SHEEP_STATE.graze || entity.state === SHEEP_STATE.alert) {
    if (setSheepState(entity, SHEEP_STATE.alert)) {
      ai.timerMs = SHEEP_ALERT_MS;
    } else {
      setSheepState(entity, SHEEP_STATE.chase);
    }
    out.speed = 0;
    return out;
  }

  if (kind === 'elite') {
    if (distance < SHEEP_AI.eliteKeepMinM) {
      seek(steeringScratch, entity.pos.x, entity.pos.z, targetX, targetZ, -baseSpeed);
    } else if (distance > SHEEP_AI.eliteKeepMaxM) {
      seek(steeringScratch, entity.pos.x, entity.pos.z, targetX, targetZ, baseSpeed);
    } else {
      const dx = targetX - entity.pos.x;
      const dz = targetZ - entity.pos.z;
      const scale = (baseSpeed * 0.5) / (Math.sqrt(dx * dx + dz * dz) || 1);
      steeringScratch.x = -dz * scale * ai.strafeSign;
      steeringScratch.z = dx * scale * ai.strafeSign;
      if (ai.timerMs <= 0) {
        ai.timerMs = ELITE_STRAFE_MS;
        ai.strafeSign = -ai.strafeSign;
      }
    }
    if (distance <= SHEEP_AI.eliteBoltRangeM) setSheepState(entity, SHEEP_STATE.ranged);
    else if (entity.state === SHEEP_STATE.ranged) setSheepState(entity, SHEEP_STATE.chase);
    blendFlock(entity, steeringScratch);
    obstacleAvoid(
      avoidScratch,
      entity.pos.x,
      entity.pos.z,
      steeringScratch.x,
      steeringScratch.z,
      world.config.arena,
      def.radiusM,
    );
    out.x = avoidScratch.x;
    out.z = avoidScratch.z;
    return out;
  }

  if (kind === 'ram') {
    if (entity.state === SHEEP_STATE.windup) {
      if (ai.timerMs <= 0) {
        if (setSheepState(entity, SHEEP_STATE.charge)) ai.timerMs = RAM_CHARGE_MAX_MS;
      }
      out.speed = 0;
      return out;
    }
    if (entity.state === SHEEP_STATE.charge) {
      const expired = ai.timerMs <= 0;
      const arena = world.config.arena;
      const limit = arena.halfSize - arena.fence.thickness;
      const barn = arena.barn;
      const inBarn =
        entity.pos.x > barn.minX &&
        entity.pos.x < barn.maxX &&
        entity.pos.z > barn.minZ &&
        entity.pos.z < barn.maxZ;
      const outOfBounds = Math.abs(entity.pos.x) > limit || Math.abs(entity.pos.z) > limit;
      if (expired || inBarn || outOfBounds) {
        ai.chargeDirX = 0;
        ai.chargeDirZ = 0;
        if (setSheepState(entity, SHEEP_STATE.stagger)) ai.timerMs = SHEEP_AI.chargeStaggerMs;
        out.speed = 0;
        return out;
      }
      out.x = ai.chargeDirX * SHEEP_AI.chargeSpeedMps;
      out.z = ai.chargeDirZ * SHEEP_AI.chargeSpeedMps;
      out.speed = SHEEP_AI.chargeSpeedMps;
      out.yaw = Math.atan2(ai.chargeDirX, ai.chargeDirZ);
      return out;
    }
    if (distance <= RAM_CHARGE_TRIGGER_M && setSheepState(entity, SHEEP_STATE.windup)) {
      ai.timerMs = SHEEP_AI.chargeWindupMs;
      const dx = targetX - entity.pos.x;
      const dz = targetZ - entity.pos.z;
      const length = Math.sqrt(dx * dx + dz * dz);
      ai.chargeDirX = length > 1e-9 ? dx / length : 0;
      ai.chargeDirZ = length > 1e-9 ? dz / length : 1;
      out.speed = 0;
      out.yaw = Math.atan2(ai.chargeDirX, ai.chargeDirZ);
      return out;
    }
  }

  if (distance <= SHEEP_AI.attackRangeM && (kind === 'grunt' || kind === 'king')) {
    setSheepState(entity, SHEEP_STATE.attack);
    out.speed = 0;
    out.yaw = Math.atan2(targetX - entity.pos.x, targetZ - entity.pos.z);
    return out;
  }
  if (entity.state === SHEEP_STATE.attack && distance > SHEEP_AI.attackRangeM * 1.4) {
    setSheepState(entity, SHEEP_STATE.chase);
  }
  if (entity.state === SHEEP_STATE.ranged) setSheepState(entity, SHEEP_STATE.chase);

  seek(steeringScratch, entity.pos.x, entity.pos.z, targetX, targetZ, baseSpeed);
  out.yaw = Math.atan2(targetX - entity.pos.x, targetZ - entity.pos.z);
  blendFlock(entity, steeringScratch);
  obstacleAvoid(
    avoidScratch,
    entity.pos.x,
    entity.pos.z,
    steeringScratch.x,
    steeringScratch.z,
    world.config.arena,
    def.radiusM,
  );
  out.x = avoidScratch.x;
  out.z = avoidScratch.z;
  return out;
}

function blendFlock(entity: Entity, into: ReturnType<typeof createSteeringOut>): void {
  if (entity.ai.flock.count <= 0) return;
  flockForce(
    flockScratch,
    entity.pos.x,
    entity.pos.z,
    entity.ai.flock,
    Math.sqrt(into.x * into.x + into.z * into.z),
  );
  into.x += flockScratch.x * 0.5;
  into.z += flockScratch.z * 0.5;
}
