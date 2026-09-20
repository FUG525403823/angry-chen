import { describe, expect, it } from 'vitest';
import {
  CONFIG,
  SERVER_TICK_MS,
  createCommand,
  createWorld,
  getEntity,
  spawnEntity,
  stepWorld,
  type Command,
  type World,
} from '../index.ts';
import { stepLocalPlayer, type MoveState } from './localStep.ts';

const moveConfig = {
  player: CONFIG.player,
  arena: CONFIG.arena,
  radius: CONFIG.entity.radiusByKind.player,
};

const bareConfig = {
  ...CONFIG,
  arena: { ...CONFIG.arena, playerSpawnPoints: [], enemySpawnPoints: [] },
};

function bareWorld(seed: number): World {
  return createWorld(seed, bareConfig);
}

function moveStateAt(x: number, z: number): MoveState {
  return { pos: { x, y: 0, z }, vel: { x: 0, y: 0, z: 0 }, yaw: 0, pitch: 0 };
}

function trajectory(count: number): Command[] {
  const commands: Command[] = [];
  for (let i = 0; i < count; i += 1) {
    const command = createCommand();
    command.seq = i & 0xffff;
    command.tick = i;
    command.moveX = Math.sin(i * 0.31);
    command.moveY = Math.cos(i * 0.17);
    command.yaw = i * 0.113;
    command.pitch = Math.sin(i * 0.05);
    command.buttons = i % 9 === 0 ? 2 : 0;
    commands.push(command);
  }
  return commands;
}

describe('stepLocalPlayer 与 stepWorld 同一份移动代码', () => {
  it('同命令序列下本地预测与服务器权威逐位一致', () => {
    const world = bareWorld(11);
    const spawned = spawnEntity(world, 'player', -30, 0, 30);
    expect(spawned.ok).toBe(true);
    if (!spawned.ok) return;
    expect(world.activeIds.length).toBe(1);

    const commands = trajectory(240);
    const local = moveStateAt(-30, 30);
    const scratch = createCommand();
    for (const command of commands)
      stepLocalPlayer(local, command, moveConfig, scratch, SERVER_TICK_MS);

    for (const command of commands) stepWorld(world, [command], SERVER_TICK_MS);
    const entity = getEntity(world, spawned.id);
    expect(entity).toBeDefined();
    if (entity === undefined) return;

    expect(entity.pos.x).toBe(local.pos.x);
    expect(entity.pos.y).toBe(local.pos.y);
    expect(entity.pos.z).toBe(local.pos.z);
    expect(entity.yaw).toBe(local.yaw);
    expect(entity.pitch).toBe(local.pitch);
  });

  it('撞到谷仓时两端结果一致', () => {
    const world = bareWorld(12);
    const spawned = spawnEntity(world, 'player', 0, 0, 30);
    expect(spawned.ok).toBe(true);
    if (!spawned.ok) return;

    const command = createCommand();
    command.moveX = 1;
    command.yaw = Math.PI;

    const local = moveStateAt(0, 30);
    const scratch = createCommand();
    for (let i = 0; i < 400; i += 1) {
      command.seq = (i + 1) & 0xffff;
      stepLocalPlayer(local, command, moveConfig, scratch, SERVER_TICK_MS);
      stepWorld(world, [command], SERVER_TICK_MS);
    }
    const entity = getEntity(world, spawned.id);
    expect(entity).toBeDefined();
    if (entity === undefined) return;
    expect(entity.pos.z).toBe(local.pos.z);
    const barnLimit = CONFIG.arena.barn.maxZ + CONFIG.entity.radiusByKind.player;
    expect(local.pos.z).toBeCloseTo(barnLimit, 5);
  });

  it('缺命令时速度归零、位置不动', () => {
    const local = moveStateAt(20, 25);
    local.vel.x = 3;
    local.vel.z = -3;
    const scratch = createCommand();
    stepLocalPlayer(local, undefined, moveConfig, scratch, SERVER_TICK_MS);
    expect(local.pos.x).toBe(20);
    expect(local.pos.z).toBe(25);
    expect(local.vel.x).toBe(0);
    expect(local.vel.z).toBe(0);
  });

  it('冲刺按钮走 sprintSpeed', () => {
    const local = moveStateAt(0, 30);
    const scratch = createCommand();
    const walk = createCommand();
    walk.moveX = 1;
    stepLocalPlayer(local, walk, moveConfig, scratch, SERVER_TICK_MS);
    const walked = local.pos.z - 30;
    const run = createCommand();
    run.moveX = 1;
    run.buttons = 2;
    const start = local.pos.z;
    stepLocalPlayer(local, run, moveConfig, scratch, SERVER_TICK_MS);
    const ran = local.pos.z - start;
    expect(ran).toBeGreaterThan(walked);
    expect(ran).toBeCloseTo(CONFIG.player.sprintSpeed * (SERVER_TICK_MS / 1000), 6);
  });
});
