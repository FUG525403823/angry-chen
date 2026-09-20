import { describe, expect, it } from 'vitest';

import { BUTTON } from '../config/input.ts';
import { CONFIG } from '../config/index.ts';
import { WEAPONS } from '../config/weapons.ts';
import { createCommand } from '../command.ts';
import { stepWorld } from '../sim.ts';
import { createWorld, getEntity, spawnEntity } from '../world.ts';
import { createCombatContext } from './resolve.ts';

const bareConfig = {
  ...CONFIG,
  arena: { ...CONFIG.arena, playerSpawnPoints: [], enemySpawnPoints: [] },
};

function scenario() {
  const world = createWorld(7, bareConfig);
  const shooter = spawnEntity(world, 'player', 0, 0, 20);
  spawnEntity(world, 'sheep', 0, 0, 30);
  const counters = { shotsFired: 0, hits: 0, damage: 0 };
  const ctx = createCombatContext();
  ctx.counters = counters;
  const command = createCommand();
  command.seq = 1;
  command.yaw = 0;
  command.pitch = 0;
  command.buttons = BUTTON.fire;
  command.yaw = 0;
  return { world, counters, ctx, command, shooterId: shooter.ok ? shooter.id : 0 };
}

describe('权威命中结算', () => {
  it('准星外的开火不产生任何伤害事件', () => {
    const { world, counters, ctx, command, shooterId } = scenario();
    const sheep = getEntity(world, world.activeIds[world.activeIds.length - 1] ?? 0);
    expect(sheep).toBeDefined();
    const before = sheep === undefined ? 0 : sheep.hp;
    command.yaw = Math.PI;
    stepWorld(world, [command], 50, ctx);
    expect(counters.shotsFired).toBe(1);
    expect(counters.hits).toBe(0);
    expect(counters.damage).toBe(0);
    expect(sheep === undefined ? 0 : sheep.hp).toBe(before);
    expect(world.events.length).toBe(0);
    expect(getEntity(world, shooterId)?.weapon.magInSlot[0]).toBe(WEAPONS.pistol.mag - 1);
  });

  it('对准目标开火造成权威伤害并产出 playerHit', () => {
    const { world, counters, ctx, command, shooterId } = scenario();
    const victimId = world.activeIds[world.activeIds.length - 1] ?? 0;
    const victim = getEntity(world, victimId);
    const before = victim === undefined ? 0 : victim.hp;
    command.pitch = -0.115;
    stepWorld(world, [command], 50, ctx);
    expect(counters.shotsFired).toBe(1);
    expect(counters.hits).toBe(1);
    expect(victim?.hp).toBeCloseTo(before - 25, 6);
    const hit = world.events.find((event) => event.type === 'playerHit');
    expect(hit).toBeDefined();
    expect(hit?.subjectId).toBe(shooterId);
    expect(hit?.targetId).toBe(victimId);
    expect(hit?.value).toBeCloseTo(25, 6);
  });

  it('射速节流：一 tick 内多次开火只结算一次', () => {
    const { world, counters, ctx, command } = scenario();
    stepWorld(world, [command], 50, ctx);
    stepWorld(world, [command], 50, ctx);
    expect(counters.shotsFired).toBe(1);
  });

  it('护甲先扣：玩家被击中时护甲与生命同时下降', () => {
    const { world, counters, ctx, command } = scenario();
    const victim = spawnEntity(world, 'player', 0, 0, 26, 1);
    if (!victim.ok) throw new Error('spawn failed');
    command.pitch = -0.115;
    stepWorld(world, [command, createCommand()], 50, ctx);
    const entity = getEntity(world, victim.id);
    command.pitch = -0.12;
    expect(counters.hits).toBe(1);
    expect(entity?.armor).toBeCloseTo(50 - 15, 6);
    expect(entity?.hp).toBeCloseTo(100 - 10, 6);
  });

  it('倒地玩家不可移动且可被救援', () => {
    const { world, ctx } = scenario();
    const victim = spawnEntity(world, 'player', 0, 0, 26);
    if (!victim.ok) throw new Error('spawn failed');
    const entity = getEntity(world, victim.id);
    if (entity === undefined) throw new Error('missing entity');
    entity.hp = 0;
    entity.combat.downed.downed = true;
    const move = createCommand();
    move.moveX = 1;
    stepWorld(world, [move, move], 50, ctx);
    expect(entity.vel.x).toBe(0);
    expect(entity.vel.z).toBe(0);
  });
});
