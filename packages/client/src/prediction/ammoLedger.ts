import { COMMAND_BUFFER_CAPACITY } from './commandBuffer.ts';

/** O07：账本对外的裁决结果（**同一对象被复用**，调用方不得跨帧持有）。 */
export interface AmmoView {
  mag: number;
  reserve: number;
  pending: number;
  rejected: number;
  overridden: boolean;
}

export interface AmmoLedger {
  /** 本地开火成功后调用（与 `predictCommand.seq` 同源）。 */
  noteLocalShot(seq: number, slot: number): void;
  /** 每次快照应用后调用；只在 seq 严格变大时推进水位。 */
  noteServerAck(seq: number): void;
  /** 用权威值 + 未 ack 开火数算出建议显示值。 */
  reconcile(serverMag: number, serverReserve: number, slot: number): AmmoView;
  readonly rejectedTotal: number;
  /** O07（加性）：换图/重连时清账，与 `reconciler.reset()` 同一时机。 */
  reset(): void;
}

/** 武器槽位数（0 主武器 / 1 / 2）。 */
export const AMMO_SLOT_COUNT = 3;

/**
 * 按命令序号记账的弹药账本：显示值 = 权威值 − 尚未被 ack 的本地开火数。
 * 结算规则见 docs/优化/O07 §5（冻结）：`actualDrop < expectedDrop` ⇒ 差额计入 rejected 且本帧钳到权威值。
 */
export function createAmmoLedger(capacity: number = COMMAND_BUFFER_CAPACITY): AmmoLedger {
  const size = Math.max(1, Math.floor(capacity));
  const seqs = new Int32Array(size);
  const slots = new Uint8Array(size);
  const ackedBySlot = new Int32Array(AMMO_SLOT_COUNT);
  const view: AmmoView = { mag: 0, reserve: 0, pending: 0, rejected: 0, overridden: false };
  let head = 0;
  let count = 0;
  let ackedSeq = 0;
  let lastServerMag = -1;
  let overflowed = 0;
  let rejectedTotal = 0;

  function pendingInSlot(slot: number): number {
    let total = 0;
    for (let i = 0; i < count; i += 1) {
      if ((slots[(head + i) % size] ?? 0) === slot) total += 1;
    }
    return total;
  }

  /** 把某个槽位的待确认开火从账本里摘掉（被服务器拒绝、或账本失真钳位时）。 */
  function dropSlot(slot: number): void {
    let kept = 0;
    for (let i = 0; i < count; i += 1) {
      const index = (head + i) % size;
      if ((slots[index] ?? 0) === slot) continue;
      const target = (head + kept) % size;
      seqs[target] = seqs[index] ?? 0;
      slots[target] = slots[index] ?? 0;
      kept += 1;
    }
    count = kept;
  }

  return {
    noteLocalShot(seq: number, slot: number): void {
      if (count === size) {
        head = (head + 1) % size;
        count -= 1;
        overflowed += 1;
      }
      const index = (head + count) % size;
      seqs[index] = seq;
      slots[index] = slot;
      count += 1;
    },
    noteServerAck(seq: number): void {
      if (seq <= ackedSeq) return;
      ackedSeq = seq;
      while (count > 0) {
        const index = head;
        if ((seqs[index] ?? 0) > seq) break;
        const slot = slots[index] ?? 0;
        if (slot < AMMO_SLOT_COUNT) ackedBySlot[slot] = (ackedBySlot[slot] ?? 0) + 1;
        head = (head + 1) % size;
        count -= 1;
      }
    },
    reconcile(serverMag: number, serverReserve: number, slot: number): AmmoView {
      const inRange = slot >= 0 && slot < AMMO_SLOT_COUNT;
      const expected = inRange ? (ackedBySlot[slot] ?? 0) : 0;
      if (inRange) ackedBySlot[slot] = 0;
      let rejected = 0;
      let overridden = false;
      if (overflowed > 0) {
        rejected += overflowed;
        rejectedTotal += overflowed;
        overflowed = 0;
        overridden = true;
        if (inRange) dropSlot(slot);
      }
      if (lastServerMag < 0) {
        lastServerMag = serverMag;
      } else if (serverMag > lastServerMag) {
        head = 0;
        count = 0;
        ackedBySlot.fill(0);
        lastServerMag = serverMag;
      } else {
        const actualDrop = lastServerMag - serverMag;
        if (actualDrop < expected) {
          const lost = expected - actualDrop;
          rejected += lost;
          rejectedTotal += lost;
          overridden = true;
          if (inRange) dropSlot(slot);
        }
        lastServerMag = serverMag;
      }
      const pending = inRange ? pendingInSlot(slot) : 0;
      view.mag = serverMag - pending;
      if (view.mag < 0) view.mag = 0;
      view.reserve = serverReserve;
      view.pending = pending;
      view.rejected = rejected;
      view.overridden = overridden;
      return view;
    },
    get rejectedTotal(): number {
      return rejectedTotal;
    },
    reset(): void {
      head = 0;
      count = 0;
      ackedSeq = 0;
      ackedBySlot.fill(0);
      lastServerMag = -1;
      overflowed = 0;
    },
  };
}
