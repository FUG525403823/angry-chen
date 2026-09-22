import {
  ARENA,
  type CombatContext,
  type Command,
  type EntityId,
  LIMITS,
  MATCH_PHASE,
  aliveSheepCount,
  createDirectorState,
  planWave,
  updateDirector,
  type DirectorState,
  type MatchState,
  type PoseHistory,
  REWIND_LIMIT_MS,
  SERVER_TICK_MS,
  SHOT_MAX_DISTANCE_M,
  SNAPSHOT_RATE_X10,
  type Snapshot,
  type World,
  activeMag,
  countActive,
  createCombatContext,
  createCommand,
  createMatchState,
  createPoseHistory,
  createShotTrace,
  createSnapshot,
  createVec3,
  createWorld,
  despawnEntity,
  encodeEventFrame,
  encodeMatchState,
  encodeSnapshot,
  getEntity,
  rageRatio,
  rageSecondsLeft,
  recordPoseHistory,
  reloadRemainingMs,
  resetDownedState,
  reviveRatio,
  snapshotWorld,
  spawnEntity,
  stepWorld,
  traceRay,
  yawPitchToDirection,
} from '@ac/shared';

import {
  createAdvanceCheck,
  explainableByDerived,
  hardCorrectLimitM,
  horizontalDistanceM,
  horizontalLimitM,
  isLegalPosition,
  validateAdvance,
  type AdvanceCheck,
} from './anticheat.ts';
import {
  GRACE_PERIOD_MS,
  RECONNECT_MIN_HP_RATIO,
  accumulateMatchEvents,
  accumulateMatchTime,
  applyShotDeltas,
  captureShotBaseline,
  checkMatchEnd,
  createMatchRuntime,
  endMatch,
  handleWaveCleared,
  joinMatchRecord,
  updateMatch,
  type MatchDeps,
  type MatchRuntime,
} from './match/controller.ts';
import { LOG_EVENTS, emit } from './log.ts';
import {
  recordSimDrift,
  recordTickInterval,
  recordTickScheduleError,
  recordTickWork,
} from './metrics.ts';
import type { Session } from './session.ts';

export const MATCH_STATE_INTERVAL_MS = 1000;
export const ANTICHEAT_LOG_INTERVAL_MS = 5000;

export interface Room {
  readonly code: string;
  readonly world: World;
  readonly sessions: Session[];
  /** pid → 会话，O(1) 取命令（房间人数 ≤ 4，避免每 tick 的线性查找）。 */
  readonly sessionsByPid: Map<number, Session>;
  readonly commands: Command[];
  readonly idleCommand: Command;
  readonly snapshot: Snapshot;
  readonly matchState: MatchState;
  readonly broadcastBuffer: Uint8Array;
  phase: number;
  wave: number;
  intermissionMs: number;
  director: DirectorState;
  lastUpdateMs: number;
  lastTickAtMs: number;
  /** 本房间首个 tick 的 monotonic 时刻（自校正调度的参考点）；-1 = 尚未开始。 */
  firstTickAtMs: number;
  /** 从首 tick 起的 tick 序号：理想时刻 = firstTickAtMs + tickIndex × SERVER_TICK_MS。 */
  tickIndex: number;
  /** 漂移基准：首个 tick 走完那一刻的真实时刻与模拟时间（见 runTick 里的口径说明）。 */
  wallStartMs: number;
  simStartMs: number;
  driftArmed: boolean;
  accumulatorMs: number;
  matchStateTimerMs: number;
  emptySinceMs: number | null;
  snapshotRateX10: number;
  jitterArmed: boolean;
  lastAnticheatLogMs: number;
  readonly capacity: number;
  readonly history: PoseHistory;
  readonly preTick: Float64Array;
  /** 与 preTick 三元组一一对应的 pid，按 pid 取回 tick 前位置（不依赖遍历序号）。 */
  readonly preTickIds: EntityId[];
  readonly combat: CombatContext;
  readonly match: MatchRuntime;
  /** 已同步到 metrics 的事件池丢弃数（world 级累计值的已消费部分）。 */
  eventsDroppedSeen: number;
}

