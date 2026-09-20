import { BUTTON, INPUT, createCommand, type Command } from '@ac/shared';

export const COMMAND_RATE_HZ = 30;
export const COMMAND_INTERVAL_MS = 1000 / COMMAND_RATE_HZ;
export const MOUSE_SENSITIVITY_SCALE = 0.001;

export const KEY_BINDINGS = Object.freeze({
  KeyW: 'forward',
  KeyS: 'back',
  KeyA: 'left',
  KeyD: 'right',
  ShiftLeft: BUTTON.sprint,
  ShiftRight: BUTTON.sprint,
  Space: BUTTON.jump,
  KeyR: BUTTON.reload,
  KeyE: BUTTON.interact,
  KeyF: BUTTON.rage,
  KeyQ: BUTTON.switchWeapon,
} as const);

export interface SamplerState {
  moveX: number;
  moveY: number;
  yaw: number;
  pitch: number;
  buttons: number;
}

export interface InputSampler {
  readonly state: SamplerState;
  setKey(code: string, down: boolean): boolean;
  addMouse(deltaX: number, deltaY: number): void;
  setSensitivity(value: number): void;
  setYaw(yaw: number): void;
  setPitch(pitch: number): void;
  update(): boolean;
  flush(): boolean;
  getSeq(): number;
}

export interface SamplerDeps {
  now(): number;
  emit(command: Command): void;
  command?: Command;
  getTick?: () => number;
  getSensitivity?: () => number;
}

export function createInputSampler(deps: SamplerDeps): InputSampler {
  const command = deps.command ?? createCommand();
  const state: SamplerState = { moveX: 0, moveY: 0, yaw: 0, pitch: 0, buttons: 0 };
  const pressed = new Set<string>();
  let sensitivity = 1;
  let seq = 0;
  let lastSentAtMs = Number.NEGATIVE_INFINITY;

  function clampPitch(value: number): number {
    if (value > INPUT.pitchLimitRad) return INPUT.pitchLimitRad;
    if (value < -INPUT.pitchLimitRad) return -INPUT.pitchLimitRad;
    return value;
  }

  function recomputeAxes(): void {
    const forward = (pressed.has('forward') ? 1 : 0) - (pressed.has('back') ? 1 : 0);
    const strafe = (pressed.has('right') ? 1 : 0) - (pressed.has('left') ? 1 : 0);
    state.moveX = forward * INPUT.moveAxisLimit;
    state.moveY = strafe * INPUT.moveAxisLimit;
  }

  function emit(nowMs: number): boolean {
    command.seq = seq & 0xffff;
    seq += 1;
    command.tick = deps.getTick === undefined ? 0 : deps.getTick();
    command.moveX = state.moveX;
    command.moveY = state.moveY;
    command.yaw = state.yaw;
    command.pitch = state.pitch;
    command.buttons = state.buttons;
    command.switchTo = 0;
    lastSentAtMs = nowMs;
    deps.emit(command);
    return true;
  }

  return {
    state,
    setKey(code: string, down: boolean): boolean {
      const binding: string | number | undefined = KEY_BINDINGS[code as keyof typeof KEY_BINDINGS];
      if (binding === undefined) return false;
      if (typeof binding === 'number') {
        if (down) state.buttons |= binding;
        else state.buttons &= ~binding;
      } else if (down) {
        pressed.add(binding);
      } else {
        pressed.delete(binding);
      }
      recomputeAxes();
      emit(deps.now());
      return true;
    },
    addMouse(deltaX: number, deltaY: number): void {
      const scale = MOUSE_SENSITIVITY_SCALE * sensitivity;
      state.yaw += deltaX * scale;
      state.pitch = clampPitch(state.pitch - deltaY * scale);
    },
    setSensitivity(value: number): void {
      if (Number.isFinite(value) && value > 0) sensitivity = value;
    },
    setYaw(yaw: number): void {
      state.yaw = yaw;
    },
    setPitch(pitch: number): void {
      state.pitch = clampPitch(pitch);
    },
    update(): boolean {
      const nowMs = deps.now();
      if (nowMs - lastSentAtMs < COMMAND_INTERVAL_MS) return false;
      return emit(nowMs);
    },
    flush(): boolean {
      return emit(deps.now());
    },
    getSeq(): number {
      return seq;
    },
  };
}
