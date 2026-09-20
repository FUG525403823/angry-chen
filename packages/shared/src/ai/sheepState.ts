export const SHEEP_KIND_CODE = Object.freeze({ grunt: 0, ram: 1, elite: 2, king: 3 } as const);

export const AGGRO_SLOTS = 8;

export interface TargetState {
  readonly aggro: Float64Array;
  targetId: number;
  targetDistanceM: number;
}

export function createTargetState(): TargetState {
  return { aggro: new Float64Array(AGGRO_SLOTS), targetId: 0, targetDistanceM: 0 };
}

export function resetTargetState(state: TargetState): void {
  state.aggro.fill(0);
  state.targetId = 0;
  state.targetDistanceM = 0;
}
