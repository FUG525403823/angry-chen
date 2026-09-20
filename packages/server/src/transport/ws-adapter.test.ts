import { describe, expect, it } from 'vitest';

import type { Connection } from './types.ts';
import { createWsTransport } from './ws-adapter.ts';

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
});
