import type { Server as HttpServer } from 'node:http';

import { WebSocket, WebSocketServer } from 'ws';

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
  let listener: ((connection: Connection) => void) | undefined;
  const connections = new Set<Connection>();

  wss.on('connection', (socket: WebSocket, request) => {
    const id = nextId;
    nextId += 1;
    let messageHandler: FrameHandler | undefined;
    let closeHandler: CloseHandler | undefined;
    let closed = false;

    const connection: Connection = {
      id,
      remote: request.socket.remoteAddress ?? 'unknown',
      get closed(): boolean {
        return closed || socket.readyState === WebSocket.CLOSED;
      },
      send(frame: Uint8Array): void {
        if (socket.readyState !== WebSocket.OPEN) return;
        socket.send(frame, { binary: true });
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
  };
}
