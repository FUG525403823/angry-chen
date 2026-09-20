import { SHEEP_ORDER, type SheepKind } from '../config/sheep.ts';
import {
  MAX_ACTIVE_SPAWN_POINTS,
  MAX_SPAWNS_PER_TICK,
  MIN_SPAWN_DISTANCE_M,
  WAVE_MAX,
  isBossWave,
  sheepKindIndex,
  waveBudget,
} from '../config/waves.ts';
import { rngInt, rngRange, type Rng } from '../rng.ts';
import { getEntity, spawnEntity, type EntityId, type World } from '../world.ts';
import { SHEEP_STATE } from '../config/sheep.ts';
import { applySheepKind } from './sheepBrain.ts';
import { pushEvent } from './sheepAttack.ts';

export const DIRECTOR_KIND_COUNT = SHEEP_ORDER.length;

export interface DirectorState {
  wave: number;
  planned: number;
  spawned: number;
  plan: Int32Array;
  cursor: number;
  waveStartPending: boolean;
  finished: boolean;
  totalSpawns: number;
}

export interface DirectorTick {
  spawned: number;
  waveStarted: boolean;
  waveCleared: boolean;
  matchEnded: boolean;
}

export function createDirectorState(): DirectorState {
  return {
    wave: 0,
    planned: 0,
    spawned: 0,
    plan: new Int32Array(DIRECTOR_KIND_COUNT),
    cursor: 0,
    waveStartPending: false,
    finished: false,
    totalSpawns: 0,
  };
}

export function planWave(state: DirectorState, wave: number, playerCount: number): number {
  state.plan.fill(0);
  state.wave = wave;
  state.cursor = 0;
  state.spawned = 0;
  let remaining = waveBudget(wave, playerCount);
  if (isBossWave(wave) && remaining >= 20) {
    state.plan[sheepKindIndex('king')] = (state.plan[sheepKindIndex('king')] ?? 0) + 1;
    remaining -= 20;
  }
  if (wave >= 5) {
    const cap = Math.min(2 + Math.floor(wave / 5), Math.floor(remaining / 6));
    if (cap > 0) {
      state.plan[sheepKindIndex('elite')] = (state.plan[sheepKindIndex('elite')] ?? 0) + cap;
      remaining -= cap * 6;
    }
  }
  if (wave >= 3) {
    const cap = Math.min(Math.floor(remaining / 3), Math.max(1, Math.floor(wave / 3)));
    if (cap > 0) {
      state.plan[sheepKindIndex('ram')] = (state.plan[sheepKindIndex('ram')] ?? 0) + cap;
      remaining -= cap * 3;
    }
  }
  if (remaining > 0)
    state.plan[sheepKindIndex('grunt')] = (state.plan[sheepKindIndex('grunt')] ?? 0) + remaining;
  state.planned = 0;
  for (let i = 0; i < DIRECTOR_KIND_COUNT; i += 1) state.planned += state.plan[i] ?? 0;
  state.waveStartPending = true;
  return state.planned;
}

export function planCountFor(state: DirectorState, kind: SheepKind): number {
  return state.plan[sheepKindIndex(kind)] ?? 0;
}

export function plannedBudgetSum(state: DirectorState): number {
  let total = 0;
  for (let i = 0; i < DIRECTOR_KIND_COUNT; i += 1) {
    const kind = SHEEP_ORDER[i];
    if (kind === undefined) continue;
    total += (state.plan[i] ?? 0) * (i === 0 ? 1 : i === 1 ? 3 : i === 2 ? 6 : 20);
  }
  return total;
}

export function aliveSheepCount(world: World): number {
  let alive = 0;
  for (let i = 0; i < world.activeIds.length; i += 1) {
    const entity = getEntity(world, world.activeIds[i] ?? 0);
    if (entity !== undefined && entity.active && entity.kind === 'sheep') alive += 1;
  }
  return alive;
}