export interface RoomDeps extends MatchDeps {
  monotonicNow(): number;
}

export function createRoom(
  code: string,
  seed: number,
  nowMs: number,
  capacity: number = LIMITS.maxPlayersPerRoom,
): Room {
  const world = createWorldForRoom(seed);
  return {
    code,
    world,
    sessions: [],
    sessionsByPid: new Map<number, Session>(),
    commands: [],
    idleCommand: createCommand(),
    snapshot: createSnapshot(),
    matchState: createMatchState(),
    broadcastBuffer: new Uint8Array(LIMITS.maxFrameBytes),
    phase: MATCH_PHASE.lobby,
    match: createMatchRuntime(),
    wave: 0,
    intermissionMs: 0,
    director: createDirectorState(),
    lastUpdateMs: nowMs,
    lastTickAtMs: nowMs,
    firstTickAtMs: -1,
    tickIndex: 0,
    wallStartMs: 0,
    simStartMs: 0,
    driftArmed: false,
    accumulatorMs: 0,
    matchStateTimerMs: 0,
    emptySinceMs: nowMs,
    snapshotRateX10: SNAPSHOT_RATE_X10,
    jitterArmed: false,
    lastAnticheatLogMs: Number.NEGATIVE_INFINITY,
    capacity: Math.min(capacity, LIMITS.maxPlayersPerRoom),
    history: createPoseHistory(SERVER_TICK_MS),
    combat: createCombatContext(),
    preTick: new Float64Array(LIMITS.maxPlayersPerRoom * 3),
    preTickIds: [],
    eventsDroppedSeen: 0,
  };
}

function createWorldForRoom(seed: number): World {
  const world = createWorld(seed);
  const initialPlayers: number[] = [];
  for (const entity of world.entities) {
    if (entity.active && entity.kind === 'player') initialPlayers.push(entity.id);
  }
  initialPlayers.sort((a, b) => b - a);
  for (const id of initialPlayers) despawnEntity(world, id);
  return world;
}

export function roomJoin(room: Room, session: Session, nowMs: number): 'ok' | 'full' {
  if (room.sessions.length >= room.capacity) return 'full';
  const spawnIndex = room.sessions.length % ARENA.playerSpawnPoints.length;
  const spawn = ARENA.playerSpawnPoints[spawnIndex];
  if (spawn === undefined) return 'full';
  const result = spawnEntity(room.world, 'player', spawn.x, 0, spawn.z);
  if (!result.ok) return 'full';
  session.pid = result.id;
  session.roomCode = room.code;
  session.ready = false;
  session.weapon = 0;
  session.kills = 0;
  session.disconnectedAtMs = null;
  session.joinedAtMs = nowMs;
  room.sessions.push(session);
  room.sessionsByPid.set(session.pid, session);
  if (room.match.hostId === 0) room.match.hostId = session.pid;
  joinMatchRecord(room, session.pid, session.name);
  room.emptySinceMs = null;
  resetSchedule(room);
  broadcastMatchState(room);
  return 'ok';
}

export function roomReconnect(
  room: Room,
  existing: Session,
  incoming: Session,
): 'ok' | 'not-found' {
  const index = room.sessions.indexOf(existing);
  if (index < 0) return 'not-found';
  incoming.pid = existing.pid;
  incoming.name = existing.name;
  incoming.ready = existing.ready;
  incoming.weapon = existing.weapon;
  incoming.weaponApplied = existing.weaponApplied;
  incoming.kills = existing.kills;
  incoming.roomCode = room.code;
  incoming.disconnectedAtMs = null;
  incoming.joinedAtMs = existing.joinedAtMs;
  room.sessions[index] = incoming;
  if (incoming.pid > 0) room.sessionsByPid.set(incoming.pid, incoming);
  existing.pid = 0;
  existing.roomCode = null;
  const entity = incoming.pid > 0 ? room.world.entities[incoming.pid - 1] : undefined;
  if (entity !== undefined && entity.active && entity.kind === 'player') {
    entity.idle = false;
    entity.vel.x = 0;
    entity.vel.y = 0;
    entity.vel.z = 0;
    const minHp = entity.maxHp * RECONNECT_MIN_HP_RATIO;
    if (entity.combat.downed.downed) resetDownedState(entity.combat.downed);
    if (entity.hp < minHp) entity.hp = minHp;
  }
  room.emptySinceMs = null;
  broadcastMatchState(room);
  return 'ok';
}

