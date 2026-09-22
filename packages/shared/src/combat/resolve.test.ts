import { describe, expect, it } from 'vitest';

import { BUTTON } from '../config/input.ts';
import { CONFIG } from '../config/index.ts';
import { WEAPONS } from '../config/weapons.ts';
import { createCommand } from '../command.ts';
import { stepWorld } from '../sim.ts';
import { createWorld, getEntity, spawnEntity, type World } from '../world.ts';
import { createCombatContext } from './resolve.ts';
import { createRayHit, rayVsAabb, rayVsCapsule } from './raycast.ts';
import { partForHeight } from '../config/combat.ts';
import { SHOT_MAX_DISTANCE_M, createShotTrace, traceRay, type ShotTrace } from './resolve.ts';
import {
  createPoseHistory,
  createSampledPose,
  recordPoseHistory,
  samplePoseAgo,
  type PoseHistory,
} from '../sim/history.ts';
import { SERVER_TICK_MS } from '../index.ts';

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

describe('对拍：早退优化不改变 ShotTrace（O04 §4 任务 6/11）', () => {
  function referenceTrace(
    world: World,
    history: PoseHistory | null,
    shooterId: number,
    ox: number,
    oy: number,
    oz: number,
    dx: number,
    dy: number,
    dz: number,
    maxDistanceM: number,
    rewindMs: number,
    out: ShotTrace,
  ): ShotTrace {
    // 与 traceRay 同语义的暴力参考实现：不做任何早退，逐实体做胶囊求交；
    // out 的复用与局部复位也和 traceRay 保持一致（否则残留字段会造成假差异）。
    out.hit = false;
    out.targetId = 0;
    out.distanceM = 0;
    out.x = 0;
    out.y = 0;
    out.z = 0;
    const barn = world.config.arena.barn;
    const barnHit = createRayHit();
    rayVsAabb(
      ox,
      oy,
      oz,
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
      barnHit,
    );
    let bestT = barnHit.hit ? barnHit.t : maxDistanceM;
    const ids = world.activeIds;
    const radiusByKind = world.config.entity.radiusByKind;
    const heightByKind = world.config.entity.heightByKind;
    for (let i = 0; i < ids.length; i += 1) {
      const target = getEntity(world, ids[i] ?? 0);
      if (target === undefined || !target.active || target.id === shooterId) continue;
      if (target.kind !== 'player' && target.kind !== 'sheep') continue;
      let tx = target.pos.x;
      let ty = target.pos.y;
      let tz = target.pos.z;
      if (history !== null && target.kind === 'player') {
        const pose = createSampledPose();
        samplePoseAgo(history, rewindMs, target.id, pose);
        if (pose.found) {
          tx = pose.x;
          ty = pose.y;
          tz = pose.z;
        }
      }
      const radius = radiusByKind[target.kind];
      const height = heightByKind[target.kind];
      const hit = createRayHit();
      rayVsCapsule(
        ox,
        oy,
        oz,
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
        hit,
      );
      if (!hit.hit || hit.t >= bestT) continue;
      bestT = hit.t;
      out.hit = true;
      out.targetId = target.id;
      out.targetKind = target.kind;
      out.part = partForHeight(ty, height, hit.y);
      out.distanceM = hit.t;
      out.x = hit.x;
      out.y = hit.y;
      out.z = hit.z;
    }
    return out;
  }

  function expectSameTrace(actual: ShotTrace, expected: ShotTrace, label: string): void {
    expect({ label, ...actual }).toEqual({ label, ...expected });
  }

  it('20 组固定姿态下 traceRay 与暴力参考实现逐字段一致', () => {
    const world = createWorld(23, bareConfig);
    const shooter = spawnEntity(world, 'player', 0, 0, 20);
    if (!shooter.ok) throw new Error('shooter spawn failed');
    for (let i = 0; i < 10; i += 1) {
      spawnEntity(world, 'sheep', 2.2 * i - 9, 0, 26 + (i % 3) * 3);
    }
    spawnEntity(world, 'player', -3, 0, 30);
    spawnEntity(world, 'player', 3, 0, 31);

    const history = createPoseHistory(SERVER_TICK_MS);
    for (let i = 0; i < 6; i += 1) {
      world.tick += 1;
      world.timeMs += SERVER_TICK_MS;
      recordPoseHistory(history, world);
    }

    const poses: { ox: number; oy: number; oz: number; dx: number; dy: number; dz: number }[] = [];
    const aim = (ox: number, oy: number, oz: number, tx: number, ty: number, tz: number): void => {
      const ddx = tx - ox;
      const ddy = ty - oy;
      const ddz = tz - oz;
      const length = Math.hypot(ddx, ddy, ddz);
      poses.push({ ox, oy, oz, dx: ddx / length, dy: ddy / length, dz: ddz / length });
    };
    const targets = world.activeIds;
    for (let i = 0; i < targets.length && poses.length < 20; i += 1) {
      const target = getEntity(world, targets[i] ?? 0);
      if (target === undefined) continue;
      const ox = target.pos.x + (i % 2 === 0 ? 6 : -6);
      const oy = target.pos.y + 2.2;
      const oz = target.pos.z + 5;
      aim(ox, oy, oz, target.pos.x, target.pos.y + 0.9, target.pos.z);
      aim(ox, oy, oz, target.pos.x + 0.4, target.pos.y + 1.8, target.pos.z - 0.3);
    }
    expect(poses.length).toBe(20);

    const direct = createShotTrace();
    const directReference = createShotTrace();
    for (let i = 0; i < 10; i += 1) {
      const pose = poses[i];
      if (pose === undefined) continue;
      traceRay(
        world,
        null,
        shooter.id,
        pose.ox,
        pose.oy,
        pose.oz,
        pose.dx,
        pose.dy,
        pose.dz,
        SHOT_MAX_DISTANCE_M,
        0,
        direct,
      );
      const expected = referenceTrace(
        world,
        null,
        shooter.id,
        pose.ox,
        pose.oy,
        pose.oz,
        pose.dx,
        pose.dy,
        pose.dz,
        SHOT_MAX_DISTANCE_M,
        0,
        directReference,
      );
      expectSameTrace(direct, expected, 'pose ' + i);
    }

    const rewound = createShotTrace();
    const rewoundReference = createShotTrace();
    let hits = 0;
    for (let i = 10; i < 20; i += 1) {
      const pose = poses[i];
      if (pose === undefined) continue;
      traceRay(
        world,
        history,
        shooter.id,
        pose.ox,
        pose.oy,
        pose.oz,
        pose.dx,
        pose.dy,
        pose.dz,
        SHOT_MAX_DISTANCE_M,
        120,
        rewound,
      );
      const expected = referenceTrace(
        world,
        history,
        shooter.id,
        pose.ox,
        pose.oy,
        pose.oz,
        pose.dx,
        pose.dy,
        pose.dz,
        SHOT_MAX_DISTANCE_M,
        120,
        rewoundReference,
      );
      expectSameTrace(rewound, expected, 'rewound pose ' + i);
      if (rewound.hit) hits += 1;
    }
    expect(hits).toBeGreaterThan(0);
  });
});
