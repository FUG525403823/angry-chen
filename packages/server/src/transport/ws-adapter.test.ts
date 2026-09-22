import { randomBytes } from 'node:crypto';
import { connect, type Socket } from 'node:net';

import { describe, expect, it } from 'vitest';

import type { Connection } from './types.ts';
import { createWsTransport } from './ws-adapter.ts';

/** 裸 TCP 客户端：完成 WS 握手后不再读 socket（Node 的全局 WebSocket 没有 pause）。 */
function handshake(port: number): Promise<Socket> {
  return new Promise<Socket>((resolve, reject) => {
    const socket = connect(port, '127.0.0.1', () => {
      socket.write(
        'GET / HTTP/1.1\r\n' +
          'Host: 127.0.0.1:' +
          String(port) +
          '\r\n' +
          'Upgrade: websocket\r\n' +
          'Connection: Upgrade\r\n' +
          'Sec-WebSocket-Key: ' +
          randomBytes(16).toString('base64') +
          '\r\n' +
          'Sec-WebSocket-Version: 13\r\n\r\n',
      );
      resolve(socket);
    });
    socket.on('error', reject);
  });
}

interface WsFrame {
  readonly opcode: number;
  readonly payload: Uint8Array;
}

/** 服务端→客户端的帧不掩码，按 opcode/length 顺序走一遍即可还原帧流。 */
function parseFrames(bytes: Uint8Array): WsFrame[] {
  const frames: WsFrame[] = [];
  let index = 0;
  while (index + 2 <= bytes.length) {
    const opcode = (bytes[index] ?? 0) & 0x0f;
    let length = (bytes[index + 1] ?? 0) & 0x7f;
    let headerEnd = index + 2;
    if (length === 126) {
      length = ((bytes[index + 2] ?? 0) << 8) | (bytes[index + 3] ?? 0);
      headerEnd = index + 4;
    } else if (length === 127) {
      length = 0;
      headerEnd = index + 10;
    }
    if (headerEnd + length > bytes.length) break;
    frames.push({ opcode, payload: bytes.subarray(headerEnd, headerEnd + length) });
    index = headerEnd + length;
  }
  return frames;
}

function waitForOpen(socket: WebSocket): Promise<void> {
  return new Promise<void>((resolve) => socket.addEventListener('open', () => resolve()));
}

function waitForMessage(socket: WebSocket): Promise<Uint8Array> {
  return new Promise<Uint8Array>((resolve) => {
    socket.addEventListener('message', (event) => {
      resolve(new Uint8Array(event.data as ArrayBuffer));
    });
  });
}

function waitForClose(socket: WebSocket): Promise<number> {
  return new Promise<number>((resolve) => {
    socket.addEventListener('close', (event) => resolve(event.code));
  });
}

