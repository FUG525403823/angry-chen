export const ENTITY = Object.freeze({
  maxEntities: 1024,
  radiusByKind: Object.freeze({
    player: 0.4,
    sheep: 0.5,
    projectile: 0.08,
    pickup: 0.35,
  }),
  baseStats: Object.freeze({
    player: Object.freeze({ hp: 100, armor: 50 }),
    sheep: Object.freeze({ hp: 60, armor: 0 }),
    projectile: Object.freeze({ hp: 1, armor: 0 }),
    pickup: Object.freeze({ hp: 1, armor: 0 }),
  }),
  separationPasses: 1,
  separationEpsilon: 1e-6,
} as const);

export type EntityConfig = typeof ENTITY;
