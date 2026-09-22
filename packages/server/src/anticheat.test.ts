import { ARENA, CONFIG, LIMITS, SERVER_TICK_MS, collideStatic, createVec3 } from '@ac/shared';
import { describe, expect, it } from 'vitest';
import {
  ADVANCE_TOLERANCE,
  createAdvanceCheck,
  explainableByDerived,
  hardCorrectLimitM,
  horizontalDistanceM,
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
    expect(LIMITS.poseHardCorrectFactor).toBe(1.5);
  });
});

describe('派生位移预算', () => {
  const limit = horizontalLimitM(SERVER_TICK_MS);

  it('位移上限 = sprintSpeed * dt * 1.15 = 0.36225', () => {
    expect(limit).toBeCloseTo(0.36225, 9);
    expect(horizontalDistanceM(0, 0, 0.3, 0.4)).toBeCloseTo(0.5, 9);
  });

  it('派生预算覆盖超出部分：distance = limit + 0.005 且 derived = 0.01 → 不报违规', () => {
    const check = createAdvanceCheck();
    const distance = limit + 0.005;
    expect(explainableByDerived(distance, limit, 0.01)).toBe(true);
    validateAdvance(0, 0, distance, 0, 0, SERVER_TICK_MS, check, 0.01);
    expect(check.speed).toBe(false);
  });

  it('无派生预算时轻微超出只算可疑（未达硬上限 1.5 倍）', () => {
    const check = createAdvanceCheck();
    const distance = limit + 0.005;
    expect(explainableByDerived(distance, limit, 0)).toBe(false);
    validateAdvance(0, 0, distance, 0, 0, SERVER_TICK_MS, check, 0);
    expect(check.speed).toBe(true);
    expect(distance).toBeLessThanOrEqual(hardCorrectLimitM(SERVER_TICK_MS, 0));
  });

  // 冻结契约取 §5 的 poseHardCorrectFactor = 1.5（硬上限 0.543375），
  // 因此 0.5m 只是"可疑"，0.6m 才越过高限（见 docs/优化/O01 §4 任务 6 与该表的差异说明）。
  it('未达硬上限只算可疑：distance = 0.5 且无派生预算', () => {
    expect(explainableByDerived(0.5, limit, 0)).toBe(false);
    expect(hardCorrectLimitM(SERVER_TICK_MS, 0)).toBeCloseTo(0.543375, 9);
    expect(0.5).toBeLessThanOrEqual(hardCorrectLimitM(SERVER_TICK_MS, 0));
  });

  it('超出硬上限才硬纠正：distance = 0.6 且无派生预算', () => {
    expect(explainableByDerived(0.6, limit, 0)).toBe(false);
    expect(0.6).toBeGreaterThan(hardCorrectLimitM(SERVER_TICK_MS, 0));
  });

  // O01 并入修复（本用例原为 it.fails）：collideStatic 把实体推到谷仓外墙外沿 x = barn.minX - radius，
  // 合法性判定必须与之一致，否则玩家贴墙时每个 tick 都被硬回退（见 docs/验收报告.md §3.2）。
  it('谷仓外推落点合法：collideStatic 的边界与 isLegalPosition 的边界语义一致', () => {
    const state = { pos: createVec3(-4.3, 0, 0), vel: createVec3(), yaw: 0, pitch: 0 };
    collideStatic(state, ARENA, radius);
    expect(state.pos.x).toBeCloseTo(ARENA.barn.minX - radius, 9);
    expect(isLegalPosition(state.pos.x, 0, 0, ARENA, radius)).toBe(true);
    expect(isLegalPosition(ARENA.barn.minX - radius + 1e-9, 0, 0, ARENA, radius)).toBe(false);
    expect(
      isLegalPosition(ARENA.barn.minX - radius, 0, ARENA.barn.minZ - radius, ARENA, radius),
    ).toBe(true);
  });

  it('派生预算同步抬升硬上限', () => {
    expect(explainableByDerived(0.5, limit, 0.15)).toBe(true);
    expect(hardCorrectLimitM(SERVER_TICK_MS, 0.15)).toBeCloseTo((limit + 0.15) * 1.5, 9);
    const check = createAdvanceCheck();
    validateAdvance(0, 0, 0.5, 0, 0, SERVER_TICK_MS, check, 0.15);
    expect(check.speed).toBe(false);
  });
});
