import { HIT_PART } from '../config/combat.ts';
import {
  SHEEP,
  SHEEP_AI,
  SHEEP_ATTACK_PROFILE,
  SHEEP_STATE,
  type SheepKind,
} from '../config/sheep.ts';
import { computeDamage, createDamageResult } from '../combat/damage.ts';
import { markDowned } from '../combat/downed.ts';
import { HIT_FLAG } from '../net/protocol.ts';
import {
  despawnEntity,
  getEntity,
  radiusOf,
  spawnEntity,
  type Entity,
  type EntityId,
  type World,
} from '../world.ts';
import { sheepKindOf } from './sheepBrain.ts';

export const BOLT_LIFE_MS = 3000;
export const BOLT_RADIUS_M = 0.22;
export const BITE_KNOCKBACK_M = 1.5;
export const CHARGE_KNOCKBACK_M = 3;

const damageScratch = createDamageResult();

export function pushEvent(
  world: World,
  type: World['events'][number]['type'],
  flags: number,
  subjectId: EntityId,
  targetId: EntityId,
  x: number,
  y: number,
  z: number,
  value: number,
): void {
  world.events.push({ type, tick: world.tick, flags, subjectId, targetId, x, y, z, value });
}

export function knockbackPlayer(
  target: Entity,
  fromX: number,
  fromZ: number,
  distanceM: number,
): void {
  const dx = target.pos.x - fromX;
  const dz = target.pos.z - fromZ;
  const length = Math.sqrt(dx * dx + dz * dz);
  const speed = SHEEP_AI.knockbackVelocityMps;
  target.combat.knockMs = Math.max(target.combat.knockMs, (distanceM / speed) * 1000);
  target.combat.knockVx = length > 1e-9 ? (dx / length) * speed : 0;
  target.combat.knockVz = length > 1e-9 ? (dz / length) * speed : speed;
}

export function damagePlayer(
  world: World,
  attacker: Entity,
  kind: SheepKind,
  target: Entity,
  distanceM: number,
  knockbackM: number,
): number {
  computeDamage(
    SHEEP_ATTACK_PROFILE[kind],
    HIT_PART.torso,
    distanceM,
    false,
    target.armor,
    damageScratch,
  );
  const armorDamage = damageScratch.armorDamage;
  const hpDamage = damageScratch.hpDamage;
  target.armor = target.armor > armorDamage ? target.armor - armorDamage : 0;
  target.hp = target.hp > hpDamage ? target.hp - hpDamage : 0;

  let flags = 0;
  if (target.hp <= 0 && !target.combat.downed.downed) {
    flags |= HIT_FLAG.downed | HIT_FLAG.killed;
    markDowned(target.combat.downed, world.timeMs);
    pushEvent(
      world,
      'playerDowned',
      0,
      attacker.id,
      target.id,
      target.pos.x,
      target.pos.y,
      target.pos.z,
      0,
    );
  }
  pushEvent(
    world,
    'playerHit',
    flags,
    attacker.id,
    target.id,
    target.pos.x,
    target.pos.y + 0.9,
    target.pos.z,
    damageScratch.hpDamage + damageScratch.armorDamage,
  );
  if (knockbackM > 0 && target.hp > 0)
    knockbackPlayer(target, attacker.pos.x, attacker.pos.z, knockbackM);
  return damageScratch.hpDamage + damageScratch.armorDamage;
}

export function resolveSheepAttacks(world: World, playerIds: readonly EntityId[]): number {
  let events = 0;
  for (let i = 0; i < world.activeIds.length; i += 1) {
    const attacker = getEntity(world, world.activeIds[i] ?? 0);
    if (attacker === undefined || !attacker.active || attacker.kind !== 'sheep') continue;
    const kind = sheepKindOf(attacker);
    if (attacker.state === SHEEP_STATE.charge) {
      for (let p = 0; p < playerIds.length; p += 1) {
        const player = getEntity(world, playerIds[p] ?? 0);
        if (player === undefined || !player.active || player.hp <= 0) continue;
        const dx = player.pos.x - attacker.pos.x;
        const dz = player.pos.z - attacker.pos.z;
        const reach = radiusOf(world, 'sheep') + radiusOf(world, 'player') + 0.15;
        if (dx * dx + dz * dz > reach * reach) continue;
        damagePlayer(
          world,
          attacker,
          'ram',
          player,
          Math.sqrt(dx * dx + dz * dz),
          CHARGE_KNOCKBACK_M,
        );
        events += 2;
        attacker.state = SHEEP_STATE.stagger;
        attacker.ai.timerMs = SHEEP_AI.chargeStaggerMs;
        attacker.ai.chargeDirX = 0;
        attacker.ai.chargeDirZ = 0;
        break;
      }
      continue;
    }
    if (attacker.state !== SHEEP_STATE.attack) continue;
    const ai = attacker.ai;
    if (ai.cooldownMs > 0) continue;
    for (let p = 0; p < playerIds.length; p += 1) {
      const player = getEntity(world, playerIds[p] ?? 0);
      if (player === undefined || !player.active || player.hp <= 0) continue;
      const dx = player.pos.x - attacker.pos.x;
      const dz = player.pos.z - attacker.pos.z;
      const reach = SHEEP_AI.attackRangeM + radiusOf(world, 'sheep') + radiusOf(world, 'player');
      if (dx * dx + dz * dz > reach * reach) continue;
      damagePlayer(
        world,
        attacker,
        kind === 'king' ? 'king' : 'grunt',
        player,
        Math.sqrt(dx * dx + dz * dz),
        BITE_KNOCKBACK_M,
      );
      events += 2;
      ai.cooldownMs = SHEEP_AI.attackCooldownMs;
      break;
    }
  }
  return events;
}

