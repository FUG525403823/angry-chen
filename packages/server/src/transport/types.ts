export type FrameHandler = (frame: Uint8Array) => void;
export type CloseHandler = () => void;

export interface Connection {
  readonly id: number;
  readonly remote: string;
  readonly closed: boolean;
  /** 当前未写完成的出站字节数（发送队列 + socket 缓冲）。 */
  readonly bufferedAmount: number;
  /**
   * 交出 `frame` 的所有权：实现必须在返回前把内容拷进自有缓冲或持有独立所有权，
   * 调用方在 `send` 返回后**可以立即改写** `frame`，线上字节不受影响（O03 §5 冻结契约）。
   */
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
  /** 因出站积压未入队而被丢弃的帧数（累计，见 `ac_slow_client_drops_total`）。 */
  readonly slowClientDrops?: number;
  /** 所有活跃连接当前持有的未写完成字节数之和（见 `ac_send_queue_bytes`）。 */
  readonly sendQueueBytes?: number;
}

export interface TransportOptions {
  readonly maxFrameBytes: number;
}
