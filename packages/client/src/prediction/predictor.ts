import {
  SERVER_TICK_MS,
  createCommand,
  stepLocalPlayer,
  type Command,
  type MoveConfig,
  type MoveState,
} from '@ac/shared';

export const PREDICTION_SUBSTEP_MS = SERVER_TICK_MS;
export const MAX_SUBSTEPS_PER_FRAME = 5;

export interface PredictedPosition {
  x: number;
  y: number;
  z: number;
}

export interface Predictor {
  readonly state: MoveState;
  readonly pendingMs: number;
  setConfig(config: MoveConfig): void;
  setAuthoritative(x: number, y: number, z: number, yaw: number, pitch: number): void;
  stepWith(command: Command | undefined): void;
  advance(dtMs: number, command: Command | undefined): number;
  renderPosition(out: PredictedPosition): PredictedPosition;
  reset(): void;
}

export function createPredictor(config: MoveConfig): Predictor {
  const state: MoveState = {
    pos: { x: 0, y: 0, z: 0 },
    vel: { x: 0, y: 0, z: 0 },
    yaw: 0,
    pitch: 0,
  };
  const scratch = createCommand();
  let activeConfig = config;
  let accumulatorMs = 0;

  return {
    state,
    get pendingMs(): number {
      return accumulatorMs;
    },
    setConfig(next: MoveConfig): void {
      activeConfig = next;
    },
    setAuthoritative(x: number, y: number, z: number, yaw: number, pitch: number): void {
      state.pos.x = x;
      state.pos.y = y;
      state.pos.z = z;
      state.vel.x = 0;
      state.vel.y = 0;
      state.vel.z = 0;
      state.yaw = yaw;
      state.pitch = pitch;
      accumulatorMs = 0;
    },
    stepWith(command: Command | undefined): void {
      stepLocalPlayer(state, command, activeConfig, scratch, PREDICTION_SUBSTEP_MS);
    },
    advance(dtMs: number, command: Command | undefined): number {
      if (!Number.isFinite(dtMs) || dtMs <= 0) return 0;
      accumulatorMs += Math.min(dtMs, PREDICTION_SUBSTEP_MS * MAX_SUBSTEPS_PER_FRAME);
      let steps = 0;
      while (accumulatorMs >= PREDICTION_SUBSTEP_MS && steps < MAX_SUBSTEPS_PER_FRAME) {
        stepLocalPlayer(state, command, activeConfig, scratch, PREDICTION_SUBSTEP_MS);
        accumulatorMs -= PREDICTION_SUBSTEP_MS;
        steps += 1;
      }
      if (accumulatorMs > PREDICTION_SUBSTEP_MS) accumulatorMs = PREDICTION_SUBSTEP_MS;
      return steps;
    },
    renderPosition(out: PredictedPosition): PredictedPosition {
      const seconds = accumulatorMs / 1000;
      out.x = state.pos.x + state.vel.x * seconds;
      out.y = state.pos.y + state.vel.y * seconds;
      out.z = state.pos.z + state.vel.z * seconds;
      return out;
    },
    reset(): void {
      accumulatorMs = 0;
      state.vel.x = 0;
      state.vel.y = 0;
      state.vel.z = 0;
    },
  };
}