export function roomDisconnect(room: Room, session: Session, nowMs: number): void {
  if (session.disconnectedAtMs !== null) return;
  session.disconnectedAtMs = nowMs;
  const entity = session.pid > 0 ? room.world.entities[session.pid - 1] : undefined;
  if (entity !== undefined && entity.active && entity.kind === 'player') {
    entity.idle = true;
    entity.vel.x = 0;
    entity.vel.y = 0;
    entity.vel.z = 0;
  }
  session.command.moveX = 0;
  session.command.moveY = 0;
  session.command.buttons = 0;
  session.command.switchTo = 0;
  if (roomIsIdle(room) && room.emptySinceMs === null) room.emptySinceMs = nowMs;
  broadcastMatchState(room);
}

export function removeMember(
  room: Room,
  session: Session,
  nowMs: number,
  leftMidMatch: boolean,
): boolean {
  const index = room.sessions.indexOf(session);
  if (index < 0) return false;
  room.sessions.splice(index, 1);
  if (session.pid > 0) room.sessionsByPid.delete(session.pid);
  if (session.pid > 0) {
    const record = room.match.records.get(session.pid);
    if (record !== undefined) record.leftMidMatch = true;
    despawnEntity(room.world, session.pid);
  }
  session.pid = 0;
  session.roomCode = null;
  session.ready = false;
  session.disconnectedAtMs = null;
  void leftMidMatch;
  reassignHost(room);
  if (room.sessions.length === 0) {
    room.emptySinceMs = nowMs;
    room.accumulatorMs = 0;
    resetSchedule(room);
  }
  broadcastMatchState(room);
  return true;
}

export function roomLeave(room: Room, session: Session, nowMs: number): boolean {
  return removeMember(room, session, nowMs, true);
}

function reassignHost(room: Room): void {
  let hostStillPresent = false;
  for (let i = 0; i < room.sessions.length; i += 1) {
    const session = room.sessions[i];
    if (session !== undefined && session.pid === room.match.hostId) {
      hostStillPresent = true;
      break;
    }
  }
  if (hostStillPresent) return;
  let nextHost = 0;
  for (let i = 0; i < room.sessions.length; i += 1) {
    const session = room.sessions[i];
    if (session === undefined || session.pid <= 0) continue;
    const entity = room.world.entities[session.pid - 1];
    if (entity === undefined || !entity.active || entity.kind !== 'player') continue;
    if (nextHost === 0 || session.pid < nextHost) nextHost = session.pid;
  }
  room.match.hostId = nextHost;
}

export function roomIsIdle(room: Room): boolean {
  if (room.sessions.length === 0) return true;
  for (let i = 0; i < room.sessions.length; i += 1) {
    const session = room.sessions[i];
    if (session !== undefined && session.disconnectedAtMs === null) return false;
  }
  return true;
}

function connectedSessionCount(room: Room): number {
  let count = 0;
  for (let i = 0; i < room.sessions.length; i += 1) {
    const session = room.sessions[i];
    if (session !== undefined && session.disconnectedAtMs === null) count += 1;
  }
  return count;
}

function expireGraceSessions(deps: RoomDeps, room: Room, nowMs: number): void {
  const sessions = room.sessions;
  for (let i = sessions.length - 1; i >= 0; i -= 1) {
    const session = sessions[i];
    if (session === undefined || session.disconnectedAtMs === null) continue;
    if (nowMs - session.disconnectedAtMs < GRACE_PERIOD_MS) continue;
    const graceMs = nowMs - session.disconnectedAtMs;
    room.match.counters.graceTimeouts += 1;
    deps.metrics.graceTimeouts += 1;
    emit(deps.log, 'info', LOG_EVENTS.graceTimeout, {
      room: room.code,
      tick: room.world.tick,
      pid: session.pid,
      detail: { graceMs },
    });
    removeMember(room, session, nowMs, true);
  }
}

