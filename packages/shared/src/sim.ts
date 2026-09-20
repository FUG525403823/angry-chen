import { BUTTON } from './config/index.ts';
import { sanitizeCommand, type Command } from './command.ts';
import { getEntity, radiusOf, type Entity, type World } from './world.ts';

const msPerSecond = 1000;

export function stepWorld(world: World, commands: readonly Command[], dtMs: number): void {
  world.tick += 1;
  world.timeMs += dtMs;
  world.events.length = 0;

  applyCommands(world, commands);

  // 槽位 2：AI 与羊群意图求解（P07 填充）

  integrate(world, dtMs / msPerSecond, dtMs);

  resolveStaticCollisions(world);
  resolveEntitySeparation(world);

  // 槽位 5：战斗、投射物与伤害（P06 填充）
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
    if (raw === undefined) {
      entity.vel.x = 0;
      entity.vel.y = 0;
      entity.vel.z = 0;
      continue;
    }

    const command = sanitizeCommand(raw, world.commandScratch);
    entity.yaw = command.yaw;
    entity.pitch = command.pitch;

    const player = world.config.player;
    const speed = (command.buttons & BUTTON.sprint) !== 0 ? player.sprintSpeed : player.moveSpeed;
    const forwardX = Math.sin(command.yaw);
    const forwardZ = Math.cos(command.yaw);
    const rightX = Math.cos(command.yaw);
    const rightZ = -Math.sin(command.yaw);

    let vx = forwardX * command.moveX + rightX * command.moveY;
    let vz = forwardZ * command.moveX + rightZ * command.moveY;
    const magnitudeSq = vx * vx + vz * vz;
    if (magnitudeSq > 1) {
      const inverse = 1 / Math.sqrt(magnitudeSq);
      vx *= inverse;
      vz *= inverse;
    }

    entity.vel.x = vx * speed;
    entity.vel.y = 0;
    entity.vel.z = vz * speed;
  }
}

function integrate(world: World, dtSeconds: number, dtMs: number): void {
  const ids = world.activeIds;
  for (let i = 0; i < ids.length; i += 1) {
    const entity = getEntity(world, ids[i] ?? 0);
    if (entity === undefined || !entity.active) continue;
    entity.aliveMs += dtMs;
    entity.pos.x += entity.vel.x * dtSeconds;
    entity.pos.y += entity.vel.y * dtSeconds;
    entity.pos.z += entity.vel.z * dtSeconds;
  }
}

function resolveStaticCollisions(world: World): void {
  const ids = world.activeIds;
  for (let i = 0; i < ids.length; i += 1) {
    const entity = getEntity(world, ids[i] ?? 0);
    if (entity === undefined || !entity.active) continue;
    if (entity.kind === 'projectile') continue;
    resolveBarn(world, entity);
    clampToBounds(world, entity);
  }
}

function resolveBarn(world: World, entity: Entity): void {
  const barn = world.config.arena.barn;
  if (entity.pos.y >= barn.maxY) return;
  const radius = radiusOf(world, entity.kind);
  const minX = barn.minX - radius;
  const maxX = barn.maxX + radius;
  const minZ = barn.minZ - radius;
  const maxZ = barn.maxZ + radius;
  if (entity.pos.x <= minX || entity.pos.x >= maxX) return;
  if (entity.pos.z <= minZ || entity.pos.z >= maxZ) return;

  const pushLeft = entity.pos.x - minX;
  const pushRight = maxX - entity.pos.x;
  const pushBack = entity.pos.z - minZ;
  const pushForward = maxZ - entity.pos.z;
  const penetrationX = Math.min(pushLeft, pushRight);
  const penetrationZ = Math.min(pushBack, pushForward);

  if (penetrationX <= penetrationZ) {
    entity.pos.x = pushLeft < pushRight ? minX : maxX;
    entity.vel.x = 0;
  } else {
    entity.pos.z = pushBack < pushForward ? minZ : maxZ;
    entity.vel.z = 0;
  }
}

function clampToBounds(world: World, entity: Entity): void {
  const arena = world.config.arena;
  const limit = arena.halfSize - arena.fence.thickness / 2 - radiusOf(world, entity.kind);
  if (entity.pos.x > limit) {
    entity.pos.x = limit;
    entity.vel.x = 0;
  } else if (entity.pos.x < -limit) {
    entity.pos.x = -limit;
    entity.vel.x = 0;
  }
  if (entity.pos.z > limit) {
    entity.pos.z = limit;
    entity.vel.z = 0;
  } else if (entity.pos.z < -limit) {
    entity.pos.z = -limit;
    entity.vel.z = 0;
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
      resolveBarn(world, entity);
      clampToBounds(world, entity);
    }
  }
}
