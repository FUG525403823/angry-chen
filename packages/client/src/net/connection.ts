import {
  LIMITS,
  OPCODE,
  PROTOCOL_VERSION,
  decodeError,
  decodeEventFrame,
  decodeMatchState,
  decodePong,
  decodeWelcome,
  encodeChat,
  encodeCommand,
  encodeInteract,
  encodeJoin,
  encodePing,
  encodeReady,
  type Command,
  type MatchState,
  type SimEvent,
  type Welcome,
} from '@ac/shared';

import type { SnapshotViewInternal } from './state.ts';

export const PING_INTERVAL_MS = 1000;
export const RECONNECT_BASE_MS = 2000;
export const RECONNECT_MAX_MS = 15000;
export const MAX_MATCH_STATE_PLAYERS = LIMITS.maxPlayersPerRoom;

export type ConnectionStatus = 'idle' | 'connecting' | 'online' | 'closed' | 'error';

export interface ConnectionOptions {
  readonly url: string;
  readonly view: SnapshotViewInternal;
  readonly name: string;
  readonly roomCode: string;
  readonly autoReconnect?: boolean;
  onStatus?(status: ConnectionStatus, detail: string): void;
  onWelcome?(welcome: Welcome): void;
  onMatchState?(state: MatchState): void;
  onEvents?(events: readonly SimEvent[]): void;
  onServerError?(code: number, message: string): void;
}

export interface GameConnection {
  readonly status: ConnectionStatus;
  readonly lastServerSnapshotRateX10: number;
  sendReady(ready: boolean): void;
  sendChat(text: string): void;
  sendInteract(): void;
  sendCommand(command: Command): void;
  dispose(): void;
}

export function clientClockMs(): number {
  return Date.now() % 4294967296;
}

export function elapsedMs(startMs: number, endMs: number): number {
  return ((endMs - startMs) >>> 0) as number;
}

export function createGameConnection(options: ConnectionOptions): GameConnection {
  const out = new Uint8Array(LIMITS.maxFrameBytes);
  const events: SimEvent[] = [];
  let socket: WebSocket | undefined;
  let status: ConnectionStatus = 'idle';
  let pingTimer: ReturnType<typeof setInterval> | undefined;
  let reconnectTimer: ReturnType<typeof setTimeout> | undefined;
  let disposed = false;
  let snapshotRateX10 = 0;
  let reconnectAttempts = 0;

  function setStatus(next: ConnectionStatus, detail = ''): void {
    status = next;
    options.onStatus?.(next, detail);
  }

  function send(bytes: Uint8Array, size: number): void {
    if (socket === undefined || socket.readyState !== WebSocket.OPEN) return;
    socket.send(bytes.subarray(0, size));
  }

  function handleWelcome(frame: Uint8Array): void {
    const decoded = decodeWelcome(frame);
    if (!decoded.ok) return;
    options.view.setLocalPlayerId(decoded.value.pid);
    options.onWelcome?.(decoded.value);
  }

  function handleMatchState(frame: Uint8Array): void {
    const decoded = decodeMatchState(frame);
    if (!decoded.ok) return;
    options.onMatchState?.(decoded.value);
  }

  function handleEvents(frame: Uint8Array): void {
    events.length = 0;
    const decoded = decodeEventFrame(frame, events);
    if (!decoded.ok) return;
    options.onEvents?.(decoded.value);
  }

  function handlePong(frame: Uint8Array): void {
    const decoded = decodePong(frame);
    if (!decoded.ok) return;
    snapshotRateX10 = decoded.value.snapshotRateX10;
    options.view.recordRtt(elapsedMs(decoded.value.clientTimeMs, clientClockMs()));
  }

  function handleFrame(data: ArrayBuffer): void {
    const frame = new Uint8Array(data);
    if (frame.length === 0) return;
    const opcode = frame[0];
    if (opcode === OPCODE.snapshot) {
      options.view.applyFrame(frame, frame.length);
      return;
    }
    if (opcode === OPCODE.welcome) {
      handleWelcome(frame);
      return;
    }
    if (opcode === OPCODE.matchState) {
      handleMatchState(frame);
      return;
    }
    if (opcode === OPCODE.event) {
      handleEvents(frame);
      return;
    }
    if (opcode === OPCODE.pong) {
      handlePong(frame);
      return;
    }
    if (opcode === OPCODE.error) {
      const decoded = decodeError(frame);
      if (decoded.ok) options.onServerError?.(decoded.value.code, decoded.value.message);
    }
  }

  function openSocket(): void {
    if (disposed) return;
    setStatus('connecting');
    const next = new WebSocket(options.url);
    next.binaryType = 'arraybuffer';
    socket = next;
    next.addEventListener('open', () => {
      setStatus('online');
      reconnectAttempts = 0;
      send(
        out,
        encodeJoin(
          { protocolVersion: PROTOCOL_VERSION, name: options.name, roomCode: options.roomCode },
          out,
        ),
      );
      if (pingTimer !== undefined) clearInterval(pingTimer);
      pingTimer = setInterval(() => {
        send(
          out,
          encodePing(
            { clientTimeMs: clientClockMs(), lastRecvTick: options.view.getAppliedTick() },
            out,
          ),
        );
      }, PING_INTERVAL_MS);
    });
    next.addEventListener('message', (event) => {
      const data = event.data;
      if (data instanceof ArrayBuffer) handleFrame(data);
    });
    next.addEventListener('error', () => setStatus('error'));
    next.addEventListener('close', () => {
      if (pingTimer !== undefined) {
        clearInterval(pingTimer);
        pingTimer = undefined;
      }
      setStatus('closed');
      if (!disposed && options.autoReconnect !== false) {
        reconnectAttempts += 1;
        const delayMs = Math.min(
          RECONNECT_MAX_MS,
          RECONNECT_BASE_MS * 2 ** (reconnectAttempts - 1),
        );
        reconnectTimer = setTimeout(openSocket, delayMs);
      }
    });
  }

  openSocket();

  return {
    get status(): ConnectionStatus {
      return status;
    },
    get lastServerSnapshotRateX10(): number {
      return snapshotRateX10;
    },
    sendReady(ready: boolean): void {
      send(out, encodeReady({ ready, weapon: 0 }, out));
    },
    sendChat(text: string): void {
      send(out, encodeChat(text, out));
    },
    sendInteract(): void {
      send(out, encodeInteract(1, out));
    },
    sendCommand(command: Command): void {
      send(out, encodeCommand(command, out));
    },
    dispose(): void {
      disposed = true;
      if (pingTimer !== undefined) clearInterval(pingTimer);
      if (reconnectTimer !== undefined) clearTimeout(reconnectTimer);
      pingTimer = undefined;
      reconnectTimer = undefined;
      socket?.close();
      socket = undefined;
    },
  };
}
