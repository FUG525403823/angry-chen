import { createCommand, type Command } from '@ac/shared';

export const COMMAND_BUFFER_CAPACITY = 128;
export const HALF_SEQ_RANGE = 32768;

/** seq 回绕安全的差值：> 0 表示 a 比 b 新。 */
export function seqDiff(a: number, b: number): number {
  const diff = (a - b) & 0xffff;
  return diff >= HALF_SEQ_RANGE ? diff - 65536 : diff;
}

export interface CommandBuffer {
  readonly capacity: number;
  readonly size: number;
  readonly overflowCount: number;
  push(command: Command): void;
  ackUpTo(seq: number): number;
  at(index: number): Command | undefined;
  clear(): void;
}

function copyInto(target: Command, source: Command): void {
  target.seq = source.seq;
  target.tick = source.tick;
  target.moveX = source.moveX;
  target.moveY = source.moveY;
  target.yaw = source.yaw;
  target.pitch = source.pitch;
  target.buttons = source.buttons;
  target.switchTo = source.switchTo;
}

export function createCommandBuffer(capacity: number = COMMAND_BUFFER_CAPACITY): CommandBuffer {
  const cap = Math.max(1, Math.floor(capacity));
  const slots: Command[] = [];
  for (let i = 0; i < cap; i += 1) slots.push(createCommand());
  let head = 0;
  let count = 0;
  let overflowCount = 0;

  return {
    capacity: cap,
    get size(): number {
      return count;
    },
    get overflowCount(): number {
      return overflowCount;
    },
    push(command: Command): void {
      if (count === cap) {
        head = (head + 1) % cap;
        count -= 1;
        overflowCount += 1;
      }
      const slot = slots[(head + count) % cap];
      if (slot === undefined) return;
      copyInto(slot, command);
      count += 1;
    },
    ackUpTo(seq: number): number {
      let dropped = 0;
      while (count > 0) {
        const slot = slots[head];
        if (slot === undefined || seqDiff(seq, slot.seq) < 0) break;
        head = (head + 1) % cap;
        count -= 1;
        dropped += 1;
      }
      return dropped;
    },
    at(index: number): Command | undefined {
      if (index < 0 || index >= count) return undefined;
      return slots[(head + index) % cap];
    },
    clear(): void {
      head = 0;
      count = 0;
    },
  };
}