export function nearestPlayerDistanceM(
  world: World,
  x: number,
  z: number,
  playerIds: readonly EntityId[],
): number {
  let best = Number.POSITIVE_INFINITY;
  for (let i = 0; i < playerIds.length; i += 1) {
    const player = getEntity(world, playerIds[i] ?? 0);
    if (player === undefined || !player.active || player.kind !== 'player') continue;
    const dx = player.pos.x - x;
    const dz = player.pos.z - z;
    const distance = Math.sqrt(dx * dx + dz * dz);
    if (distance < best) best = distance;
  }
  return best;
}

const pointScratch = new Int32Array(MAX_ACTIVE_SPAWN_POINTS);

export function selectSpawnPointIndices(
  world: World,
  playerIds: readonly EntityId[],
  rng: Rng,
  out: Int32Array,
  maxPoints = MAX_ACTIVE_SPAWN_POINTS,
): number {
  const points = world.config.arena.enemySpawnPoints;
  let count = 0;
  if (points.length === 0) return 0;
  const start = rngInt(rng, 0, points.length - 1);
  for (let offset = 0; offset < points.length && count < maxPoints; offset += 1) {
    const point = points[(start + offset) % points.length];
    if (point === undefined) continue;
    if (nearestPlayerDistanceM(world, point.x, point.z, playerIds) <= MIN_SPAWN_DISTANCE_M)
      continue;
    out[count] = (start + offset) % points.length;
    count += 1;
  }
  return count;
}

export function updateDirector(
  world: World,
  state: DirectorState,
  playerCount: number,
  rng: Rng,
  playerIds: readonly EntityId[],
): DirectorTick {
  const result: DirectorTick = {
    spawned: 0,
    waveStarted: false,
    waveCleared: false,
    matchEnded: false,
  };
  if (state.finished || state.wave <= 0) return result;

  if (state.spawned < state.planned) {
    if (state.waveStartPending) {
      state.waveStartPending = false;
      pushEvent(world, 'waveStart', 0, 0, 0, 0, 0, 0, state.wave);
      result.waveStarted = true;
    }
    const pointCount = selectSpawnPointIndices(world, playerIds, rng, pointScratch);
    let budget = MAX_SPAWNS_PER_TICK;
    while (budget > 0 && state.spawned < state.planned) {
      while (state.cursor < DIRECTOR_KIND_COUNT && (state.plan[state.cursor] ?? 0) <= 0) {
        state.cursor += 1;
      }
      if (state.cursor >= DIRECTOR_KIND_COUNT) break;
      const kind = SHEEP_ORDER[state.cursor] ?? 'grunt';
      const pointIndex = pointCount > 0 ? (pointScratch[state.spawned % pointCount] ?? 0) : -1;
      const point = pointIndex >= 0 ? world.config.arena.enemySpawnPoints[pointIndex] : undefined;
      const x = (point?.x ?? 0) + rngRange(rng, -1.5, 1.5);
      const z = (point?.z ?? 0) + rngRange(rng, -1.5, 1.5);
      const spawned = spawnEntity(world, 'sheep', x, 0, z);
      if (!spawned.ok) break;
      const entity = getEntity(world, spawned.id);
      if (entity !== undefined) {
        applySheepKind(entity, kind);
        entity.state = SHEEP_STATE.graze;
      }
      state.plan[state.cursor] = (state.plan[state.cursor] ?? 0) - 1;
      state.spawned += 1;
      state.totalSpawns += 1;
      result.spawned += 1;
      budget -= 1;
    }
    return result;
  }

  if (aliveSheepCount(world) > 0) return result;

  pushEvent(world, 'waveClear', 0, 0, 0, 0, 0, 0, state.wave);
  result.waveCleared = true;
  if (state.wave >= WAVE_MAX) {
    state.finished = true;
    pushEvent(world, 'matchEnded', 0, 0, 0, 0, 0, 0, state.wave);
    result.matchEnded = true;
  } else {
    planWave(state, state.wave + 1, playerCount);
  }
  return result;
}
