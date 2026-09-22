import type { Command } from './command.ts';
import {
  MS_PER_SECOND,
  applyCommandToState,
  collideStatic,
  integrateState,
} from './sim/localStep.ts';
import {
  despawnEntity,
  getEntity,
  radiusOf,
  type Entity,
  type EntityId,
  type World,
} from './world.ts';
import { RAGE, REVIVE } from './config/combat.ts';
import { isRageActive } from './combat/rage.ts';
import { SHEEP_AI, SHEEP_STATE } from './config/sheep.ts';
import { sheepSpeedMultiplier } from './config/waves.ts';
import { gatherNeighbors } from './ai/flocking.ts';
import {
  SHEEP_KIND_CODE,
  createSheepIntent,
  updateSheepIntent,
  type SheepIntent,
} from './ai/sheepBrain.ts';
import { advanceProjectiles, resolveEliteFire, resolveSheepAttacks } from './ai/sheepAttack.ts';
import { updateKing } from './ai/kingPhases.ts';

const playerIdScratch: EntityId[] = [];
const intentIds: EntityId[] = [];
const intentPool: SheepIntent[] = [];
import { BUTTON } from './config/input.ts';
import { hasDownedTeammateInRange, resolveCombat, type CombatContext } from './combat/resolve.ts';

export function stepWorld(
  world: World,
  commands: readonly Command[],
  dtMs: number,
  combat: CombatContext | null = null,
): void {
  world.tick += 1;
  world.timeMs += dtMs;
  world.events.length = 0;

  applyCommands(world, commands);

  const playerIds = collectPlayerIds(world);
  updateAiIntents(world, dtMs, playerIds);
  applyAiIntents(world);
  applyKnockback(world, dtMs);

  integrate(world, dtMs / MS_PER_SECOND, dtMs);

  resolveStaticCollisions(world);
  resolveEntitySeparation(world);

  resolveCombat(world, commands, dtMs, combat);
  resolveSheepAttacks(world, playerIds);
  resolveEliteFire(world, playerIds);
  advanceProjectiles(world, dtMs, playerIds);
  updateKings(world, dtMs);
}

function applyCommands(world: World, commands: readonly Command[]): void {
  const ids = world.activeIds;
  let slot = 0;
  for (let i = 0; i < ids.length; i += 1) {
    const entity = getEntity(world, ids[i] ?? 0);
    if (entity === undefined || !entity.active || entity.kind !== 'player') continue;

    const raw = commands[slot];
    slot += 1;
    applyCommandToState(
      entity,
      raw,
      world.config.player,
      world.commandScratch,
      isRageActive(entity.combat.rage, world.timeMs) ? RAGE.moveSpeedMultiplier : 1,
    );
    if (entity.combat.downed.downed) {
      entity.vel.x = 0;
      entity.vel.y = 0;
      entity.vel.z = 0;
      continue;
    }
    if (
      raw !== undefined &&
      (raw.buttons & BUTTON.interact) !== 0 &&
      hasDownedTeammateInRange(world, entity, REVIVE.rangeM)
    ) {
      clampHorizontalSpeed(entity, REVIVE.reviverMaxSpeed);
    }
  }
}

export function collectPlayerIds(world: World): EntityId[] {
  playerIdScratch.length = 0;
  const ids = world.activeIds;
  for (let i = 0; i < ids.length; i += 1) {
    const entity = getEntity(world, ids[i] ?? 0);
    if (entity === undefined || !entity.active || entity.kind !== 'player') continue;
    if (entity.idle) continue;
    playerIdScratch.push(entity.id);
  }
  return playerIdScratch;
}

function intentAt(index: number): SheepIntent {
  while (intentPool.length <= index) intentPool.push(createSheepIntent());
  return intentPool[index] as SheepIntent;
}

export function updateAiIntents(world: World, dtMs: number, playerIds: readonly EntityId[]): void {
  const speedMultiplier = sheepSpeedMultiplier(playerIds.length);
  intentIds.length = 0;
  let count = 0;
  const ids = world.activeIds;
  for (let i = 0; i < ids.length; i += 1) {
    const entity = getEntity(world, ids[i] ?? 0);
    if (entity === undefined || !entity.active || entity.kind !== 'sheep') continue;
    if (entity.state === SHEEP_STATE.dead) {
      entity.ai.timerMs += dtMs;
      continue;
    }
    gatherNeighbors(entity.ai.flock, world, entity);
    const intent = intentAt(count);
    updateSheepIntent(world, entity, playerIds, speedMultiplier, dtMs, intent);
    intentIds.push(entity.id);
    count += 1;
  }
}

export function applyAiIntents(world: World): void {
  for (let i = 0; i < intentIds.length; i += 1) {
    const entity = getEntity(world, intentIds[i] ?? 0);
    if (entity === undefined || !entity.active || entity.kind !== 'sheep') continue;
    if (entity.state === SHEEP_STATE.dead) {
      entity.vel.x = 0;
      entity.vel.y = 0;
      entity.vel.z = 0;
      if (entity.ai.timerMs >= SHEEP_AI.deadFadeMs) despawnEntity(world, entity.id);
      continue;
    }
    const intent = intentPool[i];
    if (intent === undefined) continue;
    entity.vel.x = intent.x;
    entity.vel.z = intent.z;
    entity.vel.y = 0;
    entity.yaw = intent.yaw;
  }
}

