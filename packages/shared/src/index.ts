export const PROTOCOL_VERSION = 1 as const;

export const SERVER_TICK_MS = 50 as const;

export {
  ARENA,
  BUTTON,
  BUTTON_MASK_ALL,
  CONFIG,
  ENTITY,
  INPUT,
  MAX_ENTITIES,
  NET,
  PLAYER,
  SNAPSHOT_FLAG,
  SNAPSHOT_MAX_ENTITIES,
} from './config/index.ts';
export type {
  ArenaConfig,
  EntityConfig,
  InputConfig,
  NetConfig,
  PlayerConfig,
  WorldConfig,
} from './config/index.ts';

export {
  WEAPON_PISTOL,
  WEAPON_RIFLE,
  WEAPON_SHOTGUN,
  createCommand,
  sanitizeCommand,
} from './command.ts';
export type { Command, CommandInput } from './command.ts';

export {
  TWO_PI,
  addScaled,
  approxEq,
  clamp,
  clamp01,
  copyVec3,
  createVec3,
  forwardFromYaw,
  length,
  lengthSq,
  normalize,
  rightFromYaw,
  setVec3,
  wrapAngle,
  yawPitchToDirection,
} from './math.ts';
export type { Vec3 } from './math.ts';

export { createRng, mulberry32, rngFloat, rngInt, rngRange } from './rng.ts';
export type { Rng, RngStream } from './rng.ts';

export {
  countActive,
  createEntity,
  createSimEvent,
  createWorld,
  despawnEntity,
  getEntity,
  radiusOf,
  spawnEntity,
} from './world.ts';
export type {
  Entity,
  EntityId,
  EntityKind,
  SimEvent,
  SpawnFailureReason,
  SpawnResult,
  Team,
  World,
} from './world.ts';

export {
  computeEntityFlags,
  createSnapshot,
  createSnapshotEntity,
  snapshotWorld,
} from './snapshot.ts';
export type { Snapshot, SnapshotEntity } from './snapshot.ts';

export { stepWorld } from './sim.ts';

export {
  CLIENT_OPCODE_LIST,
  ENTITY_KIND_CODE,
  ENTITY_KIND_NAMES,
  ERROR_CODE,
  EVENT_TYPE,
  EVENT_TYPE_NAMES,
  HIT_FLAG,
  LIMITS,
  MATCH_PHASE,
  OPCODE,
  QUANT,
  SERVER_OPCODE_LIST,
  SNAPSHOT_HEADER_BYTES,
  SNAPSHOT_RATE_X10,
  SNAPSHOT_RECORD_BYTES,
} from './net/protocol.ts';
export type {
  ErrorCode,
  ErrorFrame,
  EventTypeName,
  MatchPhase,
  MatchState,
  MatchStatePlayer,
  Opcode,
  Pong,
  Welcome,
} from './net/protocol.ts';

export {
  EVENT_FRAME_HEADER_BYTES,
  NEW_ROOM_CODE,
  createSnapshotBaseline,
  createSnapshotMirror,
  decodeChat,
  decodeCommand,
  decodeError,
  decodeEventFrame,
  decodeInteract,
  decodeJoin,
  decodeMatchState,
  decodePing,
  decodePong,
  decodeReady,
  decodeSimpleFrame,
  decodeSnapshot,
  decodeWelcome,
  dequantizeAngle,
  dequantizeAxis,
  dequantizePosition,
  dequantizeRatio,
  encodeChat,
  encodeCommand,
  encodeError,
  encodeEventFrame,
  encodeEventPayload,
  encodeInteract,
  encodeJoin,
  encodeMatchState,
  encodePing,
  encodePong,
  encodeReady,
  encodeSimpleFrame,
  encodeSnapshot,
  encodeWelcome,
  isValidRoomCode,
  quantizeAngle,
  quantizeAxis,
  quantizePosition,
  quantizeRatio,
  quantizeSnapshotEntity,
  readSnapshotRecord,
  sanitizeChat,
  sanitizeName,
  truncateUtf8,
  utf8Length,
} from './net/codec.ts';
export type {
  DecodeFailure,
  DecodeResult,
  JoinRequest,
  PingRequest,
  ReadyRequest,
  SnapshotBaseline,
  SnapshotMirror,
  SnapshotRecord,
} from './net/codec.ts';
