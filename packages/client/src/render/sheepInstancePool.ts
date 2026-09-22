import type { ChargeWarning, SheepInstance } from './sheepModel.ts';

/** O06：羊群实例环形池（一帧内有效）。字段可变，返回前由调用方完整覆写。 */
export interface SheepInstancePool {
  /** 同帧内每次调用都给出不同实例；越界时反复给出池内最后一个并只告警一次。 */
  acquire(index: number): SheepInstance;
  /** 新帧开始调用；只复位游标，不分配。 */
  reset(): void;
  readonly size: number;
}

export interface ChargeWarningPool {
  acquire(index: number): ChargeWarning;
  reset(): void;
  readonly size: number;
}

function createSheepInstance(): SheepInstance {
  return { id: 0, form: 0, x: 0, y: 0, z: 0, yaw: 0, hpRatio: 0, windup: false, state: 0 };
}

function createChargeWarning(): ChargeWarning {
  return { id: 0, x: 0, z: 0, radiusM: 0 };
}

export function createSheepInstancePool(capacity: number): SheepInstancePool {
  const size = Math.max(1, Math.floor(capacity));
  const pool: SheepInstance[] = [];
  for (let i = 0; i < size; i += 1) pool.push(createSheepInstance());
  let cursor = 0;
  let warned = false;
  return {
    acquire(index: number): SheepInstance {
      const slot = cursor < size ? cursor : size - 1;
      cursor += 1;
      if (cursor > size && !warned) {
        warned = true;
        console.warn('sheep instance pool overflow at index ' + String(index));
      }
      return pool[slot] ?? pool[size - 1]!;
    },
    reset(): void {
      cursor = 0;
    },
    size,
  };
}

export function createChargeWarningPool(capacity: number): ChargeWarningPool {
  const size = Math.max(1, Math.floor(capacity));
  const pool: ChargeWarning[] = [];
  for (let i = 0; i < size; i += 1) pool.push(createChargeWarning());
  let cursor = 0;
  let warned = false;
  return {
    acquire(index: number): ChargeWarning {
      const slot = cursor < size ? cursor : size - 1;
      cursor += 1;
      if (cursor > size && !warned) {
        warned = true;
        console.warn('charge warning pool overflow at index ' + String(index));
      }
      return pool[slot] ?? pool[size - 1]!;
    },
    reset(): void {
      cursor = 0;
    },
    size,
  };
}
