export type FrameHandler = (frame: Uint8Array) => void;
export type CloseHandler = () => void;

export interface Connection {
  readonly id: number;
  readonly remote: string;
  readonly closed: boolean;
  send(frame: Uint8Array): void;
  close(code?: number, reason?: string): void;
  onMessage(handler: FrameHandler): void;
  onClose(handler: CloseHandler): void;
}

export interface Transport {
  listen(handler: (connection: Connection) => void): Promise<void>;
  broadcast(frame: Uint8Array): void;
  close(): Promise<void>;
  readonly oversizedFrames?: number;
}

export interface TransportOptions {
  readonly maxFrameBytes: number;
}