describe('ws 传输适配器', () => {
  it('二进制帧双向透传，非二进制帧被拒绝', async () => {
    const transport = createWsTransport({ port: 0, host: '127.0.0.1', maxFrameBytes: 64 });
    const connections: Connection[] = [];
    const received: Uint8Array[] = [];
    await transport.listen((connection) => {
      connections.push(connection);
      connection.onMessage((frame) => received.push(frame));
    });
    expect(transport.port).toBeGreaterThan(0);

    const socket = new WebSocket('ws://127.0.0.1:' + String(transport.port));
    socket.binaryType = 'arraybuffer';
    await waitForOpen(socket);
    await new Promise((resolve) => setTimeout(resolve, 20));
    expect(transport.connectionCount).toBe(1);
    expect(connections.length).toBe(1);

    const inbound = new Uint8Array([0x06, 1, 0, 0, 0, 2, 0, 0, 0]);
    socket.send(inbound);
    await new Promise((resolve) => setTimeout(resolve, 20));
    expect(received.length).toBe(1);
    expect(Array.from(received[0] ?? [])).toEqual(Array.from(inbound));

    const outbound = new Uint8Array([0x85, 7, 0, 0, 0, 9, 0, 0, 0, 200]);
    connections[0]?.send(outbound);
    const echo = await waitForMessage(socket);
    expect(Array.from(echo)).toEqual(Array.from(outbound));

    const textSocket = new WebSocket('ws://127.0.0.1:' + String(transport.port));
    await waitForOpen(textSocket);
    const closed = waitForClose(textSocket);
    textSocket.send('not-binary');
    expect(await closed).toBe(1003);

    socket.close();
    await transport.close();
  });

  it('超过 maxPayload 的帧导致断开', async () => {
    const transport = createWsTransport({ port: 0, host: '127.0.0.1', maxFrameBytes: 32 });
    await transport.listen(() => undefined);
    const socket = new WebSocket('ws://127.0.0.1:' + String(transport.port));
    socket.binaryType = 'arraybuffer';
    await waitForOpen(socket);
    const closed = waitForClose(socket);
    socket.send(new Uint8Array(64));
    const code = await closed;
    expect([1006, 1009]).toContain(code);
    await transport.close();
  });

  it('服务端复用一块缓冲连发 20 帧，客户端收到的字节逐帧一致（拷贝语义）', async () => {
    const transport = createWsTransport({ port: 0, host: '127.0.0.1', maxFrameBytes: 256 });
    const connections: Connection[] = [];
    await transport.listen((connection) => connections.push(connection));
    const socket = new WebSocket('ws://127.0.0.1:' + String(transport.port));
    socket.binaryType = 'arraybuffer';
    await waitForOpen(socket);
    await new Promise((resolve) => setTimeout(resolve, 20));
    const received: Uint8Array[] = [];
    socket.addEventListener('message', (event) =>
      received.push(new Uint8Array(event.data as ArrayBuffer)),
    );

    const expected: number[][] = [];
    const scratch = new Uint8Array(256);
    const connection = connections[0];
    for (let i = 0; i < 20; i += 1) {
      const length = 8 + i;
      for (let k = 0; k < length; k += 1) scratch[k] = (i * 31 + k) & 0xff;
      expected.push(Array.from(scratch.subarray(0, length)));
      connection?.send(scratch.subarray(0, length));
      scratch.fill(0x5a);
    }

    await new Promise((resolve) => setTimeout(resolve, 300));
    expect(received.length).toBe(20);
    for (let i = 0; i < 20; i += 1) {
      expect(Array.from(received[i] ?? [])).toEqual(expected[i]);
    }
    // 写完成的帧必须归还，健康连接的积压回到 0（防“每帧新分配 + 不回收”）。
    expect(transport.sendQueueBytes).toBe(0);
    expect(transport.slowClientDrops).toBe(0);
    socket.close();
    await transport.close();
  });

  it('慢客户端积压超水位：计数增长并按 1013 断开', async () => {
    const transport = createWsTransport({ port: 0, host: '127.0.0.1', maxFrameBytes: 8192 });
    const connections: Connection[] = [];
    await transport.listen((connection) => connections.push(connection));
    const socket = await handshake(transport.port); // 握手后不再读 socket
    await new Promise((resolve) => setTimeout(resolve, 50));
    expect(connections.length).toBe(1);

    // 每帧内容不同，但都写在同一块 scratch 里（正是 room.ts/session.ts 的复用写法）：
    // 若 send 没有拷贝，node 写缓冲里未 flush 的帧会在下一轮被复写 ⇒ 排空后字节对不上。
    const scratch = new Uint8Array(8192);
    const connection = connections[0];
    for (let i = 0; i < 128; i += 1) {
      scratch.fill((i * 7) & 0xff);
      scratch[0] = i;
      connection?.send(scratch);
    }

    expect(transport.slowClientDrops).toBeGreaterThan(0);
    expect(transport.sendQueueBytes).toBeLessThanOrEqual(262144);

    // 恢复读取：服务端排队（且已成拷贝）的帧与 1013 关闭帧必须完整可见。
    const received: Buffer[] = [];
    socket.on('data', (chunk: Buffer) => received.push(chunk));
    socket.resume();
    await new Promise((resolve) => setTimeout(resolve, 400));
    const all = Buffer.concat(received);
    const handshakeEnd = all.indexOf('\r\n\r\n'); // 101 响应结束处才是帧流起点
    expect(handshakeEnd).toBeGreaterThan(0);
    const frames = parseFrames(all.subarray(handshakeEnd + 4));

    const closeFrame = frames.find((frame) => frame.opcode === 0x8);
    expect(closeFrame).toBeDefined();
    expect(((closeFrame?.payload[0] ?? 0) << 8) | (closeFrame?.payload[1] ?? 0)).toBe(1013);

    const dataFrames = frames.filter((frame) => frame.opcode === 0x2);
    expect(dataFrames.length).toBe(32); // 队列预算 256KiB ÷ 单帧 8KiB，之后开始丢帧
    for (let i = 0; i < dataFrames.length; i += 1) {
      const payload = dataFrames[i]?.payload ?? new Uint8Array(0);
      expect(payload.length).toBe(8192);
      expect(payload[0]).toBe(i);
      expect(payload[payload.length - 1]).toBe((i * 7) & 0xff);
    }

    socket.destroy();
    await transport.close();
  });
});
