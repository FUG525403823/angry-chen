import { SHEEP_STATE } from '@ac/shared';

export const SHEEP_VISUAL_FORM = Object.freeze({
  grunt: 0,
  ram: 1,
  elite: 2,
  king: 3,
} as const);

export const SHEEP_VISUAL_WINDUP_STATE = SHEEP_STATE.windup;
export const SHEEP_VISUAL_CHARGE_STATE = SHEEP_STATE.charge;
export const SHEEP_VISUAL_RANGED_STATE = SHEEP_STATE.ranged;
export const SHEEP_VISUAL_KING_STATES: readonly number[] = Object.freeze([
  SHEEP_STATE.kingPhase1,
  SHEEP_STATE.kingPhase2,
  SHEEP_STATE.kingPhase3,
  SHEEP_STATE.kingSummoning,
]);

export interface SheepVisualState {
  readonly form: number;
  readonly windup: boolean;
}

export interface SheepVisualTracker {
  resolve(id: number, state: number): SheepVisualState;
  formOf(id: number): number | undefined;
  forget(id: number): void;
  reset(): void;
  readonly size: number;
}

export function sheepFormFromState(state: number): number | undefined {
  if (SHEEP_VISUAL_KING_STATES.indexOf(state) >= 0) return SHEEP_VISUAL_FORM.king;
  if (state === SHEEP_VISUAL_RANGED_STATE) return SHEEP_VISUAL_FORM.elite;
  if (state === SHEEP_VISUAL_WINDUP_STATE || state === SHEEP_VISUAL_CHARGE_STATE) {
    return SHEEP_VISUAL_FORM.ram;
  }
  return undefined;
}

export function isWindupState(state: number): boolean {
  return state === SHEEP_VISUAL_WINDUP_STATE;
}

export function createSheepVisualTracker(): SheepVisualTracker {
  const forms = new Map<number, number>();
  const result: { form: number; windup: boolean } = {
    form: SHEEP_VISUAL_FORM.grunt,
    windup: false,
  };

  return {
    resolve(id: number, state: number): SheepVisualState {
      const observed = sheepFormFromState(state);
      const known = forms.get(id);
      if (observed !== undefined && (known === undefined || observed > known)) {
        forms.set(id, observed);
      }
      result.form = forms.get(id) ?? SHEEP_VISUAL_FORM.grunt;
      result.windup = isWindupState(state);
      return result;
    },
    formOf(id: number): number | undefined {
      return forms.get(id);
    },
    forget(id: number): void {
      forms.delete(id);
    },
    reset(): void {
      forms.clear();
    },
    get size(): number {
      return forms.size;
    },
  };
}
