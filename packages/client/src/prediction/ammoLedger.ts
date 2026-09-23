import { COMMAND_BUFFER_CAPACITY } from './commandBuffer.ts';

/** O07：账本对外的裁决结果（**同一对象被复用**，调用方不得跨帧持有）。 */
export interface AmmoView {
  /** HUD 显示值：**恒等于权威弹匣**（真人试玩：显示要跟实际弹药完全一致，不能提前减）。 */
  mag: number;
  /** 本地开火闸门值：权威值 − 未确认开火数（宁可少打一发，也不产生幽灵弹道）。 */
  gateMag: number;
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
  /** 用权威值 + 未确认开火数算出建议显示值。 */
  reconcile(serverMag: number, serverReserve: number, slot: number): AmmoView;
  readonly rejectedTotal: number;
  /** O07（加性）：换图/重连时清账，与 `reconciler.reset()` 同一时机。 */
  reset(): void;
}

/** 武器槽位数（0 主武器 / 1 / 2）。 */
export const AMMO_SLOT_COUNT = 3;

/**
 * ack 水位领先多少条命令后，才认定"服务器收下了这发却没扣弹"（= 本地幽灵开火）。
 * 20 Hz 快照 + 30 Hz 采样下约 0.7 s，足够覆盖"ack 与权威弹匣分属相邻两个包"的时序。
 */
export const AMMO_ACK_GRACE_SEQ = 20;

/**
 * 弹药账本：显示值 = 权威值（HUD）；开火闸门 = 权威值 − 尚未被权威下降消掉的本地开火数。
 *
 * 对账口径（二轮试玩"开枪时子弹数跳动"后修正，见 docs/evidence/playtest-fixes-round3.md §4）：
 * **只有权威弹匣真的下降才算服务器收下了这发**。旧实现把"ack 水位越过"直接当成"应消耗"，
 * 而 ack（随快照包）与权威弹匣（随比赛状态包）分属相邻两个包，于是每一发都会先被记成
 * "被拒"、显示值随即弹回权威值 —— 表现为数字上下跳。ack 水位现在只用于过期判定：
 * 越过 `AMMO_ACK_GRACE_SEQ` 仍没有对应下降的本地开火，才算被服务器丢弃（rejected）。
 */
export function createAmmoLedger(capacity: number = COMMAND_BUFFER_CAPACITY): AmmoLedger {
  const size = Math.max(1, Math.floor(capacity));
  const seqs = new Int32Array(size);
  const slots = new Uint8Array(size);
  const view: AmmoView = {
    mag: 0,
    gateMag: 0,
    reserve: 0,
    pending: 0,
    rejected: 0,
    overridden: false,
  };
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

  /**
   * 按顺序摘除满足 `keep` 的待确认开火，返回摘除条数与其中属于 `watchSlot` 的条数。
   * 摘除顺序 = 记账顺序（旧→新），保证"权威下降 N 发"消掉的是最早的 N 发。
   */
  function dropWhere(
    keep: (seq: number, slot: number) => boolean,
    watchSlot: number,
  ): { removed: number; removedInSlot: number } {
    let kept = 0;
    let removed = 0;
    let removedInSlot = 0;
    for (let i = 0; i < count; i += 1) {
      const index = (head + i) % size;
      const seq = seqs[index] ?? 0;
      const slot = slots[index] ?? 0;
      if (!keep(seq, slot)) {
        removed += 1;
        if (slot === watchSlot) removedInSlot += 1;
        continue;
      }
      const target = (head + kept) % size;
      seqs[target] = seq;
      slots[target] = slot;
      kept += 1;
    }
    count = kept;
    return { removed, removedInSlot };
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
      // 只推进水位：是否真的收下这发，以权威弹匣下降为准（见文件头注释）。
      if (seq <= ackedSeq) return;
      ackedSeq = seq;
    },
    reconcile(serverMag: number, serverReserve: number, slot: number): AmmoView {
      const inRange = slot >= 0 && slot < AMMO_SLOT_COUNT;
      let rejected = 0;
      let overridden = false;
      if (overflowed > 0) {
        rejected += overflowed;
        rejectedTotal += overflowed;
        overflowed = 0;
        overridden = true;
        if (inRange) dropWhere((_seq, entrySlot) => entrySlot !== slot, slot);
      }
      if (lastServerMag < 0) {
        lastServerMag = serverMag;
      } else if (serverMag > lastServerMag) {
        // 换弹 / 补弹：权威值上升，账本整体清零（本地未确认开火已被覆盖）。
        head = 0;
        count = 0;
        lastServerMag = serverMag;
      } else {
        const drop = lastServerMag - serverMag;
        lastServerMag = serverMag;
        if (drop > 0 && inRange) {
          let consumed = 0;
          dropWhere((_seq, entrySlot) => {
            if (entrySlot !== slot || consumed >= drop) return true;
            consumed += 1;
            return false;
          }, slot);
        }
      }
      const watermark = ackedSeq - AMMO_ACK_GRACE_SEQ;
      if (watermark > 0) {
        const gone = dropWhere((seq) => seq > watermark, slot);
        if (gone.removed > 0) {
          rejected += gone.removed;
          rejectedTotal += gone.removed;
          if (gone.removedInSlot > 0) overridden = true;
        }
      }
      const pending = inRange ? pendingInSlot(slot) : 0;
      // 显示值不加本地乐观扣减：未确认开火只收紧开火闸门，不动 HUD 数字，
      // 这样数字永远不会因为对账误差而回升（真人试玩的"开枪时子弹数跳动"）。
      view.mag = serverMag < 0 ? 0 : serverMag;
      view.gateMag = serverMag - pending;
      if (view.gateMag < 0) view.gateMag = 0;
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
      lastServerMag = -1;
      overflowed = 0;
    },
  };
}