function buildCommands(room: Room): void {
  const commands = room.commands;
  commands.length = 0;
  const world = room.world;
  const ids = world.activeIds;
  for (let i = 0; i < ids.length; i += 1) {
    const id = ids[i];
    if (id === undefined) continue;
    const entity = world.entities[id - 1];
    if (entity === undefined || !entity.active || entity.kind !== 'player') continue;
    let command: Command | undefined = room.sessionsByPid.get(id)?.command;
    if (command === undefined) {
      const idle = room.idleCommand;
      idle.moveX = 0;
      idle.moveY = 0;
      idle.buttons = 0;
      idle.seq = 0;
      idle.tick = 0;
      idle.switchTo = 0;
      idle.yaw = entity.yaw;
      idle.pitch = 0;
      command = idle;
    }
    commands.push(command);
  }
}

/** 房间清空 / 重建时重置调度基准：新一串 tick 从新的参考点重新计时。 */
function resetSchedule(room: Room): void {
  room.firstTickAtMs = -1;
  room.tickIndex = 0;
  room.wallStartMs = 0;
  room.simStartMs = 0;
  room.driftArmed = false;
  room.jitterArmed = false;
}

export function updateRoom(deps: RoomDeps, room: Room, nowMs: number): void {
  let elapsed = nowMs - room.lastUpdateMs;
  if (elapsed < 0) elapsed = 0;
  room.lastUpdateMs = nowMs;

  expireGraceSessions(deps, room, nowMs);

  if (room.sessions.length === 0) {
    if (room.phase === MATCH_PHASE.playing || room.phase === MATCH_PHASE.intermission) {
      endMatch(room, deps, nowMs, 1);
      broadcastMatchState(room);
    }
    room.accumulatorMs = 0;
    room.matchStateTimerMs = 0;
    resetSchedule(room);
    return;
  }
  if (connectedSessionCount(room) === 0) {
    room.accumulatorMs = 0;
    resetSchedule(room);
    return;
  }

  room.accumulatorMs += elapsed;
  const budgetStartMs = deps.monotonicNow();
  let steps = 0;
  while (room.accumulatorMs >= SERVER_TICK_MS) {
    if (steps >= LIMITS.tickCatchUpLimit) {
      const skipped = Math.floor(room.accumulatorMs / SERVER_TICK_MS);
      room.accumulatorMs -= skipped * SERVER_TICK_MS;
      deps.metrics.tickSkips += skipped;
      room.match.counters.skipped += skipped;
      break;
    }
    room.accumulatorMs -= SERVER_TICK_MS;
    runTick(deps, room, steps === 0, nowMs);
    steps += 1;
    // 单房间工作量预算：超出即让出事件循环，剩余累积量留给下一次调用（让出 ≠ 丢 tick，
    // 不改步长也不改 tick 顺序，只是把同一串 tick 分摊到多次调用）。
    if (deps.monotonicNow() - budgetStartMs > LIMITS.roomTickBudgetMs) {
      deps.metrics.roomBudgetExceeded += 1;
      break;
    }
  }

  if (updateMatch(room, deps, nowMs, elapsed)) broadcastMatchState(room);

  room.matchStateTimerMs += elapsed;
  while (room.matchStateTimerMs >= MATCH_STATE_INTERVAL_MS) {
    room.matchStateTimerMs -= MATCH_STATE_INTERVAL_MS;
    broadcastMatchState(room);
  }
}

function activePlayerCount(room: Room): number {
  let count = 0;
  for (let i = 0; i < room.sessions.length; i += 1) {
    const session = room.sessions[i];
    if (session !== undefined && session.pid > 0 && session.disconnectedAtMs === null) count += 1;
  }
  return count > 0 ? count : 1;
}

const directorPlayerIdScratch: number[] = [];

