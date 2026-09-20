import {
  ENTITY_KIND_CODE,
  MAX_ENTITIES,
  SNAPSHOT_RECORD_BYTES,
  createSnapshotMirror,
  decodeSnapshot,
  dequantizeAngle,
  dequantizePosition,
  dequantizeRatio,
  readSnapshotRecord,
  type SnapshotMirror,
} from '@ac/shared';

import { findBracket, interpolationAlpha, lerp, lerpAngle } from '../render/interpolation.ts';

export const INTERPOLATION_DELAY_MS = 100;
export const SNAPSHOT_HISTORY = 6;
export const STATS_WINDOW_MS = 1000;

export interface ViewEntity {
  readonly id: number;
  readonly kind: 0 | 1 | 2 | 3;
  readonly pos: { x: number; y: number; z: number };
  readonly yaw: number;
  readonly pitch: number;
  readonly hpRatio: number;
  readonly state: number;
  readonly flags: number;
}

export interface ViewStats {
  snapshotsPerSec: number;
  inboundBytesPerSec: number;
  rttMs: number;
}

export interface SnapshotView {
  readonly localPlayerId: number;
  getServerTimeMs(): number;
  getAppliedTick(): number;
  getLocalPlayer(): ViewEntity | undefined;
  forEachVisible(cb: (entity: ViewEntity) => void): void;
  getStats(): ViewStats;
}

export interface LocalAuthority {
  found: boolean;
  x: number;
  y: number;
  z: number;
  yaw: number;
  pitch: number;
  lastAckedSeq: number;
  tick: number;
}

export interface SnapshotViewInternal extends SnapshotView {
  readonly mirror: SnapshotMirror;
  setLocalPlayerId(pid: number): void;
  setLocalPrediction(x: number, y: number, z: number, yaw: number, pitch: number): void;
  getLocalAuthority(out: LocalAuthority): LocalAuthority;
  applyFrame(frame: Uint8Array, bytesIn: number): boolean;
  recordRtt(rttMs: number): void;
  reset(): void;
}

interface Keyframe {
  tick: number;
  serverTimeMs: number;
  count: number;
  readonly ids: Uint16Array;
  readonly slotOf: Int16Array;
  readonly x: Float32Array;
  readonly y: Float32Array;
  readonly z: Float32Array;
  readonly yaw: Float32Array;
  readonly pitch: Float32Array;
  readonly hpRatio: Float32Array;
  readonly kind: Uint8Array;
  readonly state: Uint8Array;
  readonly flags: Uint8Array;
}

function createKeyframe(): Keyframe {
  return {
    tick: 0,
    serverTimeMs: 0,
    count: 0,
    ids: new Uint16Array(MAX_ENTITIES),
    slotOf: new Int16Array(MAX_ENTITIES + 1).fill(-1),
    x: new Float32Array(MAX_ENTITIES),
    y: new Float32Array(MAX_ENTITIES),
    z: new Float32Array(MAX_ENTITIES),
    yaw: new Float32Array(MAX_ENTITIES),
    pitch: new Float32Array(MAX_ENTITIES),
    hpRatio: new Float32Array(MAX_ENTITIES),
    kind: new Uint8Array(MAX_ENTITIES),
    state: new Uint8Array(MAX_ENTITIES),
    flags: new Uint8Array(MAX_ENTITIES),
  };
}

