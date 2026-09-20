export type RngStream = 'ai' | 'spawn' | 'fx';

export type Rng = () => number;

const streamId: Record<RngStream, number> = { ai: 1, spawn: 2, fx: 3 };

export function mulberry32(seed: number): Rng {
  let a = seed >>> 0;
  return function next(): number {
    a = (a + 0x6d2b79f5) >>> 0;
    let t = a;
    t = Math.imul(t ^ (t >>> 15), t | 1);
    t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

export function createRng(seed: number, stream: RngStream): Rng {
  const derived = (Math.imul(seed >>> 0, 2654435761) + streamId[stream]) >>> 0;
  return mulberry32(derived);
}

export function rngFloat(rng: Rng): number {
  return rng();
}

export function rngRange(rng: Rng, min: number, max: number): number {
  return min + (max - min) * rng();
}

export function rngInt(rng: Rng, minInclusive: number, maxInclusive: number): number {
  const span = maxInclusive - minInclusive + 1;
  if (span <= 0) return minInclusive;
  return minInclusive + Math.floor(rng() * span);
}