function collectDirectorPlayerIds(room: Room): number[] {
  directorPlayerIdScratch.length = 0;
  for (let i = 0; i < room.sessions.length; i += 1) {
    const session = room.sessions[i];
    if (session === undefined || session.pid <= 0 || session.disconnectedAtMs !== null) continue;
    const entity = room.world.entities[session.pid - 1];
    if (entity === undefined || !entity.active || entity.kind !== 'player' || entity.idle) continue;
    directorPlayerIdScratch.push(entity.id);
  }
  // 邻居/出生点选择已走空间网格或按距离判定，不再依赖 pid 升序（O04 §4 任务 7）。
  return directorPlayerIdScratch;
}

function runTick(deps: RoomDeps, room: Room, firstInBurst: boolean, nowMs: number): void {
  const monotonic = deps.monotonicNow();
  if (room.firstTickAtMs < 0) {
    room.firstTickAtMs = monotonic;
    room.tickIndex = 0;
  }
  recordTickScheduleError(
    deps.metrics,
    monotonic - (room.firstTickAtMs + room.tickIndex * SERVER_TICK_MS),
  );
  room.tickIndex += 1;
  if (room.jitterArmed && firstInBurst)
    recordTickInterval(deps.metrics, monotonic - room.lastTickAtMs - SERVER_TICK_MS);
  room.lastTickAtMs = monotonic;
  room.jitterArmed = true;
  deps.metrics.ticks += 1;
  const counters = room.match.counters;
  counters.ticks += 1;
  const connected = connectedSessionCount(room);
  if (connected > counters.peakPlayers) counters.peakPlayers = connected;

  buildCommands(room);
  const checked = capturePreTick(room);
  if (room.combat.history === null) {
    room.combat.history = room.history;
    room.combat.rewindMsFor = (pid: number) => rewindMsForPlayerId(room, pid);
  }
  const sessionsForWeapon = room.sessions;
  for (let i = 0; i < sessionsForWeapon.length; i += 1) {
    const weaponSession = sessionsForWeapon[i];
    if (weaponSession === undefined || weaponSession.weaponApplied) continue;
    const owner = weaponSession.pid > 0 ? room.world.entities[weaponSession.pid - 1] : undefined;
    if (owner === undefined || !owner.active || owner.kind !== 'player') continue;
    const slot = weaponSession.weapon === 1 ? 1 : weaponSession.weapon === 2 ? 2 : 0;
    owner.weapon.activeSlot = slot;
    owner.weapon.reloadEndsAtMs = 0;
    owner.weapon.nextFireAllowedAtMs = room.world.timeMs;
    weaponSession.weaponApplied = true;
  }
  room.combat.counters = deps.metrics;
  captureShotBaseline(room);
  stepWorld(room.world, room.commands, SERVER_TICK_MS, room.combat);
  applyPoseValidation(deps, room, checked);
  if (room.phase === MATCH_PHASE.playing) {
    if (room.director.wave !== room.wave && !room.director.finished) {
      const players = activePlayerCount(room);
      planWave(room.director, room.wave, players);
      emit(deps.log, 'info', LOG_EVENTS.waveStart, {
        room: room.code,
        tick: room.world.tick,
        detail: { wave: room.wave, players },
      });
    }
    const directorTick = updateDirector(
      room.world,
      room.director,
      activePlayerCount(room),
      room.world.rng.spawn,
      collectDirectorPlayerIds(room),
    );
    deps.metrics.spawns += directorTick.spawned;
    if (directorTick.waveCleared) {
      handleWaveCleared(room, deps, deps.now());
    } else {
      checkMatchEnd(room, deps, deps.now());
    }
  }
  accumulateMatchEvents(room);
  applyShotDeltas(room);
  accumulateMatchTime(room, SERVER_TICK_MS);
  deps.metrics.sheepAlive = aliveSheepCount(room.world);
  const eventsDropped = room.world.stats.eventsDropped;
  if (eventsDropped > room.eventsDroppedSeen) {
    deps.metrics.eventsDropped += eventsDropped - room.eventsDroppedSeen;
    room.eventsDroppedSeen = eventsDropped;
  }
  deps.metrics.waveCurrent = room.wave;
  recordPoseHistory(room.history, room.world);
  snapshotWorld(room.world, room.snapshot);

  const active = countActive(room.world);
  if (active > deps.metrics.maxEntitiesObserved) deps.metrics.maxEntitiesObserved = active;
  if (active > counters.peakEntities) counters.peakEntities = active;

  const periodicFull = room.world.tick % LIMITS.fullSnapshotIntervalTicks === 0;
  const sessions = room.sessions;
  for (let i = 0; i < sessions.length; i += 1) {
    const session = sessions[i];
    if (session === undefined || session.disconnectedAtMs !== null) continue;
    sendSnapshot(deps, room, session, periodicFull);
  }

  const events = room.world.events;
  if (events.length > 0) {
    const size = encodeEventFrame(room.world.tick, events, room.broadcastBuffer);
    const frame = room.broadcastBuffer.subarray(0, size);
    for (let i = 0; i < sessions.length; i += 1) {
      const session = sessions[i];
      if (session === undefined || session.disconnectedAtMs !== null) continue;
      session.connection.send(frame);
    }
    deps.metrics.eventsSent += events.length;
  }

  // 口径写死：工作量 = 整段 runTick 的真实耗时；漂移 =（模拟时间增量）−（真实时间增量）。
  // 漂移基准在**首个 tick 走完**时取：若在 stepWorld 之前取，模拟已经领先真实时间一个 tick，
  // 指标会永久读出一个 +50ms 的系统偏差（本文件的单测固定了这一口径）。
  if (!room.driftArmed) {
    room.driftArmed = true;
    room.wallStartMs = nowMs;
    room.simStartMs = room.world.timeMs;
  }
  recordSimDrift(deps.metrics, room.world.timeMs - room.simStartMs - (nowMs - room.wallStartMs));
  recordTickWork(deps.metrics, deps.monotonicNow() - monotonic);
}