export function applyKnockback(world: World, dtMs: number): void {
  const dtSeconds = dtMs / MS_PER_SECOND;
  const ids = world.activeIds;
  for (let i = 0; i < ids.length; i += 1) {
    const entity = getEntity(world, ids[i] ?? 0);
    if (entity === undefined || !entity.active || entity.kind !== 'player') continue;
    if (entity.combat.knockMs <= 0) continue;
    entity.combat.knockMs = Math.max(0, entity.combat.knockMs - dtMs);
    if (entity.combat.downed.downed) continue;
    entity.vel.x = entity.combat.knockVx;
    entity.vel.z = entity.combat.knockVz;
    entity.vel.y = 0;
    noteDerivedMove(entity, entity.combat.knockVx * dtSeconds, entity.combat.knockVz * dtSeconds);
  }
}

export function updateKings(world: World, dtMs: number): number {
  let spawned = 0;
  const ids = world.activeIds;
  for (let i = 0; i < ids.length; i += 1) {
    const entity = getEntity(world, ids[i] ?? 0);
    if (entity === undefined || !entity.active || entity.kind !== 'sheep') continue;
    if (entity.ai.sheepKind !== SHEEP_KIND_CODE.king) continue;
    spawned += updateKing(world, entity, dtMs);
  }
  return spawned;
}

/**
 * 记账：本 tick 由权威模拟自身（击退 / 实体分离 / 静态碰撞外推）产生的水平位移。
 * 新增位移来源必须走这里，否则姿态校验会把它当成客户端非法位移。
 */
export function noteDerivedMove(entity: Entity, dx: number, dz: number): void {
  entity.derivedMoveX += dx;
  entity.derivedMoveZ += dz;
}

function clampHorizontalSpeed(entity: Entity, limit: number): void {
  const vx = entity.vel.x;
  const vz = entity.vel.z;
  const speed = Math.sqrt(vx * vx + vz * vz);
  if (speed <= limit || speed === 0) return;
  const scale = limit / speed;
  entity.vel.x = vx * scale;
  entity.vel.z = vz * scale;
}

function integrate(world: World, dtSeconds: number, dtMs: number): void {
  const ids = world.activeIds;
  for (let i = 0; i < ids.length; i += 1) {
    const entity = getEntity(world, ids[i] ?? 0);
    if (entity === undefined || !entity.active) continue;
    entity.aliveMs += dtMs;
    integrateState(entity, dtSeconds);
  }
}

function collideStaticTracked(world: World, entity: Entity): void {
  const beforeX = entity.pos.x;
  const beforeZ = entity.pos.z;
  collideStatic(entity, world.config.arena, radiusOf(world, entity.kind));
  noteDerivedMove(entity, entity.pos.x - beforeX, entity.pos.z - beforeZ);
}

function resolveStaticCollisions(world: World): void {
  const ids = world.activeIds;
  for (let i = 0; i < ids.length; i += 1) {
    const entity = getEntity(world, ids[i] ?? 0);
    if (entity === undefined || !entity.active) continue;
    if (entity.kind === 'projectile') continue;
    collideStaticTracked(world, entity);
  }
}

function resolveEntitySeparation(world: World): void {
  const passes = world.config.entity.separationPasses;
  const epsilon = world.config.entity.separationEpsilon;
  const ids = world.activeIds;
  for (let pass = 0; pass < passes; pass += 1) {
    for (let i = 0; i < ids.length; i += 1) {
      const a = getEntity(world, ids[i] ?? 0);
      if (a === undefined || !a.active || a.kind === 'projectile') continue;
      for (let j = i + 1; j < ids.length; j += 1) {
        const b = getEntity(world, ids[j] ?? 0);
        if (b === undefined || !b.active || b.kind === 'projectile') continue;
        const minDistance = radiusOf(world, a.kind) + radiusOf(world, b.kind);
        let dx = b.pos.x - a.pos.x;
        let dz = b.pos.z - a.pos.z;
        const distanceSq = dx * dx + dz * dz;
        if (distanceSq >= minDistance * minDistance) continue;

        let distance = Math.sqrt(distanceSq);
        if (distance < epsilon) {
          dx = 1;
          dz = 0;
          distance = 0;
        }
        const inverse = distance === 0 ? 1 : 1 / distance;
        const nx = dx * inverse;
        const nz = dz * inverse;
        const half = (minDistance - distance) / 2;
        a.pos.x -= nx * half;
        a.pos.z -= nz * half;
        b.pos.x += nx * half;
        b.pos.z += nz * half;
        noteDerivedMove(a, -nx * half, -nz * half);
        noteDerivedMove(b, nx * half, nz * half);
      }
    }
    for (let i = 0; i < ids.length; i += 1) {
      const entity = getEntity(world, ids[i] ?? 0);
      if (entity === undefined || !entity.active || entity.kind === 'projectile') continue;
      collideStaticTracked(world, entity);
    }
  }
}