export function createSnapshotView(options?: { now?: () => number }): SnapshotViewInternal {
  const now = options?.now ?? ((): number => performance.now());
  const mirror = createSnapshotMirror();
  const frames: Keyframe[] = [];
  for (let i = 0; i < SNAPSHOT_HISTORY; i += 1) frames.push(createKeyframe());
  let frameCount = 0;
  let newestIndex = 0;
  let localPlayerId = 0;
  let latestServerTimeMs = 0;
  let latestRecvAtMs = 0;
  let hasFrame = false;
  let windowStartMs = now();
  let windowSnapshots = 0;
  let windowBytes = 0;
  let rttMs = 0;
  const stats: ViewStats = { snapshotsPerSec: 0, inboundBytesPerSec: 0, rttMs: 0 };
  const pool = new Map<number, ViewEntity>();
  const bracketTimes: number[] = [];
  const ordered: Keyframe[] = [];

  function capture(target: Keyframe, tick: number, serverTimeMs: number): void {
    target.tick = tick;
    target.serverTimeMs = serverTimeMs;
    target.slotOf.fill(-1);
    let count = 0;
    const ids = mirror.ids;
    for (let i = 0; i < ids.length; i += 1) {
      const id = ids[i];
      if (id === undefined || id > MAX_ENTITIES) continue;
      if (mirror.present[id] !== 1) continue;
      readSnapshotRecord(mirror.bytes, id * SNAPSHOT_RECORD_BYTES, mirror.record);
      const slot = count;
      count += 1;
      target.ids[slot] = id;
      target.slotOf[id] = slot;
      target.x[slot] = dequantizePosition(mirror.record.xCm);
      target.y[slot] = dequantizePosition(mirror.record.yCm);
      target.z[slot] = dequantizePosition(mirror.record.zCm);
      target.yaw[slot] = dequantizeAngle(mirror.record.yawUnits);
      target.pitch[slot] = dequantizeAngle(mirror.record.pitchUnits);
      target.hpRatio[slot] = dequantizeRatio(mirror.record.hpRatioUnits);
      target.kind[slot] = ENTITY_KIND_CODE[mirror.record.kind] ?? 0;
      target.state[slot] = mirror.record.state;
      target.flags[slot] = mirror.record.flags;
    }
    target.count = count;
  }

  function entityFor(
    id: number,
    kind: number,
    hpRatio: number,
    state: number,
    flags: number,
  ): ViewEntity {
    let entity = pool.get(id);
    if (entity === undefined) {
      entity = {
        id,
        kind: 0,
        pos: { x: 0, y: 0, z: 0 },
        yaw: 0,
        pitch: 0,
        hpRatio: 0,
        state: 0,
        flags: 0,
      };
      pool.set(id, entity);
    }
    const mutable = entity as {
      kind: 0 | 1 | 2 | 3;
      hpRatio: number;
      state: number;
      flags: number;
    };
    mutable.kind = kind === 1 ? 1 : kind === 2 ? 2 : kind === 3 ? 3 : 0;
    mutable.hpRatio = hpRatio;
    mutable.state = state;
    mutable.flags = flags;
    return entity;
  }

  const predicted = { active: false, x: 0, y: 0, z: 0, yaw: 0, pitch: 0 };
  const authority: LocalAuthority = {
    found: false,
    x: 0,
    y: 0,
    z: 0,
    yaw: 0,
    pitch: 0,
    lastAckedSeq: 0,
    tick: 0,
  };

  function predictedEntity(): ViewEntity {
    const newest = frames[newestIndex];
    const slot = newest === undefined ? -1 : (newest.slotOf[localPlayerId] ?? -1);
    const entity = entityFor(
      localPlayerId,
      slot >= 0 ? (newest?.kind[slot] ?? 0) : 0,
      slot >= 0 ? (newest?.hpRatio[slot] ?? 0) : 1,
      slot >= 0 ? (newest?.state[slot] ?? 0) : 0,
      slot >= 0 ? (newest?.flags[slot] ?? 0) : 0,
    );
    entity.pos.x = predicted.x;
    entity.pos.y = predicted.y;
    entity.pos.z = predicted.z;
    const mutable = entity as { yaw: number; pitch: number };
    mutable.yaw = predicted.yaw;
    mutable.pitch = predicted.pitch;
    return entity;
  }

  const view: SnapshotViewInternal = {
    mirror,
    get localPlayerId(): number {
      return localPlayerId;
    },
    setLocalPlayerId(pid: number): void {
      localPlayerId = pid;
      predicted.active = false;
    },
    setLocalPrediction(x: number, y: number, z: number, yaw: number, pitch: number): void {
      predicted.active = true;
      predicted.x = x;
      predicted.y = y;
      predicted.z = z;
      predicted.yaw = yaw;
      predicted.pitch = pitch;
    },
    getLocalAuthority(out: LocalAuthority): LocalAuthority {
      const newest = frames[newestIndex];
      const slot =
        newest === undefined || localPlayerId === 0 ? -1 : (newest.slotOf[localPlayerId] ?? -1);
      out.found = slot >= 0;
      out.lastAckedSeq = mirror.lastAckedSeq;
      out.tick = newest === undefined ? 0 : newest.tick;
      if (slot < 0 || newest === undefined) {
        out.x = predicted.x;
        out.y = predicted.y;
        out.z = predicted.z;
        out.yaw = predicted.yaw;
        out.pitch = predicted.pitch;
        return out;
      }
      out.x = newest.x[slot] ?? 0;
      out.y = newest.y[slot] ?? 0;
      out.z = newest.z[slot] ?? 0;
      out.yaw = newest.yaw[slot] ?? 0;
      out.pitch = newest.pitch[slot] ?? 0;
      return out;
    },
    getServerTimeMs(): number {
      if (!hasFrame) return 0;
      return latestServerTimeMs + (now() - latestRecvAtMs);
    },
    getAppliedTick(): number {
      return hasFrame ? (frames[newestIndex]?.tick ?? 0) : 0;
    },
    recordRtt(value: number): void {
      if (Number.isFinite(value) && value >= 0) rttMs = value;
      stats.rttMs = Number(rttMs.toFixed(2));
    },
    applyFrame(frame: Uint8Array, bytesIn: number): boolean {
      const previousTick = view.getAppliedTick();
      const decoded = decodeSnapshot(frame, mirror);
      if (!decoded.ok) return false;
      if (hasFrame && mirror.tick <= previousTick) return false;
      const atMs = now();
      const hadFrame = hasFrame;
      frameCount = Math.min(frameCount + 1, SNAPSHOT_HISTORY);
      newestIndex = hadFrame ? (newestIndex + 1) % SNAPSHOT_HISTORY : 0;
      const target = frames[newestIndex];
      if (target === undefined) return false;
      capture(target, mirror.tick, mirror.serverTimeMs);
      latestServerTimeMs = mirror.serverTimeMs;
      latestRecvAtMs = atMs;
      hasFrame = true;
      windowSnapshots += 1;
      windowBytes += bytesIn;
      if (atMs - windowStartMs >= STATS_WINDOW_MS) {
        const spanSec = (atMs - windowStartMs) / 1000;
        stats.snapshotsPerSec = Number((windowSnapshots / spanSec).toFixed(2));
        stats.inboundBytesPerSec = Number((windowBytes / spanSec).toFixed(1));
        windowSnapshots = 0;
        windowBytes = 0;
        windowStartMs = atMs;
      }
      return true;
    },

    getLocalPlayer(): ViewEntity | undefined {
      if (predicted.active) return predictedEntity();
      if (!hasFrame || localPlayerId === 0) return undefined;
      const newest = frames[newestIndex];
      if (newest === undefined) return undefined;
      const slot = newest.slotOf[localPlayerId] ?? -1;
      if (slot < 0) return undefined;
      const entity = entityFor(
        localPlayerId,
        newest.kind[slot] ?? 0,
        newest.hpRatio[slot] ?? 0,
        newest.state[slot] ?? 0,
        newest.flags[slot] ?? 0,
      );
      const mutable = entity as { yaw: number; pitch: number };
      entity.pos.x = newest.x[slot] ?? 0;
      entity.pos.y = newest.y[slot] ?? 0;
      entity.pos.z = newest.z[slot] ?? 0;
      mutable.yaw = newest.yaw[slot] ?? 0;
      mutable.pitch = newest.pitch[slot] ?? 0;
      return entity;
    },
    forEachVisible(cb: (entity: ViewEntity) => void): void {
      if (!hasFrame || frameCount === 0) return;
      const newest = frames[newestIndex];
      if (newest === undefined) return;
      ordered.length = 0;
      bracketTimes.length = 0;
      const start = frameCount < SNAPSHOT_HISTORY ? 0 : (newestIndex + 1) % SNAPSHOT_HISTORY;
      for (let i = 0; i < frameCount; i += 1) {
        const frame = frames[(start + i) % SNAPSHOT_HISTORY];
        if (frame === undefined) continue;
        ordered.push(frame);
        bracketTimes.push(frame.serverTimeMs);
      }
      const renderTimeMs = view.getServerTimeMs() - INTERPOLATION_DELAY_MS;
      const bracket = findBracket(bracketTimes, ordered.length, renderTimeMs);
      const older = ordered[bracket] ?? newest;
      const newer = ordered[bracket + 1] ?? newest;
      const alpha = interpolationAlpha(older.serverTimeMs, newer.serverTimeMs, renderTimeMs);
      for (let i = 0; i < newest.count; i += 1) {
        const id = newest.ids[i];
        if (id === undefined) continue;
        const entity = entityFor(
          id,
          newest.kind[i] ?? 0,
          newest.hpRatio[i] ?? 0,
          newest.state[i] ?? 0,
          newest.flags[i] ?? 0,
        );
        const mutable = entity as { yaw: number; pitch: number };
        const newerSlot = newer.slotOf[id] ?? -1;
        const olderSlot = older === newer ? newerSlot : (older.slotOf[id] ?? -1);
        const endFrame = newerSlot >= 0 ? newer : newest;
        const endSlot = newerSlot >= 0 ? newerSlot : i;
        if (older !== newer && olderSlot >= 0 && endSlot >= 0) {
          entity.pos.x = lerp(older.x[olderSlot] ?? 0, endFrame.x[endSlot] ?? 0, alpha);
          entity.pos.y = lerp(older.y[olderSlot] ?? 0, endFrame.y[endSlot] ?? 0, alpha);
          entity.pos.z = lerp(older.z[olderSlot] ?? 0, endFrame.z[endSlot] ?? 0, alpha);
          mutable.yaw = lerpAngle(older.yaw[olderSlot] ?? 0, endFrame.yaw[endSlot] ?? 0, alpha);
          mutable.pitch = lerpAngle(
            older.pitch[olderSlot] ?? 0,
            endFrame.pitch[endSlot] ?? 0,
            alpha,
          );
        } else {
          entity.pos.x = endFrame.x[endSlot] ?? 0;
          entity.pos.y = endFrame.y[endSlot] ?? 0;
          entity.pos.z = endFrame.z[endSlot] ?? 0;
          mutable.yaw = endFrame.yaw[endSlot] ?? 0;
          mutable.pitch = endFrame.pitch[endSlot] ?? 0;
        }
        cb(entity);
      }
    },
    reset(): void {
      predicted.active = false;
      authority.found = false;
      hasFrame = false;
      frameCount = 0;
      newestIndex = 0;
      latestServerTimeMs = 0;
      latestRecvAtMs = 0;
      windowSnapshots = 0;
      windowBytes = 0;
      windowStartMs = now();
      stats.snapshotsPerSec = 0;
      stats.inboundBytesPerSec = 0;
      for (const frame of frames) frame.count = 0;
      pool.clear();
    },
    getStats(): ViewStats {
      if (hasFrame && now() - latestRecvAtMs > STATS_WINDOW_MS * 1.5) {
        stats.snapshotsPerSec = 0;
        stats.inboundBytesPerSec = 0;
      }
      return stats;
    },
  };
  return view;
}
