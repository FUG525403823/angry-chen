import { sanitizeCommand, type Command, type CommandInput } from '../command.ts';
import { BUTTON } from '../config/index.ts';
import type { ArenaConfig } from '../config/arena.ts';
import type { PlayerConfig } from '../config/player.ts';

export const MS_PER_SECOND = 1000;

export interface MoveState {
  readonly pos: { x: number; y: number; z: number };
  readonly vel: { x: number; y: number; z: number };
  yaw: number;
  pitch: number;
}

export interface MoveConfig {
  readonly player: PlayerConfig;
  readonly arena: ArenaConfig;
  readonly radius: number;
}

export function applyCommandToState(
  state: MoveState,
  input: CommandInput | undefined,
  player: PlayerConfig,
  scratch: Command,
  speedMultiplier = 1,
): void {
  if (input === undefined) {
    state.vel.x = 0;
    state.vel.y = 0;
    state.vel.z = 0;
    return;
  }
  const command = sanitizeCommand(input, scratch);
  state.yaw = command.yaw;
  state.pitch = command.pitch;

  const speed =
    ((command.buttons & BUTTON.sprint) !== 0 ? player.sprintSpeed : player.moveSpeed) *
    speedMultiplier;
  const forwardX = Math.sin(command.yaw);
  const forwardZ = Math.cos(command.yaw);
  const rightX = Math.cos(command.yaw);
  const rightZ = -Math.sin(command.yaw);

  let vx = forwardX * command.moveX + rightX * command.moveY;
  let vz = forwardZ * command.moveX + rightZ * command.moveY;
  const magnitudeSq = vx * vx + vz * vz;
  if (magnitudeSq > 1) {
    const inverse = 1 / Math.sqrt(magnitudeSq);
    vx *= inverse;
    vz *= inverse;
  }

  state.vel.x = vx * speed;
  state.vel.y = 0;
  state.vel.z = vz * speed;
}

export function integrateState(state: MoveState, dtSeconds: number): void {
  state.pos.x += state.vel.x * dtSeconds;
  state.pos.y += state.vel.y * dtSeconds;
  state.pos.z += state.vel.z * dtSeconds;
}

export function collideStatic(state: MoveState, arena: ArenaConfig, radius: number): void {
  resolveBarn(state, arena, radius);
  clampToBounds(state, arena, radius);
}

function resolveBarn(state: MoveState, arena: ArenaConfig, radius: number): void {
  const barn = arena.barn;
  if (state.pos.y >= barn.maxY) return;
  const minX = barn.minX - radius;
  const maxX = barn.maxX + radius;
  const minZ = barn.minZ - radius;
  const maxZ = barn.maxZ + radius;
  if (state.pos.x <= minX || state.pos.x >= maxX) return;
  if (state.pos.z <= minZ || state.pos.z >= maxZ) return;

  const pushLeft = state.pos.x - minX;
  const pushRight = maxX - state.pos.x;
  const pushBack = state.pos.z - minZ;
  const pushForward = maxZ - state.pos.z;

  if (Math.min(pushLeft, pushRight) <= Math.min(pushBack, pushForward)) {
    state.pos.x = pushLeft < pushRight ? minX : maxX;
    state.vel.x = 0;
  } else {
    state.pos.z = pushBack < pushForward ? minZ : maxZ;
    state.vel.z = 0;
  }
}

function clampToBounds(state: MoveState, arena: ArenaConfig, radius: number): void {
  const limit = arena.halfSize - arena.fence.thickness / 2 - radius;
  if (state.pos.x > limit) {
    state.pos.x = limit;
    state.vel.x = 0;
  } else if (state.pos.x < -limit) {
    state.pos.x = -limit;
    state.vel.x = 0;
  }
  if (state.pos.z > limit) {
    state.pos.z = limit;
    state.vel.z = 0;
  } else if (state.pos.z < -limit) {
    state.pos.z = -limit;
    state.vel.z = 0;
  }
}

export function stepLocalPlayer(
  state: MoveState,
  input: CommandInput | undefined,
  config: MoveConfig,
  scratch: Command,
  dtMs: number,
  speedMultiplier = 1,
): void {
  applyCommandToState(state, input, config.player, scratch, speedMultiplier);
  integrateState(state, dtMs / MS_PER_SECOND);
  collideStatic(state, config.arena, config.radius);
}
