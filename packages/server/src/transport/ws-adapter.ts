import type { Server as HttpServer } from 'node:http';

import { LIMITS } from '@ac/shared';
import { WebSocket, WebSocketServer } from 'ws';

import { createSendQueue, type SendQueue } from './send-queue.ts';

import type {
  CloseHandler,
  Connection,
  FrameHandler,
  Transport,
  TransportOptions,
} from './types.ts';

export interface WsTransportOptions extends TransportOptions {
  readonly port: number;
  readonly host: string;
  readonly server?: HttpServer;
}

export interface WsTransport extends Transport {
  readonly port: number;
  readonly connectionCount: number;
}

export function createWsTransport(options: WsTransportOptions): WsTransport {
  const wss = new WebSocketServer({
    port: options.server === undefined ? options.port : undefined,
    host: options.host,
    server: options.server,
    maxPayload: options.maxFrameBytes,
    perMessageDeflate: false,
  });
  let nextId = 1;
  let oversizedFrames = 0;
  let slowClientDrops = 0;
  let listener: ((connection: Connection) => void) | undefined;
  const connections = new Set<Connection>();
  const queues = new Map<Connection, SendQueue>();

  wss.on('connection', (socket: WebSocket, request) => {
    const id = nextId;
    nextId += 1;
    let messageHandler: FrameHandler | undefined;
    let closeHandler: CloseHandler | undefined;
    let closed = false;
    // O03：拷贝语义的唯一实现处。帧在 send 返回前进队列的自有缓冲，写完成后归还。
    const queue = createSendQueue(LIMITS.maxBufferedBytes, options.maxFrameBytes);
    let reportedDrops = 0;

    const connection: Connection = {
      id,
      remote: request.socket.remoteAddress ?? 'unknown',
      get closed(): boolean {
        return closed || socket.readyState === WebSocket.CLOSED;
      },
      get bufferedAmount(): number {
        return queue.queuedBytes + socket.bufferedAmount;
      },
      send(frame: Uint8Array): void {
        if (socket.readyState !== WebSocket.OPEN) return;
        queue.send(frame, (chunk, done) => {
          socket.send(chunk, { binary: true }, () => done());
        });
        if (queue.drops > reportedDrops) {
          slowClientDrops += queue.drops - reportedDrops;
          reportedDrops = queue.drops;
        }
        // 慢客户端高水位（O03 §4 任务 3；判据调整见 §5 与验收报告）：
        // `bufferedAmount = 队列 Q + node 写缓冲`，而写缓冲装的就是同一批未 flush 的字节，故 `bufferedAmount ≈ 2Q`、
        // 上界恰好是 `2 × maxBufferedBytes`（`Q ≤ maxBufferedBytes`）——§5 原文的「`> 2 ×`」**取不到**。
        // 可达且语义等价的做法：已经开始丢帧（说明队列顶到预算）且积压仍在预算上限之上 ⇒ 判定为慢客户端。
        if (reportedDrops > 0 && connection.bufferedAmount >= LIMITS.maxBufferedBytes) {
          connection.close(1013, 'slow client');
        }
      },
      close(code?: number, reason?: string): void {
        if (closed) return;
        socket.close(code, reason);
      },
      onMessage(handler: FrameHandler): void {
        messageHandler = handler;
      },
      onClose(handler: CloseHandler): void {
        closeHandler = handler;
      },
    };

    connections.add(connection);
    queues.set(connection, queue);
    socket.binaryType = 'nodebuffer';
    socket.on('message', (data: Buffer | ArrayBuffer | Buffer[], isBinary: boolean) => {
      if (!isBinary) {
        socket.close(1003, 'binary only');
        return;
      }
      const buffer = Buffer.isBuffer(data) ? data : Buffer.from(data as ArrayBuffer);
      if (buffer.byteLength > options.maxFrameBytes) {
        oversizedFrames += 1;
        socket.close(1009, 'frame too large');
        return;
      }
      const handler = messageHandler;
      if (handler !== undefined)
        handler(new Uint8Array(buffer.buffer, buffer.byteOffset, buffer.byteLength));
    });
    socket.on('close', (code: number) => {
      if (code === 1009) oversizedFrames += 1;
      // 在途回调可能在关闭后才触发，clear() 让它们变成空操作并把池还空（防池泄漏）。
      queue.clear();
      queues.delete(connection);
      if (closed) return;
      closed = true;
      connections.delete(connection);
      const handler = closeHandler;
      if (handler !== undefined) handler();
    });
    socket.on('error', (error: Error) => {
      if (error instanceof RangeError) oversizedFrames += 1;
      socket.close();
    });
    if (listener !== undefined) listener(connection);
  });

  return {
    listen(handler: (connection: Connection) => void): Promise<void> {
      listener = handler;
      return new Promise((resolve, reject) => {
        if (wss.address() !== null) {
          resolve();
          return;
        }
        wss.once('listening', () => resolve());
        wss.once('error', (error) => reject(error));
      });
    },
    broadcast(frame: Uint8Array): void {
      for (const connection of connections) connection.send(frame);
    },
    close(): Promise<void> {
      return new Promise((resolve) => {
        for (const connection of connections) connection.close(1001, 'server shutdown');
        wss.close(() => resolve());
      });
    },
    get port(): number {
      const address = wss.address();
      if (address === null || typeof address === 'string') return options.port;
      return address.port;
    },
    get connectionCount(): number {
      return connections.size;
    },
    get oversizedFrames(): number {
      return oversizedFrames;
    },
    get slowClientDrops(): number {
      return slowClientDrops;
    },
    get sendQueueBytes(): number {
      let total = 0;
      for (const queue of queues.values()) total += queue.queuedBytes;
      return total;
    },
  };
}
