/**
 * 出站发送队列：`send` 的语义是「交出帧的所有权」。
 *
 * 服务端方向的 `ws` 不做掩码，`Sender.frame()` 对不掩码的帧直接返回调用方的视图
 * （`node_modules/ws/lib/sender.js`：`if (!options.mask) return [target, data]`），
 * 而 `room.ts` / `session.ts` 每个 tick 都在复写同一块 `outbound` / `broadcastBuffer`。
 * 于是「写进 socket 之后又改写源数组」会直接损坏尚未 flush 的线上帧（TCP 背压时必然发生）。
 *
 * 本模块是**全仓库唯一的拷贝点**：帧在 `send` 返回前被拷进自有缓冲（池化复用，O(1) 归还），
 * 调用方随即可改写源数组；写完成后缓冲回到池里。
 *
 * 有界性：
 * - 队列字节数上限 `maxBytes`（`LIMITS.maxBufferedBytes`），超出即 `drops += 1` 且**不入队**；
 * - 池容量 = `2 × bufferBytes`（见 O03 §5「池上限」），池满时退化为一次性精确分配（`unpooledFrames` 计数），
 *   内存仍受 `maxBytes` 约束 —— 直接丢弃健康连接的第 3 帧会制造假丢帧，故只对「超过队列预算」计数为 `drops`。
 */
export interface SendQueue {
  /** 已拷贝但尚未写完成的字节数（超预算的帧不进这个数）。 */
  readonly queuedBytes: number;
  /** 因超过 `maxBytes`（或单帧大于池槽）而未入队的帧数，累计值。 */
  readonly drops: number;
  /** 池容量用尽、走一次性分配的次数（诊断用：健康连接应恒为 0）。 */
  readonly unpooledFrames: number;
  send(frame: Uint8Array, write: (chunk: Uint8Array, done: () => void) => void): void;
  /** 归还全部池缓冲并作废在途回调（连接关闭 / 出错时调用，防止池泄漏）。 */
  clear(): void;
}

/** O03 §5：每连接池容量 = 2 × 单帧上限。 */
const POOL_SLOTS = 2;

export function createSendQueue(maxBytes: number, bufferBytes: number): SendQueue {
  /** 已创建的池槽（≤ POOL_SLOTS）。 */
  let createdSlots = 0;
  /** 空闲池槽。 */
  const free: Uint8Array[] = [];
  let queuedBytes = 0;
  let drops = 0;
  let unpooledFrames = 0;
  /** `clear()` 会让此前发出的 `done` 回调变成空操作，避免重复归还与账面错乱。 */
  let epoch = 0;

  function acquire(): Uint8Array | null {
    const reused = free.pop();
    if (reused !== undefined) return reused;
    if (createdSlots < POOL_SLOTS) {
      createdSlots += 1;
      return new Uint8Array(bufferBytes);
    }
    return null;
  }

  return {
    get queuedBytes(): number {
      return queuedBytes;
    },
    get drops(): number {
      return drops;
    },
    get unpooledFrames(): number {
      return unpooledFrames;
    },
    send(frame: Uint8Array, write: (chunk: Uint8Array, done: () => void) => void): void {
      if (frame.length > bufferBytes || queuedBytes + frame.length > maxBytes) {
        drops += 1;
        return;
      }
      const slot = acquire();
      let chunk: Uint8Array;
      if (slot === null) {
        chunk = frame.slice();
        unpooledFrames += 1;
      } else {
        chunk = slot.subarray(0, frame.length);
        chunk.set(frame);
      }
      queuedBytes += frame.length;
      const myEpoch = epoch;
      let settled = false;
      write(chunk, () => {
        if (settled) return;
        settled = true;
        if (myEpoch !== epoch) return;
        queuedBytes -= frame.length;
        if (slot !== null) free.push(slot);
      });
    },
    clear(): void {
      epoch += 1;
      queuedBytes = 0;
      createdSlots = 0;
      free.length = 0;
    },
  };
}