export function spawnBolt(world: World, shooter: Entity, target: Entity): EntityId {
  const dx = target.pos.x - shooter.pos.x;
  const dz = target.pos.z - shooter.pos.z;
  const length = Math.sqrt(dx * dx + dz * dz);
  if (length <= 1e-6) return 0;
  const result = spawnEntity(
    world,
    'projectile',
    shooter.pos.x,
    shooter.pos.y + 0.6,
    shooter.pos.z,
    undefined,
    shooter.id,
  );
  if (!result.ok) return 0;
  const bolt = getEntity(world, result.id);
  if (bolt === undefined) return 0;
  bolt.vel.x = (dx / length) * SHEEP_AI.boltSpeedMps;
  bolt.vel.z = (dz / length) * SHEEP_AI.boltSpeedMps;
  bolt.hp = 1;
  bolt.maxHp = 1;
  return bolt.id;
}

export function resolveEliteFire(world: World, playerIds: readonly EntityId[]): number {
  let bolts = 0;
  for (let i = 0; i < world.activeIds.length; i += 1) {
    const elite = getEntity(world, world.activeIds[i] ?? 0);
    if (elite === undefined || !elite.active || elite.kind !== 'sheep') continue;
    if (sheepKindOf(elite) !== 'elite' || elite.state !== SHEEP_STATE.ranged) continue;
    const ai = elite.ai;
    if (ai.cooldownMs > 0) continue;
    let target: Entity | undefined;
    let bestDistance = Number.POSITIVE_INFINITY;
    for (let p = 0; p < playerIds.length; p += 1) {
      const player = getEntity(world, playerIds[p] ?? 0);
      if (player === undefined || !player.active || player.hp <= 0) continue;
      const dx = player.pos.x - elite.pos.x;
      const dz = player.pos.z - elite.pos.z;
      const distance = Math.sqrt(dx * dx + dz * dz);
      if (distance > SHEEP_AI.eliteBoltRangeM || distance >= bestDistance) continue;
      bestDistance = distance;
      target = player;
    }
    if (target === undefined) continue;
    if (spawnBolt(world, elite, target) === 0) continue;
    ai.cooldownMs = SHEEP_AI.eliteBoltCooldownMs;
    elite.yaw = Math.atan2(target.pos.x - elite.pos.x, target.pos.z - elite.pos.z);
    bolts += 1;
  }
  return bolts;
}

export function advanceProjectiles(
  world: World,
  dtMs: number,
  playerIds: readonly EntityId[],
): number {
  let events = 0;
  for (let i = world.activeIds.length - 1; i >= 0; i -= 1) {
    const bolt = getEntity(world, world.activeIds[i] ?? 0);
    if (bolt === undefined || !bolt.active || bolt.kind !== 'projectile') continue;
    bolt.aliveMs += dtMs;
    const arena = world.config.arena;
    const barn = arena.barn;
    const limit = arena.halfSize - arena.fence.thickness;
    let destroy =
      bolt.aliveMs >= BOLT_LIFE_MS ||
      Math.abs(bolt.pos.x) > limit ||
      Math.abs(bolt.pos.z) > limit ||
      (bolt.pos.x > barn.minX &&
        bolt.pos.x < barn.maxX &&
        bolt.pos.z > barn.minZ &&
        bolt.pos.z < barn.maxZ);
    let victim: Entity | undefined;
    if (!destroy) {
      for (let p = 0; p < playerIds.length; p += 1) {
        const player = getEntity(world, playerIds[p] ?? 0);
        if (player === undefined || !player.active || player.hp <= 0) continue;
        const dx = player.pos.x - bolt.pos.x;
        const dz = player.pos.z - bolt.pos.z;
        const reach = BOLT_RADIUS_M + radiusOf(world, 'player');
        if (dx * dx + dz * dz > reach * reach) continue;
        victim = player;
        break;
      }
    }
    if (victim !== undefined) {
      const owner = getEntity(world, bolt.ownerId);
      damagePlayer(world, owner ?? bolt, 'elite', victim, 0, 0);
      events += 2;
      destroy = true;
    }
    if (destroy) despawnEntity(world, bolt.id);
  }
  return events;
}

export function sheepDefOf(kind: SheepKind) {
  return SHEEP[kind];
}
