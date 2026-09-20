import {
  ERROR_CODE,
  LIMITS,
  MATCH_PHASE,
  OPCODE,
  PROTOCOL_VERSION,
  SNAPSHOT_RATE_X10,
  createCommand,
  createSnapshotBaseline,
  decodeChat,
  decodeCommand,
  decodeInteract,
  decodeJoin,
  decodePing,
  decodeReady,
  decodeSimpleFrame,
  encodeError,
  encodePong,
  encodeWelcome,
  sanitizeName,
  type Command,
  type SnapshotBaseline,
} from '@ac/shared';

import { formatLogLine } from './log.ts';
import type { Metrics } from './metrics.ts';
import { INTERMISSION_MS, broadcastMatchState } from './room.ts';
import type { RoomRegistry } from './rooms.ts';
import type { Connection } from './transport/types.ts';

export interface Session {
  readonly id: number;
  readonly connection: Connection;
  pid: number;
  name: string;
  roomCode: string | null;
  ready: boolean;
  weapon: number;
  kills: number;
  readonly command: Command;
  lastAckedCmdSeq: number;
  lastRecvTick: number;
  lagTicks: number;
  pingCount: number;
  malformedLogAtMs: number;
  interactAct: number;
  readonly baseline: SnapshotBaseline;
  readonly outbound: Uint8Array;
  readonly frameTimes: Float64Array;
  frameTimeCount: number;
  readonly queue: Command[];
  queueCount: number;
  readonly incoming: Command;
  strikes: number;
  closed: boolean;
  joinedAtMs: number;
}

export interface SessionDeps {
  readonly rooms: RoomRegistry;
  readonly metrics: Metrics;
  now(): number;
  monotonicNow(): number;
  log(message: string): void;
  onSessionEnd?(session: Session): void;
}

export function createSession(connection: Connection, nowMs: number): Session {
  return {
    id: connection.id,
    connection,
    pid: 0,
    name: '',
    roomCode: null,
    ready: false,
    weapon: 0,
    kills: 0,
    command: createCommand(),
    lastAckedCmdSeq: 0,
    lastRecvTick: 0,
    lagTicks: 0,
    pingCount: 0,
    malformedLogAtMs: Number.NEGATIVE_INFINITY,
    interactAct: 0,
    baseline: createSnapshotBaseline(),
    outbound: new Uint8Array(LIMITS.maxFrameBytes),
    frameTimes: new Float64Array(LIMITS.maxMessagesPerSecond + 1),
    frameTimeCount: 0,
    queue: [createCommand(), createCommand(), createCommand()],
    queueCount: 0,
    incoming: createCommand(),
    strikes: 0,
    closed: false,
    joinedAtMs: nowMs,
  };
}

function seqIsNewer(candidate: number, current: number): boolean {
  const delta = (candidate - current) & 0xffff;
  return delta !== 0 && delta < 0x8000;
}

function sendError(deps: SessionDeps, session: Session, code: number, message: string): void {
  const size = encodeError(code, message, session.outbound);
  session.connection.send(session.outbound.subarray(0, size));
  deps.metrics.errorsSent += 1;
  deps.metrics.framesOut += 1;
  deps.metrics.bytesOut += size;
}

const malformedLogIntervalMs = 5000;

function logMalformed(deps: SessionDeps, session: Session, reason: string): void {
  const nowMs = deps.now();
  if (nowMs - session.malformedLogAtMs < malformedLogIntervalMs) return;
  session.malformedLogAtMs = nowMs;
  deps.log(
    formatLogLine('warn', 'malformedFrame', {
      count: deps.metrics.malformedFrames,
      session: session.id,
      reason,
    }),
  );
}

function dropFrame(deps: SessionDeps, session: Session, reason: string): void {
  deps.metrics.malformedFrames += 1;
  logMalformed(deps, session, reason);
  sendError(deps, session, ERROR_CODE.malformedFrame, reason);
}

export function closeSession(
  deps: SessionDeps,
  session: Session,
  code: number,
  reason: string,
): void {
  if (session.closed) return;
  session.closed = true;
  deps.rooms.leave(session, deps.now());
  session.connection.close(code, reason);
  if (deps.onSessionEnd !== undefined) deps.onSessionEnd(session);
}

function withinRateLimit(deps: SessionDeps, session: Session, nowMs: number): boolean {
  const times = session.frameTimes;
  let kept = 0;
  for (let i = 0; i < session.frameTimeCount; i += 1) {
    const at = times[i];
    if (at !== undefined && nowMs - at < LIMITS.rateWindowMs) {
      times[kept] = at;
      kept += 1;
    }
  }
  session.frameTimeCount = kept;
  if (kept >= LIMITS.maxMessagesPerSecond) {
    deps.metrics.rateLimitedFrames += 1;
    session.strikes += 1;
    if (session.strikes >= LIMITS.rateStrikesBeforeDisconnect) {
      sendError(deps, session, ERROR_CODE.rateLimited, 'rate limit exceeded');
      closeSession(deps, session, 1008, 'rate limited');
    }
    return false;
  }
  if (session.strikes > 0 && kept === 0) session.strikes = 0;
  times[kept] = nowMs;
  session.frameTimeCount = kept + 1;
  return true;
}

