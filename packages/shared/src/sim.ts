import type { Command } from './command.ts';
import {
  MS_PER_SECOND,
  applyCommandToState,
  collideStatic,
  integrateState,
} from './sim/localStep.ts';
import { getEntity, radiusOf, type Entity, type World } from './world.ts';
import { RAGE, REVIVE } from './config/combat.ts';
import { isRageActive } from './combat/rage.ts';
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

  // 槽位 2：AI 与羊群意图求解（P07 填充）

  integrate(world, dtMs / MS_PER_SECOND, dtMs);

  resolveStaticCollisions(world);
  resolveEntitySeparation(world);

  resolveCombat(world, commands, dtMs, combat);
  // 槽位 6：事件由产生既成事实的槽位写入 world.events
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

function resolveStaticCollisions(world: World): void {
  const ids = world.activeIds;
  for (let i = 0; i < ids.length; i += 1) {
    const entity = getEntity(world, ids[i] ?? 0);
    if (entity === undefined || !entity.active) continue;
    if (entity.kind === 'projectile') continue;
    collideStatic(entity, world.config.arena, radiusOf(world, entity.kind));
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
      }
    }
    for (let i = 0; i < ids.length; i += 1) {
      const entity = getEntity(world, ids[i] ?? 0);
      if (entity === undefined || !entity.active || entity.kind === 'projectile') continue;
      collideStatic(entity, world.config.arena, radiusOf(world, entity.kind));
    }
  }
}
