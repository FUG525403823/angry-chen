import { describe, expect, it } from 'vitest';

import { createCommand, type Command } from './command.ts';
import { CONFIG } from './config/index.ts';
import { stepWorld } from './sim.ts';
import { createWorld, getEntity, spawnEntity, type Entity, type World } from './world.ts';

const HALF = CONFIG.arena.halfSize;
const FENCE_HALF = CONFIG.arena.fence.thickness / 2;
const PLAYER_LIMIT = HALF - FENCE_HALF - CONFIG.player.radius;
const SHEEP_LIMIT = HALF - FENCE_HALF - CONFIG.entity.radiusByKind.sheep;

function requireEntity(world: World, id: number): Entity {
  const entity = getEntity(world, id);
  if (entity === undefined) throw new Error('entity missing: ' + String(id));
  return entity;
}

function command(moveX: number, moveY: number, yaw = 0, buttons = 0): Command {
  const cmd = createCommand();
  cmd.moveX = moveX;
  cmd.moveY = moveY;
  cmd.yaw = yaw;
  cmd.buttons = buttons;
  return cmd;
}

function run(world: World, commands: Command[], ticks: number): void {
  for (let i = 0; i < ticks; i += 1) stepWorld(world, commands, 50);
}

describe('碰撞与边界', () => {
  it('撞围栏被挡住，斜向移动沿墙滑动', () => {
    const world = createWorld(21);
    const player = requireEntity(world, 1);
    player.pos.x = 39;
    player.pos.z = 0;

    run(world, [command(0, 1)], 40);
    expect(player.pos.x).toBeCloseTo(PLAYER_LIMIT, 12);
    expect(player.pos.x).toBeLessThan(HALF);
    expect(player.vel.x).toBe(0);

    const zBefore = player.pos.z;
    run(world, [command(1, 1)], 40);
    expect(player.pos.x).toBeCloseTo(PLAYER_LIMIT, 12);
    expect(player.pos.z).toBeGreaterThan(zBefore);
  });

  it('撞谷仓被挡在扩大半径后的 AABB 外（南侧）', () => {
    const world = createWorld(22);
    const player = requireEntity(world, 1);
    player.pos.x = 0;
    player.pos.z = 8;

    run(world, [command(-1, 0)], 60);
    expect(player.pos.z).toBeCloseTo(CONFIG.arena.barn.maxZ + CONFIG.player.radius, 12);
    expect(player.vel.z).toBe(0);
  });

  it('撞谷仓被挡在扩大半径后的 AABB 外（东侧）', () => {
    const world = createWorld(23);
    const player = requireEntity(world, 1);
    player.pos.x = 6;
    player.pos.z = 0;

    run(world, [command(0, -1)], 60);
    expect(player.pos.x).toBeCloseTo(CONFIG.arena.barn.maxX + CONFIG.player.radius, 12);
  });

  it('边界外的坐标会被拉回场内，因此不可达', () => {
    const world = createWorld(24);
    const stray = spawnEntity(world, 'sheep', 200, 0, -300);
    expect(stray.ok).toBe(true);
    if (!stray.ok) return;

    stepWorld(world, [], 50);
    const sheep = requireEntity(world, stray.id);
    expect(sheep.pos.x).toBeCloseTo(SHEEP_LIMIT, 12);
    expect(sheep.pos.z).toBeCloseTo(-SHEEP_LIMIT, 12);
    expect(sheep.pos.x).toBeLessThan(HALF);
  });

  it('重叠实体被分离到半径和之外', () => {
    const world = createWorld(25);
    const a = requireEntity(world, 1);
    const b = requireEntity(world, 2);
    a.pos.x = 0;
    a.pos.z = 20;
    b.pos.x = 0.1;
    b.pos.z = 20;

    stepWorld(world, [], 50);
    const dx = b.pos.x - a.pos.x;
    const dz = b.pos.z - a.pos.z;
    expect(Math.sqrt(dx * dx + dz * dz)).toBeCloseTo(CONFIG.player.radius * 2, 6);
  });

  it('斜向输入被归一化，速度不超过配置上限（含疾跑）', () => {
    const world = createWorld(26);
    const player = requireEntity(world, 1);
    player.pos.x = 0;
    player.pos.z = 20;

    stepWorld(world, [command(1, 1)], 50);
    expect(Math.sqrt(player.vel.x ** 2 + player.vel.z ** 2)).toBeCloseTo(
      CONFIG.player.moveSpeed,
      12,
    );

    stepWorld(world, [command(1, 1, 0, 2)], 50);
    expect(Math.sqrt(player.vel.x ** 2 + player.vel.z ** 2)).toBeCloseTo(
      CONFIG.player.sprintSpeed,
      12,
    );
  });
});

describe('退化情形', () => {
  it('完全重合的两个实体沿 +X 确定性分离', () => {
    const world = createWorld(27);
    const a = requireEntity(world, 1);
    const b = requireEntity(world, 2);
    a.pos.x = 5;
    a.pos.z = 25;
    b.pos.x = 5;
    b.pos.z = 25;

    stepWorld(world, [], 50);
    expect(b.pos.x - a.pos.x).toBeCloseTo(CONFIG.player.radius * 2, 6);
    expect(b.pos.z).toBeCloseTo(a.pos.z, 12);
    expect(a.pos.x).toBeLessThan(b.pos.x);
  });
});

describe('分离与静态几何的交互', () => {
  it('分离后重解谷仓碰撞，实体不会被留在谷仓内部', () => {
    const world = createWorld(28);
    const a = requireEntity(world, 1);
    const b = requireEntity(world, 2);
    a.pos.x = 0.1;
    a.pos.z = 0.1;
    b.pos.x = 0.1;
    b.pos.z = 0.1;

    stepWorld(world, [], 50);

    const radius = CONFIG.player.radius;
    const barn = CONFIG.arena.barn;
    for (const entity of [a, b]) {
      const insideX = entity.pos.x > barn.minX - radius && entity.pos.x < barn.maxX + radius;
      const insideZ = entity.pos.z > barn.minZ - radius && entity.pos.z < barn.maxZ + radius;
      expect(insideX && insideZ).toBe(false);
    }
  });
});
