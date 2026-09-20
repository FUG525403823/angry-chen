export const SMOOTHING_TIME_CONSTANT_S = 0.12;
export const SMOOTHING_EPSILON_M = 0.001;

function magnitudeOf(x: number, y: number, z: number): number {
  return Math.sqrt(x * x + y * y + z * z);
}

export interface ErrorSmoother {
  readonly x: number;
  readonly y: number;
  readonly z: number;
  readonly magnitude: number;
  add(dx: number, dy: number, dz: number): void;
  decay(dtMs: number): void;
  reset(): void;
}

/** 视觉偏移：只作用于渲染变换，绝不写回模拟状态。 */
export function createErrorSmoother(): ErrorSmoother {
  let x = 0;
  let y = 0;
  let z = 0;

  return {
    get x(): number {
      return x;
    },
    get y(): number {
      return y;
    },
    get z(): number {
      return z;
    },
    get magnitude(): number {
      return magnitudeOf(x, y, z);
    },
    add(dx: number, dy: number, dz: number): void {
      if (!Number.isFinite(dx) || !Number.isFinite(dy) || !Number.isFinite(dz)) return;
      x += dx;
      y += dy;
      z += dz;
    },
    decay(dtMs: number): void {
      if (!Number.isFinite(dtMs) || dtMs <= 0) return;
      const factor = Math.exp(-(dtMs / 1000) / SMOOTHING_TIME_CONSTANT_S);
      x *= factor;
      y *= factor;
      z *= factor;
      if (magnitudeOf(x, y, z) < SMOOTHING_EPSILON_M) {
        x = 0;
        y = 0;
        z = 0;
      }
    },
    reset(): void {
      x = 0;
      y = 0;
      z = 0;
    },
  };
}
