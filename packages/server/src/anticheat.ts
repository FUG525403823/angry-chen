import { CONFIG, MS_PER_SECOND, type ArenaConfig } from '@ac/shared';

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

export function validateAdvance(
  prevX: number,
  prevZ: number,
  x: number,
  z: number,
  velocityY: number,
  dtMs: number,
  out: AdvanceCheck,
): AdvanceCheck {
  const dx = x - prevX;
  const dz = z - prevZ;
  out.speed = Math.sqrt(dx * dx + dz * dz) > horizontalLimitM(dtMs);
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
  const insideX = x > barn.minX - radius - 1e-6 && x < barn.maxX + radius + 1e-6;
  const insideZ = z > barn.minZ - radius - 1e-6 && z < barn.maxZ + radius + 1e-6;
  return !(insideX && insideZ);
}
