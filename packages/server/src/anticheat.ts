import { CONFIG, LIMITS, MS_PER_SECOND, type ArenaConfig } from '@ac/shared';

export const ADVANCE_TOLERANCE = 1.15;

export interface AdvanceCheck {
  speed: boolean;
  vertical: boolean;
  position: boolean;
}

export function createAdvanceCheck(): AdvanceCheck {
  return { speed: false, vertical: false, position: false };
}

export function horizontalLimitM(dtMs: number): number {
  return CONFIG.player.sprintSpeed * (dtMs / MS_PER_SECOND) * ADVANCE_TOLERANCE;
}

export function verticalLimitMps(): number {
  return CONFIG.player.jumpSpeed * ADVANCE_TOLERANCE;
}

export function horizontalDistanceM(prevX: number, prevZ: number, x: number, z: number): number {
  const dx = x - prevX;
  const dz = z - prevZ;
  return Math.sqrt(dx * dx + dz * dz);
}

/**
 * 位移是否可由「命令位移上限 + 本 tick 派生位移预算」解释。
 * 派生位移只由权威模拟产生（击退 / 实体分离 / 静态碰撞外推），客户端无法影响。
 */
export function explainableByDerived(
  distanceM: number,
  limitM: number,
  derivedBudgetM: number,
): boolean {
  return distanceM <= limitM + derivedBudgetM;
}

/** 无法由派生位移解释时的硬上限：超出 (limit + 派生预算) 的 poseHardCorrectFactor 倍才回退。 */
export function hardCorrectLimitM(dtMs: number, derivedBudgetM: number): number {
  return (horizontalLimitM(dtMs) + derivedBudgetM) * LIMITS.poseHardCorrectFactor;
}

export function validateAdvance(
  prevX: number,
  prevZ: number,
  x: number,
  z: number,
  velocityY: number,
  dtMs: number,
  out: AdvanceCheck,
  derivedBudgetM = 0,
): AdvanceCheck {
  const dx = x - prevX;
  const dz = z - prevZ;
  out.speed = Math.sqrt(dx * dx + dz * dz) > horizontalLimitM(dtMs) + derivedBudgetM;
  out.vertical = Math.abs(velocityY) > verticalLimitMps();
  return out;
}

export function isLegalPosition(
  x: number,
  y: number,
  z: number,
  arena: ArenaConfig,
  radius: number,
): boolean {
  if (!Number.isFinite(x) || !Number.isFinite(y) || !Number.isFinite(z)) return false;
  const limit = arena.halfSize - arena.fence.thickness / 2 - radius;
  if (Math.abs(x) > limit + 1e-6 || Math.abs(z) > limit + 1e-6) return false;
  const barn = arena.barn;
  if (y >= barn.maxY) return true;
  // 边界语义必须与 collideStatic（resolveBarn）一致：那里把 `x <= minX || x >= maxX` 视为"已在谷仓外"，
  // 并把实体正好推到 minX / maxX。若这里再用 epsilon 把该落点算回"仓内"，权威模拟自己放置的合法位置
  // 会被判非法，玩家贴墙时每个 tick 都被硬回退（O01 并入修复，见 docs/验收报告.md §3.2）。
  const insideX = x > barn.minX - radius && x < barn.maxX + radius;
  const insideZ = z > barn.minZ - radius && z < barn.maxZ + radius;
  return !(insideX && insideZ);
}
