import { getEntity, type World } from '../world.ts';

export const POSE_HISTORY_SLOTS = 20;
export const POSE_FLOATS_PER_ENTRY = 6;
export const POSE_HISTORY_DEFAULT_SLOT_MS = 50;
export const POSE_HISTORY_DEFAULT_MAX_PER_SLOT = 8;
export const REWIND_LIMIT_MS = 200;

export interface PoseHistory {
  readonly slotMs: number;
  readonly capacity: number;
  readonly maxPerSlot: number;
  readonly ticks: Int32Array;
  readonly counts: Uint8Array;
  readonly values: Float64Array;
  head: number;
  written: number;
}

export interface SampledPose {
  found: boolean;
  clamped: boolean;
  pid: number;
  x: number;
  y: number;
  z: number;
  yaw: number;
  pitch: number;
}

export function createPoseHistory(
  slotMs: number = POSE_HISTORY_DEFAULT_SLOT_MS,
  capacity: number = POSE_HISTORY_SLOTS,
  maxPerSlot: number = POSE_HISTORY_DEFAULT_MAX_PER_SLOT,
): PoseHistory {
  return {
    slotMs,
    capacity,
    maxPerSlot,
    ticks: new Int32Array(capacity),
    counts: new Uint8Array(capacity),
    values: new Float64Array(capacity * maxPerSlot * POSE_FLOATS_PER_ENTRY),
    head: -1,
    written: 0,
  };
}

export function createSampledPose(): SampledPose {
  return { found: false, clamped: false, pid: 0, x: 0, y: 0, z: 0, yaw: 0, pitch: 0 };
}

function baseOffset(history: PoseHistory, slot: number): number {
  return slot * history.maxPerSlot * POSE_FLOATS_PER_ENTRY;
}

export function recordPoseHistory(history: PoseHistory, world: World): void {
  const slot = history.head < 0 ? 0 : (history.head + 1) % history.capacity;
  history.head = slot;
  history.written = Math.min(history.written + 1, history.capacity);
  history.ticks[slot] = world.tick;

  let count = 0;
  const ids = world.activeIds;
  for (let i = 0; i < ids.length && count < history.maxPerSlot; i += 1) {
    const entity = getEntity(world, ids[i] ?? 0);
    if (entity === undefined || !entity.active || entity.kind !== 'player') continue;
    const offset = baseOffset(history, slot) + count * POSE_FLOATS_PER_ENTRY;
    history.values[offset] = entity.id;
    history.values[offset + 1] = entity.pos.x;
    history.values[offset + 2] = entity.pos.y;
    history.values[offset + 3] = entity.pos.z;
    history.values[offset + 4] = entity.yaw;
    history.values[offset + 5] = entity.pitch;
    count += 1;
  }
  history.counts[slot] = count;
}

function slotIndex(history: PoseHistory, age: number): number {
  return (history.head - age + history.capacity * 2) % history.capacity;
}

function readEntry(history: PoseHistory, slot: number, pid: number, out: SampledPose): boolean {
  const count = history.counts[slot] ?? 0;
  const base = baseOffset(history, slot);
  for (let i = 0; i < count; i += 1) {
    const offset = base + i * POSE_FLOATS_PER_ENTRY;
    if (history.values[offset] !== pid) continue;
    out.found = true;
    out.x = history.values[offset + 1] ?? 0;
    out.y = history.values[offset + 2] ?? 0;
    out.z = history.values[offset + 3] ?? 0;
    out.yaw = history.values[offset + 4] ?? 0;
    out.pitch = history.values[offset + 5] ?? 0;
    return true;
  }
  return false;
}

const olderScratch: SampledPose = {
  found: false,
  clamped: false,
  pid: 0,
  x: 0,
  y: 0,
  z: 0,
  yaw: 0,
  pitch: 0,
};

export function samplePoseAgo(
  history: PoseHistory,
  ms: number,
  pid: number,
  out: SampledPose,
): SampledPose {
  out.found = false;
  out.clamped = ms > REWIND_LIMIT_MS || ms < 0;
  out.pid = pid;
  if (history.head < 0 || history.written === 0) return out;

  const clampedMs = Math.min(Math.max(ms, 0), REWIND_LIMIT_MS);
  const newestTick = history.ticks[history.head] ?? 0;
  const targetTick = newestTick - clampedMs / history.slotMs;

  const available = history.written;
  let olderSlot = slotIndex(history, available - 1);
  let newerSlot = olderSlot;
  for (let age = 0; age < available; age += 1) {
    const slot = slotIndex(history, age);
    if ((history.ticks[slot] ?? 0) > targetTick) continue;
    olderSlot = slot;
    newerSlot = age === 0 ? slot : slotIndex(history, age - 1);
    break;
  }

  const olderTick = history.ticks[olderSlot] ?? 0;
  const newerTick = history.ticks[newerSlot] ?? 0;
  const span = newerTick - olderTick;

  if (!readEntry(history, newerSlot, pid, out)) return out;
  const newerX = out.x;
  const newerY = out.y;
  const newerZ = out.z;
  const newerYaw = out.yaw;
  const newerPitch = out.pitch;
  if (span <= 0) return out;

  const alpha = Math.min(Math.max((targetTick - olderTick) / span, 0), 1);
  if (alpha >= 1) return out;
  if (!readEntry(history, olderSlot, pid, olderScratch)) return out;

  out.x = olderScratch.x + (newerX - olderScratch.x) * alpha;
  out.y = olderScratch.y + (newerY - olderScratch.y) * alpha;
  out.z = olderScratch.z + (newerZ - olderScratch.z) * alpha;
  out.yaw = olderScratch.yaw + (newerYaw - olderScratch.yaw) * alpha;
  out.pitch = olderScratch.pitch + (newerPitch - olderScratch.pitch) * alpha;
  return out;
}
