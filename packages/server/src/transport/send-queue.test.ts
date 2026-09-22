import { describe, expect, it } from 'vitest';

import { createSendQueue } from './send-queue.ts';

interface Recorder {
  readonly chunks: Uint8Array[];
  readonly dones: (() => void)[];
  readonly write: (chunk: Uint8Array, done: () => void) => void;
}

function recorder(): Recorder {
  const chunks: Uint8Array[] = [];
  const dones: (() => void)[] = [];
  return {
    chunks,
    dones,
    write: (chunk: Uint8Array, done: () => void): void => {
      chunks.push(chunk);
      dones.push(done);
    },
  };
}

describe('出站发送队列（O03 所有权语义）', () => {
  it('send 返回后立即改写源数组，写回调拿到的字节与发送时一致', () => {
    const queue = createSendQueue(1024, 64);
    const sink = recorder();
    const source = new Uint8Array([1, 2, 3, 4]);

    queue.send(source, sink.write);
    source.fill(0xff);
    source[0] = 9;

    expect(sink.chunks.length).toBe(1);
    expect(Array.from(sink.chunks[0] ?? [])).toEqual([1, 2, 3, 4]);
    expect(queue.queuedBytes).toBe(4);
    sink.dones[0]?.();
    expect(queue.queuedBytes).toBe(0);
  });

  it('复用的是同一块池缓冲：归还后再发送落到同一个 ArrayBuffer 上', () => {
    const queue = createSendQueue(4096, 64);
    const sink = recorder();
    const push = (): void => {
      queue.send(new Uint8Array([7]), sink.write);
    };

    push();
    push();
    push();
    expect(sink.chunks.length).toBe(3);
    expect(queue.unpooledFrames).toBe(1);
    // 池槽是同一个 `Uint8Array`，交出去的是它的 subarray 视图，故比较底层 ArrayBuffer。
    const first = sink.chunks[0];
    expect(sink.chunks[1]?.buffer).not.toBe(first?.buffer);
    expect(sink.chunks[2]?.buffer).not.toBe(first?.buffer);

    sink.dones[0]?.();
    push();
    expect(sink.chunks[3]?.buffer).toBe(first?.buffer);
    expect(queue.unpooledFrames).toBe(1);
  });

  it('超过队列预算或单帧大于池槽时不入队，只计 drops', () => {
    const queue = createSendQueue(10, 8);
    const sink = recorder();

    queue.send(new Uint8Array(8), sink.write);
    queue.send(new Uint8Array(4), sink.write);
    expect(queue.drops).toBe(1);
    expect(sink.chunks.length).toBe(1);
    expect(queue.queuedBytes).toBe(8);

    queue.send(new Uint8Array(9), sink.write);
    expect(queue.drops).toBe(2);
    expect(sink.chunks.length).toBe(1);
  });

  it('clear() 归零账面、作废在途回调，之后仍可继续使用', () => {
    const queue = createSendQueue(1024, 64);
    const sink = recorder();

    queue.send(new Uint8Array(4), sink.write);
    expect(queue.queuedBytes).toBe(4);
    queue.clear();
    expect(queue.queuedBytes).toBe(0);

    sink.dones[0]?.();
    expect(queue.queuedBytes).toBe(0);

    queue.send(new Uint8Array(2), sink.write);
    expect(queue.queuedBytes).toBe(2);
    expect(queue.drops).toBe(0);
  });
});