function sendSnapshot(deps: RoomDeps, room: Room, session: Session, periodicFull: boolean): void {
  const forceFull = session.baseline.tick === 0 || periodicFull;
  const size = encodeSnapshot(
    room.snapshot,
    session.baseline,
    session.lastAckedCmdSeq,
    session.outbound,
    forceFull,
  );
  session.connection.send(session.outbound.subarray(0, size));
  deps.metrics.snapshotsSent += 1;
  deps.metrics.snapshotBytes += size;
  if (size > deps.metrics.snapshotBytesMax) deps.metrics.snapshotBytesMax = size;
  deps.metrics.snapshotRecords += room.snapshot.entities.length;
  deps.metrics.framesOut += 1;
  deps.metrics.bytesOut += size;
}

function hpRatioOf(room: Room, pid: number): number {
  const entity = pid > 0 ? room.world.entities[pid - 1] : undefined;
  if (entity === undefined || !entity.active) return 0;
  if (entity.maxHp <= 0) return 0;
  return entity.hp / entity.maxHp;
}

export function broadcastMatchState(room: Room): void {
  const state = room.matchState;
  state.phase = room.phase;
  state.wave = room.wave;
  state.hostId = room.match.hostId;
  state.intermissionMs =
    room.phase === MATCH_PHASE.loading
      ? Math.min(65535, Math.max(0, Math.round(room.match.loadingMs)))
      : room.intermissionMs > 0
        ? Math.min(65535, Math.round(room.intermissionMs))
        : 0;
  const sessions = room.sessions;
  for (let i = 0; i < sessions.length; i += 1) {
    const session = sessions[i];
    if (session === undefined) continue;
    let entry = state.players[i];
    if (entry === undefined) {
      entry = {
        pid: 0,
        name: '',
        ready: false,
        weapon: 0,
        hpRatio: 1,
        kills: 0,
        mag: 0,
        reserve: 0,
        reloadLeft10Ms: 0,
        rage: 0,
        rageLeft100Ms: 0,
        downed: false,
        reviveRatio255: 0,
      };
      state.players[i] = entry;
    }
    entry.pid = session.pid;
    entry.name = session.name;
    entry.ready = session.ready;
    const entity = session.pid > 0 ? room.world.entities[session.pid - 1] : undefined;
    const alive = entity !== undefined && entity.active && entity.kind === 'player';
    entry.weapon = alive && session.weaponApplied ? entity.weapon.activeSlot : session.weapon;
    entry.hpRatio = hpRatioOf(room, session.pid);
    entry.kills = session.kills;
    entry.mag = alive ? activeMag(entity.weapon) : 0;
    entry.reserve = alive ? entity.weapon.reserveAmmo : 0;
    entry.reloadLeft10Ms = alive
      ? Math.min(255, Math.round(reloadRemainingMs(entity.weapon, room.world.timeMs) / 10))
      : 0;
    entry.rage = alive ? Math.min(255, Math.round(rageRatio(entity.combat.rage) * 100)) : 0;
    entry.rageLeft100Ms = alive
      ? Math.min(255, Math.round(rageSecondsLeft(entity.combat.rage, room.world.timeMs) * 10))
      : 0;
    entry.downed = alive ? entity.combat.downed.downed : false;
    entry.reviveRatio255 = alive
      ? Math.min(255, Math.round(reviveRatio(entity.combat.downed) * 255))
      : 0;
    if (alive) session.weapon = entity.weapon.activeSlot;
  }
  state.players.length = sessions.length;
  if (sessions.length === 0) return;
  const size = encodeMatchState(state, room.broadcastBuffer);
  const frame = room.broadcastBuffer.subarray(0, size);
  for (let i = 0; i < sessions.length; i += 1) {
    const session = sessions[i];
    if (session === undefined || session.disconnectedAtMs !== null) continue;
    session.connection.send(frame);
  }
}

