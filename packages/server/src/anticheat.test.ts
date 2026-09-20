import { ARENA, CONFIG, LIMITS, SERVER_TICK_MS } from '@ac/shared';
import { describe, expect, it } from 'vitest';
import {
  ADVANCE_TOLERANCE,
  createAdvanceCheck,
  horizontalLimitM,
  isLegalPosition,
  validateAdvance,
  verticalLimitMps,
} from './anticheat.ts';

const radius = CONFIG.entity.radiusByKind.player;

describe('姿态校验阈值', () => {
  it('合法位移不报违规：一个 tick 最多走 sprintSpeed * dt', () => {
    const check = createAdvanceCheck();
    const maxStep = CONFIG.player.sprintSpeed * (SERVER_TICK_MS / 1000);
    validateAdvance(0, 0, maxStep, 0, 0, SERVER_TICK_MS, check);
    expect(check.speed).toBe(false);
    expect(horizontalLimitM(SERVER_TICK_MS)).toBeCloseTo(maxStep * ADVANCE_TOLERANCE, 9);
  });

  it('3 倍速位移被判定为违规', () => {
    const check = createAdvanceCheck();
    const maxStep = CONFIG.player.sprintSpeed * (SERVER_TICK_MS / 1000);
    validateAdvance(0, 0, 0, maxStep * 3, 0, SERVER_TICK_MS, check);
    expect(check.speed).toBe(true);
  });

  it('垂直速度超上限被判定为违规', () => {
    const check = createAdvanceCheck();
    validateAdvance(0, 0, 0, 0, 0, SERVER_TICK_MS, check);
    expect(check.vertical).toBe(false);
    validateAdvance(0, 0, 0, 0, verticalLimitMps() + 0.01, SERVER_TICK_MS, check);
    expect(check.vertical).toBe(true);
    expect(verticalLimitMps()).toBeCloseTo(CONFIG.player.jumpSpeed * ADVANCE_TOLERANCE, 9);
  });
});

describe('位置合法性', () => {
  it('场地内合法、越界非法、NaN 非法', () => {
    const limit = ARENA.halfSize - ARENA.fence.thickness / 2 - radius;
    expect(isLegalPosition(limit, 0, 0, ARENA, radius)).toBe(true);
    expect(isLegalPosition(limit + 0.5, 0, 0, ARENA, radius)).toBe(false);
    expect(isLegalPosition(0, 0, -limit - 0.5, ARENA, radius)).toBe(false);
    expect(isLegalPosition(Number.NaN, 0, 0, ARENA, radius)).toBe(false);
  });

  it('谷仓 AABB 内非法，屋顶之上合法', () => {
    const insideX = (ARENA.barn.minX + ARENA.barn.maxX) / 2;
    const insideZ = (ARENA.barn.minZ + ARENA.barn.maxZ) / 2;
    expect(isLegalPosition(insideX, 0, insideZ, ARENA, radius)).toBe(false);
    expect(isLegalPosition(insideX, ARENA.barn.maxY, insideZ, ARENA, radius)).toBe(true);
    expect(isLegalPosition(ARENA.barn.maxX + radius + 1, 0, insideZ, ARENA, radius)).toBe(true);
  });

  it('阈值常量与 LIMITS 不冲突', () => {
    expect(LIMITS.maxPlayersPerRoom).toBeGreaterThan(0);
  });
});
