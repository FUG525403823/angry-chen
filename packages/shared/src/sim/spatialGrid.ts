import { CONFIG } from '../config/index.ts';
import { getEntity, type World } from '../world.ts';

/**
 * 零分配均匀网格：两趟计数排序把实体按格写进 cellItems，查询只走覆盖半径的整数格。
 *
 * 映射基准是 CONFIG.arena.halfSize（build 与查询用同一基准）。超出范围的坐标夹到边界格：
 * 格边长不变时，「距离 ≤ 半径」的两个实体必然落在同格或相邻格，夹取只会多访问、不会漏配对。
 */
export interface SpatialGrid {
  readonly cellSizeM: number;
  readonly cols: number;
  readonly rows: number;
  /** 长度 cols*rows + 1：格 c 的条目区间是 [cellStart[c], cellStart[c+1])。 */
  readonly cellStart: Int32Array;
  /** 存 entities 数组下标（id - 1），按 activeIds 顺序（id 升序）稳定排列。 */
  readonly cellItems: Int32Array;
  readonly itemCount: number;
  /** 实现补充字段（§5 契约之外）：每格计数与写入游标，计数排序的中间状态。 */
  readonly cellCount: Int32Array;
  readonly cursor: Int32Array;
}

export function createSpatialGrid(cellSizeM: number, capacity: number): SpatialGrid {
  const cols = Math.max(1, Math.ceil((CONFIG.arena.halfSize * 2) / cellSizeM));
  const rows = cols;
  const cells = cols * rows;
  const cellStart = new Int32Array(cells + 1);
  const cellItems = new Int32Array(Math.max(1, capacity));
  const cellCount = new Int32Array(cells);
  const cursor = new Int32Array(cells);
  return {
    cellSizeM,
    cols,
    rows,
    cellStart,
    cellItems,
    cellCount,
    cursor,
    get itemCount(): number {
      return cellStart[cells] ?? 0;
    },
  };
}

/** 坐标 → 格下标；非有限值返回 -1（调用方跳过该实体）。 */
export function cellIndexOf(grid: SpatialGrid, x: number, z: number): number {
  if (!Number.isFinite(x) || !Number.isFinite(z)) return -1;
  const half = CONFIG.arena.halfSize;
  let cx = Math.floor((x + half) / grid.cellSizeM);
  let cz = Math.floor((z + half) / grid.cellSizeM);
  if (cx < 0) cx = 0;
  else if (cx >= grid.cols) cx = grid.cols - 1;
  if (cz < 0) cz = 0;
  else if (cz >= grid.rows) cz = grid.rows - 1;
  return cz * grid.cols + cx;
}

/** 两趟计数排序重建网格（O(n)）：只统计参与碰撞的实体，projectile 不算。 */
export function buildSpatialGrid(grid: SpatialGrid, world: World): SpatialGrid {
  const { cellStart, cellCount, cursor, cellItems, cols, rows } = grid;
  const cells = cols * rows;
  cellCount.fill(0);
  const ids = world.activeIds;

  for (let i = 0; i < ids.length; i += 1) {
    const entity = getEntity(world, ids[i] ?? 0);
    if (entity === undefined || !entity.active || entity.kind === 'projectile') continue;
    const cell = cellIndexOf(grid, entity.pos.x, entity.pos.z);
    if (cell < 0) continue;
    cellCount[cell] = (cellCount[cell] ?? 0) + 1;
  }

  let at = 0;
  for (let c = 0; c < cells; c += 1) {
    cellStart[c] = at;
    at += cellCount[c] ?? 0;
    cursor[c] = cellStart[c] ?? 0;
  }
  cellStart[cells] = at;

  for (let i = 0; i < ids.length; i += 1) {
    const id = ids[i] ?? 0;
    const entity = getEntity(world, id);
    if (entity === undefined || !entity.active || entity.kind === 'projectile') continue;
    const cell = cellIndexOf(grid, entity.pos.x, entity.pos.z);
    if (cell < 0) continue;
    const slot = cursor[cell] ?? 0;
    if (slot >= cellItems.length) continue;
    cellItems[slot] = id - 1;
    cursor[cell] = slot + 1;
  }

  return grid;
}

/** 遍历所有可能落在 [x±radiusM, z±radiusM] 内的格子里的实体（下标）。 */
export function forEachNeighbor(
  grid: SpatialGrid,
  x: number,
  z: number,
  radiusM: number,
  visit: (entityIndex: number) => void,
): void {
  const { cellStart, cellItems, cols, rows, cellSizeM } = grid;
  const half = CONFIG.arena.halfSize;
  let minCx = Math.floor((x - radiusM + half) / cellSizeM);
  let maxCx = Math.floor((x + radiusM + half) / cellSizeM);
  let minCz = Math.floor((z - radiusM + half) / cellSizeM);
  let maxCz = Math.floor((z + radiusM + half) / cellSizeM);
  // 越界坐标与实体一样夹到边界格：夹取只会多访问，不会漏配对（格边长不变）。
  if (minCx < 0) minCx = 0;
  else if (minCx > cols - 1) minCx = cols - 1;
  if (minCz < 0) minCz = 0;
  else if (minCz > rows - 1) minCz = rows - 1;
  if (maxCx < 0) maxCx = 0;
  else if (maxCx > cols - 1) maxCx = cols - 1;
  if (maxCz < 0) maxCz = 0;
  else if (maxCz > rows - 1) maxCz = rows - 1;
  for (let cz = minCz; cz <= maxCz; cz += 1) {
    const rowBase = cz * cols;
    for (let cx = minCx; cx <= maxCx; cx += 1) {
      const cell = rowBase + cx;
      const end = cellStart[cell + 1] ?? 0;
      for (let i = cellStart[cell] ?? 0; i < end; i += 1) {
        const index = cellItems[i];
        if (index !== undefined) visit(index);
      }
    }
  }
}
