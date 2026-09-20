import { createRng, type Rng } from '@ac/shared';

import type {
  CloseHandler,
  Connection,
  FrameHandler,
  Transport,
  TransportOptions,
} from './types.ts';

export interface MemoryClient {
  readonly id: number;
  readonly closed: boolean;
  send(frame: Uint8Array): void;
  onMessage(handler: FrameHandler): void;
  onClose(handler: CloseHandler): void;
  flush(): Promise<void>;
}

export interface MemoryTransportOptions extends TransportOptions {
  readonly latencyMs?: number;
  readonly dropRate?: number;
  readonly seed?: number;
}

export interface MemoryTransport extends Transport {
  connect(): MemoryClient;
  readonly connectionCount: number;
  readonly oversizedFrames: number;
}

interface Link {
  readonly id: number;
  connection: Connection;
  clientMessageHandler?: FrameHandler;
  clientCloseHandler?: CloseHandler;
  serverMessageHandler?: FrameHandler;
  serverCloseHandler?: CloseHandler;
  clientClosed: boolean;
  serverClosed: boolean;
}

export function createMemoryTransport(options: MemoryTransportOptions): MemoryTransport {
  const latencyMs = options.latencyMs ?? 0;
  const dropRate = options.dropRate ?? 0;
  const rng: Rng = createRng(options.seed ?? 1, 'fx');
  const links = new Map<number, Link>();
  let listener: ((connection: Connection) => void) | undefined;
  let oversizedFrames = 0;
  let nextId = 1;

  function defer(work: () => void): void {
    if (latencyMs <= 0) {
      work();
      return;
    }
    setTimeout(work, latencyMs);
  }

  function toServer(link: Link, frame: Uint8Array): void {
    if (link.serverClosed || link.clientClosed) return;
    if (frame.length > options.maxFrameBytes) {
      oversizedFrames += 1;
      link.connection.close(1009, 'frame too large');
      return;
    }
    if (dropRate > 0 && rng() < dropRate) return;
    const copy = frame.slice();
    defer(() => {
      const handler = link.serverMessageHandler;
      if (link.serverClosed || handler === undefined) return;
      handler(copy);
    });
  }

  function toClient(link: Link, frame: Uint8Array): void {
    if (link.serverClosed || link.clientClosed) return;
    const copy = frame.slice();
    defer(() => {
      const handler = link.clientMessageHandler;
      if (link.clientClosed || handler === undefined) return;
      handler(copy);
    });
  }

  function closeFromServer(link: Link): void {
    if (link.serverClosed) return;
    link.serverClosed = true;
    defer(() => {
      const handler = link.serverCloseHandler;
      if (handler !== undefined) handler();
      if (!link.clientClosed) {
        link.clientClosed = true;
        const clientHandler = link.clientCloseHandler;
        if (clientHandler !== undefined) clientHandler();
      }
    });
  }

  function createLink(): Link {
    const id = nextId;
    nextId += 1;
    const link: Link = {
      id,
      connection: undefined as unknown as Connection,
      clientClosed: false,
      serverClosed: false,
    };
    const connection: Connection = {
      id,
      remote: 'memory:' + String(id),
      get closed(): boolean {
        return link.serverClosed;
      },
      send(frame: Uint8Array): void {
        toClient(link, frame);
      },
      close(): void {
        closeFromServer(link);
      },
      onMessage(handler: FrameHandler): void {
        link.serverMessageHandler = handler;
      },
      onClose(handler: CloseHandler): void {
        link.serverCloseHandler = handler;
      },
    };
    link.connection = connection;
    links.set(id, link);
    if (listener !== undefined) listener(connection);
    return link;
  }

  return {
    listen(handler: (connection: Connection) => void): Promise<void> {
      listener = handler;
      return Promise.resolve();
    },
    broadcast(frame: Uint8Array): void {
      for (const link of links.values()) toClient(link, frame);
    },
    close(): Promise<void> {
      for (const link of links.values()) {
        link.serverClosed = true;
        link.clientClosed = true;
      }
      return Promise.resolve();
    },
    connect(): MemoryClient {
      const link = createLink();
      const client: MemoryClient = {
        id: link.id,
        get closed(): boolean {
          return link.clientClosed;
        },
        send(frame: Uint8Array): void {
          toServer(link, frame);
        },
        onMessage(handler: FrameHandler): void {
          link.clientMessageHandler = handler;
        },
        onClose(handler: CloseHandler): void {
          link.clientCloseHandler = handler;
        },
        flush(): Promise<void> {
          return new Promise((resolve) => {
            setTimeout(resolve, latencyMs + 1);
          });
        },
      };
      return client;
    },
    get connectionCount(): number {
      return links.size;
    },
    get oversizedFrames(): number {
      return oversizedFrames;
    },
  };
}