export { SHOT_MAX_DISTANCE_M };

export interface ShotResult {
  hit: boolean;
  targetId: number;
  rewindMs: number;
  clamped: boolean;
  x: number;
  y: number;
  z: number;
}

export function createShotResult(): ShotResult {
  return { hit: false, targetId: 0, rewindMs: 0, clamped: false, x: 0, y: 0, z: 0 };
}

const directionScratch = createVec3();
const advanceCheckScratch = createAdvanceCheck();
const shotScratch = createShotResult();
const shotTraceScratch = createShotTrace();

export function rewindMsForPlayerId(room: Room, pid: number): number {
  if (pid === 0) return 0;
  const sessions = room.sessions;
  for (let i = 0; i < sessions.length; i += 1) {
    const session = sessions[i];
    if (session !== undefined && session.pid === pid) return rewindMsForSession(session);
  }
  return 0;
}

export function rewindMsForSession(session: Session): number {
  return Math.min(Math.max(session.lagTicks * SERVER_TICK_MS, 0), REWIND_LIMIT_MS);
}

function capturePreTick(room: Room): number {
  const ids = room.world.activeIds;
  const preTick = room.preTick;
  const preTickIds = room.preTickIds;
  preTickIds.length = 0;
  let count = 0;
  for (let i = 0; i < ids.length; i += 1) {
    const entity = getEntity(room.world, ids[i] ?? 0);
    if (entity === undefined || !entity.active || entity.kind !== 'player') continue;
    if (count >= LIMITS.maxPlayersPerRoom) break;
    preTickIds.push(entity.id);
    preTick[count * 3] = entity.pos.x;
    preTick[count * 3 + 1] = entity.pos.y;
    preTick[count * 3 + 2] = entity.pos.z;
    count += 1;
  }
  return count;
}

function logPoseViolation(
  deps: RoomDeps,
  room: Room,
  pid: number,
  check: AdvanceCheck,
  distanceM: number,
  derivedBudgetM: number,
  decision: 'suspect' | 'rejected',
): void {
  const nowMs = room.lastUpdateMs;
  if (nowMs - room.lastAnticheatLogMs < ANTICHEAT_LOG_INTERVAL_MS) return;
  room.lastAnticheatLogMs = nowMs;
  const round = (value: number): number => Number(value.toFixed(4));
  emit(deps.log, 'warn', LOG_EVENTS.anticheatSpeed, {
    room: room.code,
    tick: room.world.tick,
    pid,
    detail: {
      decision,
      speed: String(check.speed),
      vertical: String(check.vertical),
      position: String(check.position),
      distanceM: round(distanceM),
      derivedBudgetM: round(derivedBudgetM),
      suspects: deps.metrics.poseSuspects,
      rejected: deps.metrics.poseRejected,
      hardCorrects: deps.metrics.hardCorrectTotal,
    },
  });
}

