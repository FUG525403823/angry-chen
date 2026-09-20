export const TWO_PI = Math.PI * 2;

export function lerp(from: number, to: number, t: number): number {
  return from + (to - from) * t;
}

export function shortestAngleDelta(from: number, to: number): number {
  let delta = (to - from) % TWO_PI;
  if (delta > Math.PI) delta -= TWO_PI;
  else if (delta < -Math.PI) delta += TWO_PI;
  return delta;
}

export function lerpAngle(from: number, to: number, t: number): number {
  return from + shortestAngleDelta(from, to) * t;
}

export function interpolationAlpha(fromMs: number, toMs: number, timeMs: number): number {
  const span = toMs - fromMs;
  if (span <= 0) return 1;
  const alpha = (timeMs - fromMs) / span;
  if (alpha < 0) return 0;
  if (alpha > 1) return 1;
  return alpha;
}

export function findBracket(times: readonly number[], count: number, timeMs: number): number {
  if (count <= 1) return 0;
  for (let i = 0; i < count - 1; i += 1) {
    const current = times[i];
    const next = times[i + 1];
    if (current === undefined || next === undefined) continue;
    if (timeMs < next) return i;
  }
  return count - 2;
}
