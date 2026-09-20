export const ARENA = Object.freeze({
  width: 80,
  depth: 80,
  halfSize: 40,
  fence: Object.freeze({
    height: 3,
    thickness: 0.5,
  }),
  barn: Object.freeze({
    minX: -4,
    maxX: 4,
    minY: 0,
    maxY: 5,
    minZ: -4,
    maxZ: 4,
  }),
  playerSpawnPoints: Object.freeze([
    { x: -4.5, z: 7 },
    { x: -1.5, z: 7 },
    { x: 1.5, z: 7 },
    { x: 4.5, z: 7 },
  ]),
  enemySpawnPoints: Object.freeze([
    { x: 0, z: 38 },
    { x: 19, z: 32.909 },
    { x: 32.909, z: 19 },
    { x: 38, z: 0 },
    { x: 32.909, z: -19 },
    { x: 19, z: -32.909 },
    { x: 0, z: -38 },
    { x: -19, z: -32.909 },
    { x: -32.909, z: -19 },
    { x: -38, z: -0 },
    { x: -32.909, z: 19 },
    { x: -19, z: 32.909 },
  ]),
} as const);

export type ArenaConfig = typeof ARENA;