export function applyPoseValidation(deps: RoomDeps, room: Room, checked: number): void {
  const world = room.world;
  const preTick = room.preTick;
  const preTickIds = room.preTickIds;
  const arena = world.config.arena;
  const radius = world.config.entity.radiusByKind.player;
  const check = advanceCheckScratch;
  const limitM = horizontalLimitM(SERVER_TICK_MS);
  const count = Math.min(checked, preTickIds.length);
  for (let index = 0; index < count; index += 1) {
    const entity = getEntity(world, preTickIds[index] ?? 0);
    if (entity === undefined) continue;
    const derivedBudgetM = Math.sqrt(
      entity.derivedMoveX * entity.derivedMoveX + entity.derivedMoveZ * entity.derivedMoveZ,
    );
    entity.derivedMoveX = 0;
    entity.derivedMoveZ = 0;
    if (!entity.active || entity.kind !== 'player') continue;

    const prevX = preTick[index * 3] ?? 0;
    const prevZ = preTick[index * 3 + 2] ?? 0;
    const distanceM = horizontalDistanceM(prevX, prevZ, entity.pos.x, entity.pos.z);
    validateAdvance(
      prevX,
      prevZ,
      entity.pos.x,
      entity.pos.z,
      entity.vel.y,
      SERVER_TICK_MS,
      check,
      derivedBudgetM,
    );
    check.position = !isLegalPosition(entity.pos.x, entity.pos.y, entity.pos.z, arena, radius);
    // 判定优先级：位置非法 > 垂直违规 > 超出硬上限 > 派生位移可解释（可疑） > 通过
    if (
      explainableByDerived(distanceM, limitM, derivedBudgetM) &&
      !check.vertical &&
      !check.position
    )
      continue;

    if (check.speed || check.vertical) deps.metrics.speedViolations += 1;
    const rejected =
      check.position ||
      check.vertical ||
      distanceM > hardCorrectLimitM(SERVER_TICK_MS, derivedBudgetM);
    if (!rejected) {
      deps.metrics.poseSuspects += 1;
      logPoseViolation(deps, room, entity.id, check, distanceM, derivedBudgetM, 'suspect');
      continue;
    }

    if (check.position) deps.metrics.poseRejected += 1;
    logPoseViolation(deps, room, entity.id, check, distanceM, derivedBudgetM, 'rejected');
    deps.metrics.hardCorrectTotal += 1;
    entity.pos.x = prevX;
    entity.pos.z = prevZ;
    entity.vel.x = 0;
    entity.vel.y = 0;
    entity.vel.z = 0;
  }
}

export function resolveShot(
  room: Room,
  deps: RoomDeps,
  shooterId: number,
  rewindMs: number,
): ShotResult {
  const out = shotScratch;
  out.hit = false;
  out.targetId = 0;
  out.clamped = rewindMs > REWIND_LIMIT_MS;
  out.rewindMs = Math.min(Math.max(rewindMs, 0), REWIND_LIMIT_MS);
  out.x = 0;
  out.y = 0;
  out.z = 0;
  if (out.clamped) deps.metrics.rewindClampedCount += 1;

  const world = room.world;
  const shooter = shooterId > 0 ? world.entities[shooterId - 1] : undefined;
  if (shooter === undefined || !shooter.active || shooter.kind !== 'player') return out;
  const eyeY = shooter.pos.y + world.config.player.eyeHeight;
  const direction = yawPitchToDirection(directionScratch, shooter.yaw, shooter.pitch);
  traceRay(
    world,
    room.history,
    shooterId,
    shooter.pos.x,
    eyeY,
    shooter.pos.z,
    direction.x,
    direction.y,
    direction.z,
    SHOT_MAX_DISTANCE_M,
    out.rewindMs,
    shotTraceScratch,
  );
  out.hit = shotTraceScratch.hit;
  out.targetId = shotTraceScratch.targetId;
  out.x = shotTraceScratch.x;
  out.y = shotTraceScratch.y;
  out.z = shotTraceScratch.z;
  return out;
}
