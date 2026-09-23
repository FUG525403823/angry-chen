import { describe, expect, it } from 'vitest';

import { applySheepKind } from '../ai/sheepBrain.ts';
import { createCommand } from '../command.ts';
import { BUTTON } from '../config/input.ts';
import { CONFIG } from '../config/index.ts';
import { stepWorld } from '../sim.ts';
import { createWorld, getEntity, spawnEntity } from '../world.ts';
import { createCombatContext } from './resolve.ts';
import { switchSlot } from './weapon.ts';

const bareConfig = {
  ...CONFIG,
  arena: { ...CONFIG.arena, playerSpawnPoints: [], enemySpawnPoints: [] },
};

const TICK_MS = 50;
/** 命中率下限：45m 外"按住不放"连射时，大部分子弹应当落在目标身上（三轮追加反馈：远处打不到）。 */
const MIN_HIT_RATE = 0.6;

/**
 * 在 x = -20 车道（避开场地中央谷仓）对着 45m 外的咩咩兵持续开火，每 tick 重新跟踪瞄准
 * （模拟真人把准星压在目标上），返回命中率。目标血量拉满以避免中途死亡影响统计。
 */
function sustainedFireAt(slot: 0 | 1 | 2, distanceM: number, ticks: number) {
  const world = createWorld(11, bareConfig);
  const spawnedShooter = spawnEntity(world, 'player', -20, 0, 5);
  if (!spawnedShooter.ok) throw new Error('shooter spawn failed');
  const spawnedSheep = spawnEntity(world, 'sheep', -20, 0, 5 + distanceM);
  if (!spawnedSheep.ok) throw new Error('sheep spawn failed');
  const shooter = getEntity(world, spawnedShooter.id);
  const sheep = getEntity(world, spawnedSheep.id);
  if (shooter === undefined || sheep === undefined) throw new Error('entity missing');
  applySheepKind(sheep, 'grunt');
  sheep.hp = 1e6;
  sheep.maxHp = 1e6;
  if (slot !== 0) switchSlot(shooter.weapon, slot, 0);

  const counters = { shotsFired: 0, hits: 0, damage: 0 };
  const ctx = createCombatContext();
  ctx.counters = counters;
  const command = createCommand();
  command.buttons = BUTTON.fire;

  for (let i = 0; i < ticks; i += 1) {
    const eyeY = shooter.pos.y + bareConfig.player.eyeHeight;
    const flat = Math.hypot(sheep.pos.x - shooter.pos.x, sheep.pos.z - shooter.pos.z);
    command.seq = (i + 1) & 0xffff;
    command.yaw = Math.atan2(sheep.pos.x - shooter.pos.x, sheep.pos.z - shooter.pos.z);
    command.pitch = Math.atan2(0.55 - eyeY, flat);
    stepWorld(world, [command], TICK_MS, ctx);
  }
  return { counters, flat: Math.hypot(sheep.pos.x - shooter.pos.x, sheep.pos.z - shooter.pos.z) };
}

describe('远距离连射命中率', () => {
  it('手枪 45m 连射：大部分子弹命中（散布累积不得把弹着点推出目标）', () => {
    const { counters, flat } = sustainedFireAt(0, 45, 40);
    const rate = counters.shotsFired === 0 ? 0 : counters.hits / counters.shotsFired;
    expect(counters.shotsFired).toBeGreaterThanOrEqual(8);
    expect(
      rate,
      '手枪 45m 命中 ' +
        String(counters.hits) +
        '/' +
        String(counters.shotsFired) +
        '（' +
        rate.toFixed(2) +
        '），末距离 ' +
        flat.toFixed(1) +
        'm',
    ).toBeGreaterThanOrEqual(MIN_HIT_RATE);
  });

  it('步枪 45m 连射：大部分子弹命中（全自动也不能打飘）', () => {
    const { counters, flat } = sustainedFireAt(1, 45, 24);
    const rate = counters.shotsFired === 0 ? 0 : counters.hits / counters.shotsFired;
    expect(counters.shotsFired).toBeGreaterThanOrEqual(8);
    expect(
      rate,
      '步枪 45m 命中 ' +
        String(counters.hits) +
        '/' +
        String(counters.shotsFired) +
        '（' +
        rate.toFixed(2) +
        '），末距离 ' +
        flat.toFixed(1) +
        'm',
    ).toBeGreaterThanOrEqual(MIN_HIT_RATE);
  });
});
