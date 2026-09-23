import {
  SERVER_TICK_MS,
  applyCommandToState,
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
      // 刻意不清零 accumulatorMs：它是"渲染节拍余量"，不是模拟状态。
      // 清零会让 20Hz 和解后的两帧渲染位置原地不动（相机每 3 帧才挪一次 = 画面一顿一顿）。
    },
    stepWith(command: Command | undefined): void {
      stepLocalPlayer(state, command, activeConfig, PREDICTION_SUBSTEP_MS);
    },
    advance(dtMs: number, command: Command | undefined): number {
      if (!Number.isFinite(dtMs) || dtMs <= 0) return 0;
      accumulatorMs += Math.min(dtMs, PREDICTION_SUBSTEP_MS * MAX_SUBSTEPS_PER_FRAME);
      let steps = 0;
      while (accumulatorMs >= PREDICTION_SUBSTEP_MS && steps < MAX_SUBSTEPS_PER_FRAME) {
        stepLocalPlayer(state, command, activeConfig, PREDICTION_SUBSTEP_MS);
        accumulatorMs -= PREDICTION_SUBSTEP_MS;
        steps += 1;
      }
      if (accumulatorMs > PREDICTION_SUBSTEP_MS) accumulatorMs = PREDICTION_SUBSTEP_MS;
      // 本帧没攒够子步时也要把渲染速度刷成当前意图：renderPosition 用 state.vel 做子步内线性外推，
      // 速度为 0 的帧渲染位置完全不动（顿挫的另一半根因）。
      if (steps === 0) applyCommandToState(state, command, activeConfig.player);
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