function handleJoin(deps: SessionDeps, session: Session, frame: Uint8Array, nowMs: number): void {
  if (session.roomCode !== null) {
    dropFrame(deps, session, 'join while already in a room');
    return;
  }
  const decoded = decodeJoin(frame);
  if (!decoded.ok) {
    dropFrame(deps, session, 'join ' + decoded.reason);
    return;
  }
  if (decoded.value.protocolVersion !== PROTOCOL_VERSION) {
    sendError(deps, session, ERROR_CODE.protocolMismatch, 'protocol version mismatch');
    closeSession(deps, session, 1002, 'protocol mismatch');
    return;
  }
  const name = sanitizeName(decoded.value.name);
  if (name === null) {
    sendError(deps, session, ERROR_CODE.invalidName, 'invalid name');
    return;
  }
  const outcome = deps.rooms.join(decoded.value.roomCode, session, nowMs);
  if (!outcome.ok) {
    if (outcome.reason === 'room-not-found')
      sendError(deps, session, ERROR_CODE.roomNotFound, 'room not found');
    else if (outcome.reason === 'match-in-progress')
      sendError(deps, session, ERROR_CODE.matchInProgress, 'match in progress');
    else sendError(deps, session, ERROR_CODE.roomFull, 'room full');
    return;
  }
  session.name = name;
  deps.metrics.joins += 1;
  const size = encodeWelcome(
    {
      pid: session.pid,
      roomCode: outcome.room.code,
      protocolVersion: PROTOCOL_VERSION,
      tick: outcome.room.world.tick,
      serverTimeMs: nowMs,
    },
    session.outbound,
  );
  session.connection.send(session.outbound.subarray(0, size));
  deps.metrics.framesOut += 1;
  deps.metrics.bytesOut += size;
}

function handlePing(deps: SessionDeps, session: Session, frame: Uint8Array, nowMs: number): void {
  const decoded = decodePing(frame);
  if (!decoded.ok) {
    dropFrame(deps, session, 'ping ' + decoded.reason);
    return;
  }
  session.pingCount += 1;
  deps.metrics.pings += 1;
  session.lastRecvTick = decoded.value.lastRecvTick;
  const room = deps.rooms.roomOf(session);
  if (room !== undefined)
    session.lagTicks = Math.max(0, room.world.tick - decoded.value.lastRecvTick);
  const size = encodePong(
    {
      clientTimeMs: decoded.value.clientTimeMs,
      serverTimeMs: nowMs,
      snapshotRateX10: room === undefined ? SNAPSHOT_RATE_X10 : room.snapshotRateX10,
    },
    session.outbound,
  );
  session.connection.send(session.outbound.subarray(0, size));
  deps.metrics.framesOut += 1;
  deps.metrics.bytesOut += size;
}

function enqueueCommand(session: Session, incoming: Command): void {
  const limit = LIMITS.maxCommandsPerSession;
  const count = session.queueCount;
  let index = 0;
  while (index < count) {
    const slot = session.queue[index];
    if (slot === undefined) break;
    if (slot.seq === incoming.seq) return;
    if (seqIsNewer(incoming.seq, slot.seq)) index += 1;
    else break;
  }
  const total = count + 1;
  const drop = Math.max(0, total - limit);
  if (index < drop) return;
  const kept = Math.min(total - drop, limit);
  const writeAt = index - drop;
  if (drop === 0) {
    for (let i = kept - 1; i > writeAt; i -= 1) {
      const to = session.queue[i];
      const from = session.queue[i - 1];
      if (to !== undefined && from !== undefined) Object.assign(to, from);
    }
  } else {
    for (let i = 0; i < kept; i += 1) {
      const src = i + drop;
      const to = session.queue[i];
      const from = session.queue[src];
      if (to !== undefined && from !== undefined) Object.assign(to, from);
    }
  }
  const target = session.queue[writeAt];
  if (target !== undefined) Object.assign(target, incoming);
  session.queueCount = kept;
  const newest = session.queue[kept - 1];
  if (newest !== undefined) Object.assign(session.command, newest);
}

function handleInput(deps: SessionDeps, session: Session, frame: Uint8Array): void {
  const decoded = decodeCommand(frame, session.incoming);
  if (!decoded.ok) {
    dropFrame(deps, session, 'input ' + decoded.reason);
    return;
  }
  enqueueCommand(session, session.incoming);
  if (seqIsNewer(decoded.value.seq, session.lastAckedCmdSeq))
    session.lastAckedCmdSeq = decoded.value.seq;
}

