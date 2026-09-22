export const PROTOCOL_VERSION = 2 as const;

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
export * from './sim/events.ts';
export * from './sim/spatialGrid.ts';

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

export { MATCH_PHASE_NAMES, MATCH_PHASE_TRANSITIONS } from './match/phase.ts';
export { canTransition } from './match/transition.ts';
export type { IllegalTransition, TransitionResult } from './match/transition.ts';
export { createMatchState, createMatchStatePlayer } from './match/state.ts';
export {
  accuracyOf,
  accumulateAlive,
  accumulateDowned,
  createPlayerMatchStats,
  noteDown,
  noteHit,
  noteKill,
  noteRevive,
  noteShot,
  resetPlayerMatchStats,
  survivalMsOf,
} from './match/stats.ts';
export type { PlayerMatchStats } from './match/stats.ts';

export {
  EVENT_FRAME_HEADER_BYTES,
  NEW_ROOM_CODE,
  createSnapshotBaseline,
  createSnapshotMirror,
  decodeChat,
  decodeChatMessage,
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
  encodeChatMessage,
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
  baselinePresentIdsSorted,
  isValidRoomCode,
  mirrorIdsSorted,
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
  ChatMessage,
  DecodeFailure,
  DecodeResult,
  JoinRequest,
  PingRequest,
  ReadyRequest,
  SnapshotBaseline,
  SnapshotMirror,
  SnapshotRecord,
} from './net/codec.ts';

export {
  MS_PER_SECOND,
  applyCommandToState,
  collideStatic,
  integrateState,
  stepLocalPlayer,
} from './sim/localStep.ts';
export type { MoveConfig, MoveState } from './sim/localStep.ts';

export {
  POSE_FLOATS_PER_ENTRY,
  POSE_HISTORY_DEFAULT_MAX_PER_SLOT,
  POSE_HISTORY_DEFAULT_SLOT_MS,
  POSE_HISTORY_SLOTS,
  REWIND_LIMIT_MS,
  createPoseHistory,
  createSampledPose,
  recordPoseHistory,
  samplePoseAgo,
} from './sim/history.ts';
export type { PoseHistory, SampledPose } from './sim/history.ts';

export {
  clearRayHit,
  createRayHit,
  rayVsAabb,
  rayVsCapsule,
  rayVsSphere,
} from './combat/raycast.ts';
export type { RayHit } from './combat/raycast.ts';

export {
  WEAPONS,
  WEAPON_SLOT_ORDER,
  WEAPON_SLOT_COUNT,
  RESERVE_AMMO_INITIAL,
  SPREAD_GROWTH_PER_SHOT_DEG,
  SPREAD_MAX_DEG,
  SPREAD_DECAY_DELAY_MS,
  SPREAD_DECAY_PER_SECOND_DEG,
  RECOIL_PITCH_PER_SHOT_DEG,
  RECOIL_YAW_JITTER_DEG,
  weaponDefForSlot,
  rpmToIntervalMs,
  unitJitter,
  signedJitter,
} from './config/weapons.ts';
export type { WeaponDef, WeaponSlot } from './config/weapons.ts';
export {
  HIT_PART,
  HIT_PART_NAMES,
  BODY_PART_MULTIPLIER,
  HEAD_MIN_HEIGHT_RATIO,
  TORSO_MIN_HEIGHT_RATIO,
  ARMOR_ABSORB_RATIO,
  ARMOR_MAX,
  HEALTH_MAX,
  FALLOFF_MIN_MULTIPLIER,
  FRIENDLY_FIRE,
  RAGE,
  REVIVE,
  SHEEP_ELITE_STATE,
  partForHeight,
  partMultiplier,
} from './config/combat.ts';
export type { HitPart } from './config/combat.ts';
export {
  createWeaponState,
  resetWeaponState,
  activeWeaponDef,
  activeMag,
  isReloading,
  reloadRemainingMs,
  updateWeapon,
  tryFire,
  tryStartReload,
  cancelReload,
  switchSlot,
} from './combat/weapon.ts';
export type { WeaponState } from './combat/weapon.ts';
export { computeDamage, damageFor, createDamageResult } from './combat/damage.ts';
export type { DamageResult } from './combat/damage.ts';
export {
  createRageState,
  resetRageState,
  isRageActive,
  rageRatio,
  rageSecondsLeft,
  noteCombat,
  addKillRage,
  activateRage,
  updateRage,
} from './combat/rage.ts';
export type { RageState } from './combat/rage.ts';
export {
  REVIVE_OUTCOME,
  createDownedState,
  resetDownedState,
  markDowned,
  reviveRatio,
  canBeRevived,
  reviveStep,
  reviveTo,
} from './combat/downed.ts';
export type { DownedState, ReviveOutcome } from './combat/downed.ts';
export {
  SHOT_MAX_DISTANCE_M,
  DEG_TO_RAD,
  PITCH_LIMIT_RAD,
  createCombatContext,
  createShotTrace,
  traceRay,
  resolveCombat,
  reviveDownedForWaveClear,
  hasDownedTeammateInRange,
  reviveStateOf,
} from './combat/resolve.ts';
export type { CombatContext, CombatCounters, ShotTrace } from './combat/resolve.ts';
export type { CombatState } from './world.ts';
export { createCombatState, resetCombatState } from './world.ts';

export * from './ai/director.ts';
export * from './ai/flocking.ts';
export * from './ai/kingPhases.ts';
export * from './ai/sheepAttack.ts';
export * from './ai/sheepBrain.ts';
export * from './ai/steering.ts';
export * from './ai/targeting.ts';
export * from './config/sheep.ts';
export * from './config/waves.ts';
