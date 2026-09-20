import type { ErrorSmoother } from '../render/errorSmoother.ts';
import type { LocalAuthority } from '../net/state.ts';

export type { LocalAuthority };
import type { CommandBuffer } from './commandBuffer.ts';
import type { Predictor } from './predictor.ts';

export const HARD_CORRECT_THRESHOLD_M = 1.0;

export function createLocalAuthority(): LocalAuthority {
  return { found: false, x: 0, y: 0, z: 0, yaw: 0, pitch: 0, lastAckedSeq: 0, tick: 0 };
}

export interface Reconciler {
  readonly hardCorrectCount: number;
  readonly lastErrorM: number;
  readonly maxErrorM: number;
  readonly corrections: number;
  reconcile(
    authority: LocalAuthority,
    buffer: CommandBuffer,
    predictor: Predictor,
    smoother: ErrorSmoother,
  ): number;
  reset(): void;
}

export function createReconciler(): Reconciler {
  let hardCorrectCount = 0;
  let lastErrorM = 0;
  let maxErrorM = 0;
  let corrections = 0;

  return {
    get hardCorrectCount(): number {
      return hardCorrectCount;
    },
    get lastErrorM(): number {
      return lastErrorM;
    },
    get maxErrorM(): number {
      return maxErrorM;
    },
    get corrections(): number {
      return corrections;
    },
    reconcile(
      authority: LocalAuthority,
      buffer: CommandBuffer,
      predictor: Predictor,
      smoother: ErrorSmoother,
    ): number {
      if (!authority.found) return 0;

      const state = predictor.state;
      const beforeX = state.pos.x;
      const beforeY = state.pos.y;
      const beforeZ = state.pos.z;

      predictor.setAuthoritative(
        authority.x,
        authority.y,
        authority.z,
        authority.yaw,
        authority.pitch,
      );
      buffer.ackUpTo(authority.lastAckedSeq);

      const size = buffer.size;
      let replayed = 0;
      for (let i = 0; i < size; i += 1) {
        const command = buffer.at(i);
        if (command === undefined) break;
        predictor.stepWith(command);
        replayed += 1;
      }

      const dx = beforeX - state.pos.x;
      const dy = beforeY - state.pos.y;
      const dz = beforeZ - state.pos.z;
      const error = Math.sqrt(dx * dx + dy * dy + dz * dz);
      lastErrorM = error;
      if (error > maxErrorM) maxErrorM = error;
      corrections += 1;

      if (error > HARD_CORRECT_THRESHOLD_M) {
        hardCorrectCount += 1;
        smoother.reset();
        return replayed;
      }
      smoother.add(dx, dy, dz);
      return replayed;
    },
    reset(): void {
      hardCorrectCount = 0;
      lastErrorM = 0;
      maxErrorM = 0;
      corrections = 0;
    },
  };
}