function handleReady(deps: SessionDeps, session: Session, frame: Uint8Array): void {
  const decoded = decodeReady(frame);
  if (!decoded.ok) {
    dropFrame(deps, session, 'ready ' + decoded.reason);
    return;
  }
  const room = deps.rooms.roomOf(session);
  if (room === undefined) return;
  if (room.phase !== 0 && room.phase !== 1) {
    deps.metrics.droppedFrames += 1;
    return;
  }
  session.ready = decoded.value.ready;
  session.weapon = decoded.value.weapon;
  broadcastMatchState(room);
}

function handleChat(deps: SessionDeps, session: Session, frame: Uint8Array): void {
  const decoded = decodeChat(frame);
  if (!decoded.ok) {
    dropFrame(deps, session, 'chat ' + decoded.reason);
    return;
  }
  deps.metrics.chatMessages += 1;
  deps.log(
    'chat session=' +
      String(session.id) +
      ' room=' +
      String(session.roomCode) +
      ' text=' +
      decoded.value,
  );
}

function handleInteract(deps: SessionDeps, session: Session, frame: Uint8Array): void {
  const decoded = decodeInteract(frame);
  if (!decoded.ok) {
    dropFrame(deps, session, 'interact ' + decoded.reason);
    return;
  }
  session.interactAct = decoded.value;
  deps.metrics.interactRequests += 1;
}

function handleSimple(
  deps: SessionDeps,
  session: Session,
  frame: Uint8Array,
  opcode: number,
): void {
  const decoded = decodeSimpleFrame(frame, opcode);
  if (!decoded.ok) {
    dropFrame(deps, session, 'simple ' + decoded.reason);
    return;
  }
  if (opcode === OPCODE.respawn) {
    deps.metrics.respawnRequests += 1;
    return;
  }
  if (opcode === OPCODE.leave) {
    deps.metrics.leaves += 1;
    closeSession(deps, session, 1000, 'left');
    return;
  }
  deps.metrics.startMatchRequests += 1;
  const room = deps.rooms.roomOf(session);
  if (room === undefined) return;
  if (room.sessions[0] !== session) {
    sendError(deps, session, ERROR_CODE.notHost, 'only the host can start the match');
    return;
  }
  if (room.phase !== 0) {
    deps.metrics.droppedFrames += 1;
    return;
  }
  room.phase = MATCH_PHASE.intermission;
  room.intermissionMs = INTERMISSION_MS;
  broadcastMatchState(room);
}

export function handleSessionFrame(deps: SessionDeps, session: Session, frame: Uint8Array): void {
  if (session.closed) return;
  deps.metrics.framesIn += 1;
  deps.metrics.bytesIn += frame.length;
  if (frame.length === 0) {
    dropFrame(deps, session, 'empty frame');
    return;
  }
  if (frame.length > LIMITS.maxFrameBytes) {
    deps.metrics.malformedFrames += 1;
    logMalformed(deps, session, 'frame too large: ' + String(frame.length));
    closeSession(deps, session, 1009, 'frame too large');
    return;
  }
  const nowMs = deps.now();
  if (!withinRateLimit(deps, session, nowMs)) return;
  const opcode = frame[0] ?? -1;
  switch (opcode) {
    case OPCODE.join:
      handleJoin(deps, session, frame, nowMs);
      break;
    case OPCODE.inputCmd:
      handleInput(deps, session, frame);
      break;
    case OPCODE.ready:
      handleReady(deps, session, frame);
      break;
    case OPCODE.chat:
      handleChat(deps, session, frame);
      break;
    case OPCODE.interact:
      handleInteract(deps, session, frame);
      break;
    case OPCODE.ping:
      handlePing(deps, session, frame, nowMs);
      break;
    case OPCODE.respawn:
    case OPCODE.leave:
    case OPCODE.startMatch:
      handleSimple(deps, session, frame, opcode);
      break;
    default:
      dropFrame(deps, session, 'unknown opcode ' + String(opcode));
      break;
  }
}

export function attachSession(deps: SessionDeps, session: Session): void {
  session.connection.onMessage((frame: Uint8Array) => {
    try {
      handleSessionFrame(deps, session, frame);
    } catch (error) {
      deps.metrics.malformedFrames += 1;
      deps.log('frame handler threw: ' + String(error));
    }
  });
  session.connection.onClose(() => {
    if (session.closed) return;
    session.closed = true;
    deps.rooms.leave(session, deps.now());
    deps.metrics.leaves += 1;
    if (deps.onSessionEnd !== undefined) deps.onSessionEnd(session);
  });
}
