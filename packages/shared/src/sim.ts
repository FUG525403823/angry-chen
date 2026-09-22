import type { Command } from './command.ts';
import {
  MS_PER_SECOND,
  applyCommandToState,
  collideStatic,
  integrateState,
} from './sim/localStep.ts';
import {
  countActive,
  despawnEntity,
  getEntity,
  radiusOf,
  type Entity,
  type EntityId,
  type World,
} from './world.ts';
import { CONFIG } from './config/index.ts';
import { buildSpatialGrid, createSpatialGrid, type SpatialGrid } from './sim/spatialGrid.ts';
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
/** 每 tick 复用的空间网格（邻居聚集与实体分离共用同一份网格结构，零分配）。 */
const spatialGrid = createSpatialGrid(SHEEP_AI.neighborRadiusM, CONFIG.entity.maxEntities);
/** 分离的半邻域偏移（东/南/东南/西南），保证每对实体只处理一次。 */
const SEPARATION_OFFSETS = new Int8Array([1, 0, 0, 1, 1, 1, -1, 1]);
const SEPARATION_RADIUS_BY_KIND = CONFIG.entity.radiusByKind;
/** 便宜预筛阈值：(最大半径 × 2)²，先于 radiusOf 查表排除远对。 */
const MAX_PAIR_DISTANCE_SQ =
  (2 *
    Math.max(
      SEPARATION_RADIUS_BY_KIND.player,
      SEPARATION_RADIUS_BY_KIND.sheep,
      SEPARATION_RADIUS_BY_KIND.pickup,
      SEPARATION_RADIUS_BY_KIND.projectile,
    )) **
  2;
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
  world.eventCursor = 0;

  applyCommands(world, commands);

  const playerIds = collectPlayerIds(world);
  buildSpatialGrid(spatialGrid, world);
  updateAiIntents(world, dtMs, playerIds, spatialGrid);
  applyAiIntents(world);
  applyKnockback(world, dtMs);

  integrate(world, dtMs / MS_PER_SECOND, dtMs);

  resolveStaticCollisions(world);
  buildSpatialGrid(spatialGrid, world);
  resolveEntitySeparation(world, spatialGrid);

  resolveCombat(world, commands, dtMs, combat);
  resolveSheepAttacks(world, playerIds);
  resolveEliteFire(world, playerIds);
  advanceProjectiles(world, dtMs, playerIds);
  updateKings(world, dtMs);

  world.stats.aliveSheep = countActive(world, 'sheep');
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

export function updateAiIntents(
  world: World,
  dtMs: number,
  playerIds: readonly EntityId[],
  grid: SpatialGrid,
): void {
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
    gatherNeighbors(entity.ai.flock, world, entity, grid);
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

function resolveEntitySeparation(world: World, grid: SpatialGrid): void {
  const passes = world.config.entity.separationPasses;
  const epsilon = world.config.entity.separationEpsilon;
  const ids = world.activeIds;
  const entities = world.entities;
  const cellStart = grid.cellStart;
  const cellItems = grid.cellItems;
  const cols = grid.cols;
  const rows = grid.rows;
  const cells = cols * rows;
  for (let pass = 0; pass < passes; pass += 1) {
    // 半邻域扫描：同格（i<j）+ 东/南/东南/西南四格，每对只处理一次，顺序仍是 id 升序。
    for (let cell = 0; cell < cells; cell += 1) {
      const start = cellStart[cell] ?? 0;
      const end = cellStart[cell + 1] ?? 0;
      if (start >= end) continue;
      const cx = cell % cols;
      const cz = (cell - cx) / cols;
      for (let i = start; i < end; i += 1) {
        const aIndex = cellItems[i];
        if (aIndex === undefined) continue;
        const a = entities[aIndex];
        if (a === undefined || !a.active || a.kind === 'projectile') continue;
        for (let j = i + 1; j < end; j += 1) {
          const bIndex = cellItems[j];
          if (bIndex === undefined) continue;
          separatePair(a, entities[bIndex], epsilon);
        }
        for (let o = 0; o < SEPARATION_OFFSETS.length; o += 2) {
          const nx = cx + (SEPARATION_OFFSETS[o] ?? 0);
          const nz = cz + (SEPARATION_OFFSETS[o + 1] ?? 0);
          if (nx < 0 || nz < 0 || nx >= cols || nz >= rows) continue;
          const neighbor = nz * cols + nx;
          const neighborEnd = cellStart[neighbor + 1] ?? 0;
          for (let j = cellStart[neighbor] ?? 0; j < neighborEnd; j += 1) {
            const bIndex = cellItems[j];
            if (bIndex === undefined) continue;
            separatePair(a, entities[bIndex], epsilon);
          }
        }
      }
    }
    for (let i = 0; i < ids.length; i += 1) {
      const entity = getEntity(world, ids[i] ?? 0);
      if (entity === undefined || !entity.active || entity.kind === 'projectile') continue;
      collideStaticTracked(world, entity);
    }
  }
}

/** 单对分离：n 指向 a→b，按重叠量的一半互推（公式与逐对循环一致；结果对调换 a/b 对称）。 */
function separatePair(a: Entity, b: Entity | undefined, epsilon: number): void {
  if (b === undefined || !b.active || b.kind === 'projectile') return;
  let dx = b.pos.x - a.pos.x;
  let dz = b.pos.z - a.pos.z;
  const distanceSq = dx * dx + dz * dz;
  if (distanceSq >= MAX_PAIR_DISTANCE_SQ) return;
  const minDistance =
    (SEPARATION_RADIUS_BY_KIND[a.kind] ?? 0) + (SEPARATION_RADIUS_BY_KIND[b.kind] ?? 0);
  if (distanceSq >= minDistance * minDistance) return;
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
