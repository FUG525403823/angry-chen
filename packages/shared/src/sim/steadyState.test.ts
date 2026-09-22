import { describe, expect, it } from 'vitest';

import {
  BUTTON,
  LIMITS,
  MAX_ENTITIES,
  SERVER_TICK_MS,
  buildSpatialGrid,
  countActive,
  createCommand,
  createRng,
  createSpatialGrid,
  createWorld,
  rngRange,
  spawnEntity,
  stepWorld,
  type Command,
  type SimEvent,
} from '../index.ts';
import { SHEEP_AI } from '../config/sheep.ts';

const SHEEP_TARGET = 60;
const TICKS = 600;

describe('稳态零分配（O04 §4 任务 11）', () => {
  it('固定 64 实体连续 600 tick：事件全部来自池、引用集合恒定、羊数只统计一次', () => {
    const world = createWorld(9);
    const rng = createRng(9, 'ai');
    for (let i = 0; i < SHEEP_TARGET; i += 1) {
      spawnEntity(world, 'sheep', rngRange(rng, -12, 12), 0, rngRange(rng, 14, 30));
    }

    const commands: Command[] = [];
    for (let i = 0; i < 4; i += 1) {
      const command = createCommand();
      command.seq = i;
      command.buttons = BUTTON.fire;
      commands.push(command);
    }

    const eventsRef = world.events;
    const poolRef = world.eventPool;
    const poolMembers = new Set<SimEvent>(poolRef);
    const seen = new Set<SimEvent>();

    for (let tick = 0; tick < TICKS; tick += 1) {
      for (let s = 0; s < commands.length; s += 1) {
        const command = commands[s];
        if (command !== undefined) command.seq = (tick + s) & 0xffff;
      }
      stepWorld(world, commands, SERVER_TICK_MS);

      for (let i = 0; i < world.events.length; i += 1) {
        const event = world.events[i];
        if (event === undefined) continue;
        expect(event.tick).toBe(world.tick);
        seen.add(event);
      }
      if (countActive(world, 'sheep') < SHEEP_TARGET) {
        spawnEntity(world, 'sheep', rngRange(rng, -12, 12), 0, rngRange(rng, 14, 30));
      }
    }

    expect(world.eventPool).toBe(poolRef);
    expect(world.eventPool.length).toBe(LIMITS.eventPoolSize);
    expect(world.events).toBe(eventsRef);
    expect(seen.size).toBeGreaterThan(0);
    // 引用集合大小恒定：600 tick 内出现过的事件对象不超过池容量（没有逐事件分配）。
    expect(seen.size).toBeLessThanOrEqual(LIMITS.eventPoolSize);
    for (const event of seen) expect(poolMembers.has(event)).toBe(true);
    expect(world.stats.aliveSheep).toBe(countActive(world, 'sheep'));

    // 网格结构引用在一轮轮重建后不变（零分配复用）。
    const grid = createSpatialGrid(SHEEP_AI.neighborRadiusM, MAX_ENTITIES);
    buildSpatialGrid(grid, world);
    const cellStart = grid.cellStart;
    const cellItems = grid.cellItems;
    buildSpatialGrid(grid, world);
    expect(grid.cellStart).toBe(cellStart);
    expect(grid.cellItems).toBe(cellItems);
  });
});
