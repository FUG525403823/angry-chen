export const SNAPSHOT_FLAG = Object.freeze({
  downed: 1,
  rageMode: 2,
  reloading: 4,
  charging: 8,
  fading: 16,
} as const);

export const NET = Object.freeze({
  snapshotMaxEntities: 256,
  snapshotFlag: SNAPSHOT_FLAG,
} as const);

export type NetConfig = typeof NET;
