import {
  LIMITS,
  SERVER_TICK_MS,
  createSnapshotMirror,
  decodeError,
  decodeMatchState,
  decodePong,
  decodeSnapshot,
  decodeWelcome,
  encodeJoin,
  encodePing,
  encodeSimpleFrame,
} from '@ac/shared';
import type { MatchState, Pong, SnapshotMirror, Welcome } from '@ac/shared';

import { createGameServer, type GameServer } from '../server.ts';
import {
  createMemoryTransport,
  type MemoryClient,
  type MemoryTransport,
} from '../transport/memory-adapter.ts';

export interface TestClient {
  readonly id: number;
  readonly client: MemoryClient;
  readonly frames: Uint8Array[];
  readonly mirror: SnapshotMirror;
  send(frame: Uint8Array): void;
  join(name: string, roomCode: string, protocolVersion?: number): void;
  framesOf(opcode: number): Uint8Array[];
  errorCodes(): number[];
  welcome(): Welcome | undefined;
  matchState(): MatchState | undefined;
  pong(): Pong | undefined;
  applySnapshots(): number;
  snapshotTicks(): number[];
}

export interface Harness {
  readonly game: GameServer;
  readonly transport: MemoryTransport;
  readonly clock: { value: number };
  advance(ms: number): void;
  tick(count: number): void;
  connect(): TestClient;
  close(): Promise<void>;
}

export function createHarness(options?: {
  maxRooms?: number;
  maxPlayersPerRoom?: number;
  seed?: number;
  latencyMs?: number;
  dropRate?: number;
}): Harness {
  const clock = { value: 1_000_000 };
  const transport = createMemoryTransport({
    maxFrameBytes: LIMITS.maxFrameBytes,
    latencyMs: options?.latencyMs ?? 0,
    dropRate: options?.dropRate ?? 0,
    seed: 7,
  });
  const game = createGameServer(transport, {
    maxRooms: options?.maxRooms ?? 64,
    ...(options?.maxPlayersPerRoom === undefined
      ? {}
      : { maxPlayersPerRoom: options.maxPlayersPerRoom }),
    now: (): number => clock.value,
    monotonicNow: (): number => clock.value,
    ...(options?.seed === undefined ? {} : { seed: options.seed }),
  });
  void game.listen();
  const outbound = new Uint8Array(LIMITS.maxFrameBytes);

  function connect(): TestClient {
    const client = transport.connect();
    const frames: Uint8Array[] = [];
    const mirror = createSnapshotMirror();
    let appliedSnapshots = 0;
    client.onMessage((frame: Uint8Array): void => {
      frames.push(frame);
    });
    const api: TestClient = {
      id: client.id,
      client,
      frames,
      mirror,
      send(frame: Uint8Array): void {
        client.send(frame);
      },
      join(name: string, roomCode: string, protocolVersion = 1): void {
        const size = encodeJoin({ protocolVersion, name, roomCode }, outbound);
        client.send(outbound.subarray(0, size));
      },
      framesOf(opcode: number): Uint8Array[] {
        return frames.filter((frame) => frame[0] === opcode);
      },
      errorCodes(): number[] {
        const codes: number[] = [];
        for (const frame of frames) {
          if (frame[0] !== 0x86) continue;
          const decoded = decodeError(frame);
          if (decoded.ok) codes.push(decoded.value.code);
        }
        return codes;
      },
      welcome(): Welcome | undefined {
        for (const frame of frames) {
          if (frame[0] !== 0x81) continue;
          const decoded = decodeWelcome(frame);
          if (decoded.ok) return decoded.value;
        }
        return undefined;
      },
      matchState(): MatchState | undefined {
        let state: MatchState | undefined;
        for (const frame of frames) {
          if (frame[0] !== 0x84) continue;
          const decoded = decodeMatchState(frame);
          if (decoded.ok) state = decoded.value;
        }
        return state;
      },
      pong(): Pong | undefined {
        let pong: Pong | undefined;
        for (const frame of frames) {
          if (frame[0] !== 0x85) continue;
          const decoded = decodePong(frame);
          if (decoded.ok) pong = decoded.value;
        }
        return pong;
      },
      applySnapshots(): number {
        let applied = 0;
        for (let i = appliedSnapshots; i < frames.length; i += 1) {
          const frame = frames[i];
          if (frame === undefined || frame[0] !== 0x82) continue;
          const decoded = decodeSnapshot(frame, mirror);
          if (!decoded.ok) throw new Error('snapshot decode failed: ' + decoded.reason);
          applied += 1;
        }
        appliedSnapshots = frames.length;
        return applied;
      },
      snapshotTicks(): number[] {
        const ticks: number[] = [];
        for (const frame of frames) {
          if (frame[0] !== 0x82) continue;
          const view = new DataView(frame.buffer, frame.byteOffset, frame.byteLength);
          ticks.push(view.getUint32(1, true));
        }
        return ticks;
      },
    };
    return api;
  }

  return {
    game,
    transport,
    clock,
    advance(ms: number): void {
      clock.value += ms;
      game.step(clock.value);
    },
    tick(count: number): void {
      for (let i = 0; i < count; i += 1) {
        clock.value += SERVER_TICK_MS;
        game.step(clock.value);
      }
    },
    connect,
    close(): Promise<void> {
      return game.close();
    },
  };
}

export function sendPing(client: TestClient, clientTimeMs: number, lastRecvTick: number): void {
  const buffer = new Uint8Array(9);
  const size = encodePing({ clientTimeMs, lastRecvTick }, buffer);
  client.send(buffer.subarray(0, size));
}

export function sendSimple(client: TestClient, opcode: number): void {
  const buffer = new Uint8Array(1);
  const size = encodeSimpleFrame(opcode, buffer);
  client.send(buffer.subarray(0, size));
}
