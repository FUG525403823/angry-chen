import { NET } from '../config/index.ts';

export const OPCODE = Object.freeze({
  join: 0x01,
  inputCmd: 0x02,
  ready: 0x03,
  chat: 0x04,
  interact: 0x05,
  ping: 0x06,
  respawn: 0x07,
  leave: 0x08,
  startMatch: 0x09,
  welcome: 0x81,
  snapshot: 0x82,
  event: 0x83,
  matchState: 0x84,
  pong: 0x85,
  error: 0x86,
} as const);

export type Opcode = (typeof OPCODE)[keyof typeof OPCODE];

export const CLIENT_OPCODE_LIST: readonly number[] = Object.freeze([
  OPCODE.join,
  OPCODE.inputCmd,
  OPCODE.ready,
  OPCODE.chat,
  OPCODE.interact,
  OPCODE.ping,
  OPCODE.respawn,
  OPCODE.leave,
  OPCODE.startMatch,
]);

export const SERVER_OPCODE_LIST: readonly number[] = Object.freeze([
  OPCODE.welcome,
  OPCODE.snapshot,
  OPCODE.event,
  OPCODE.matchState,
  OPCODE.pong,
  OPCODE.error,
]);

export const ERROR_CODE = Object.freeze({
  protocolMismatch: 1,
  roomNotFound: 2,
  roomFull: 3,
  rateLimited: 4,
  malformedFrame: 5,
  invalidName: 6,
  matchInProgress: 7,
  serverShutdown: 8,
  notHost: 9,
} as const);

export type ErrorCode = (typeof ERROR_CODE)[keyof typeof ERROR_CODE];

export const EVENT_TYPE = Object.freeze({
  playerHit: 1,
  sheepKilled: 2,
  waveStart: 3,
  waveClear: 4,
  playerDowned: 5,
  reviveProgress: 6,
  reviveDone: 7,
  rageActivated: 8,
  matchEnded: 9,
} as const);

export type EventTypeName = keyof typeof EVENT_TYPE;

export const EVENT_TYPE_NAMES: readonly EventTypeName[] = Object.freeze([
  'playerHit',
  'sheepKilled',
  'waveStart',
  'waveClear',
  'playerDowned',
  'reviveProgress',
  'reviveDone',
  'rageActivated',
  'matchEnded',
]);

export const HIT_FLAG = Object.freeze({ headshot: 1, downed: 2, killed: 4 } as const);

export const ENTITY_KIND_CODE = Object.freeze({
  player: 0,
  sheep: 1,
  projectile: 2,
  pickup: 3,
} as const);

export const ENTITY_KIND_NAMES: readonly ('player' | 'sheep' | 'projectile' | 'pickup')[] =
  Object.freeze(['player', 'sheep', 'projectile', 'pickup']);

export const MATCH_PHASE = Object.freeze({
  lobby: 0,
  intermission: 1,
  playing: 2,
  ended: 3,
} as const);

export type MatchPhase = (typeof MATCH_PHASE)[keyof typeof MATCH_PHASE];

export const QUANT = Object.freeze({
  centimeterPerMeter: 100,
  moveAxisScale: 127,
  angleUnits: 65536,
  hpUnits: 255,
  maxPositionCm: 32767,
  minPositionCm: -32768,
  maxAngleUnit: 65535,
} as const);

export const LIMITS = Object.freeze({
  maxFrameBytes: 8192,
  maxMessagesPerSecond: 60,
  rateWindowMs: 1000,
  rateStrikesBeforeDisconnect: 3,
  minNameBytes: 1,
  maxNameBytes: 12,
  maxChatBytes: 64,
  maxCommandsPerSession: 3,
  maxPlayersPerRoom: 4,
  maxRooms: 64,
  snapshotMaxEntities: NET.snapshotMaxEntities,
  maxSnapshotRecordsPerFrame: 255,
  maxRemovedPerFrame: 255,
  tickCatchUpLimit: 5,
  fullSnapshotIntervalTicks: 40,
  emptyRoomReclaimMs: 60000,
  roomCodeLength: 4,
  roomCodeAlphabet: 'ABCDEFGHJKMNPQRSTUVWXYZ23456789',
  roomCodeExcludedChars: 'ILO01',
  roomCodeAttempts: 32,
} as const);

export const SNAPSHOT_RATE_X10 = 200;

export const U8_MAX = 255;

export const SNAPSHOT_RECORD_BYTES = 15;

export const SNAPSHOT_HEADER_BYTES = 1 + 4 + 4 + 2 + 4 + 1;

export interface Welcome {
  pid: number;
  roomCode: string;
  protocolVersion: number;
  tick: number;
  serverTimeMs: number;
}

export interface Pong {
  clientTimeMs: number;
  serverTimeMs: number;
  snapshotRateX10: number;
}

export interface ErrorFrame {
  code: number;
  message: string;
}

export interface MatchStatePlayer {
  pid: number;
  name: string;
  ready: boolean;
  weapon: number;
  hpRatio: number;
  kills: number;
  mag: number;
  reserve: number;
  reloadLeft10Ms: number;
  rage: number;
  rageLeft100Ms: number;
  downed: boolean;
  reviveRatio255: number;
}

export interface MatchState {
  phase: number;
  wave: number;
  intermissionMs: number;
  players: MatchStatePlayer[];
}
