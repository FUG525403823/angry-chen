import { PROTOCOL_VERSION, decodeError, decodeJoin, encodeError, encodeWelcome } from '@ac/shared';
import { afterEach, describe, expect, it, vi } from 'vitest';

import {
  LAST_ROOM_CODE_STORAGE_KEY,
  NICKNAME_STORAGE_KEY,
  clearStoredRoomCode,
  createGameConnection,
  normalizeRoomCode,
  readStoredCredentials,
  storeCredentials,
  validateChatText,
  type ClientCredentialStorage,
  type ConnectionStatus,
} from './connection.ts';
import { createSnapshotView } from './state.ts';

function createStorage(
  seed: ReadonlyArray<readonly [string, string]> = [],
): ClientCredentialStorage {
  const values = new Map<string, string>(seed);
  return {
    getItem(key: string): string | null {
      return values.get(key) ?? null;
    },
    setItem(key: string, value: string): void {
      values.set(key, value);
    },
  };
}

describe('连接层的本地凭据', () => {
  it('没有存储时使用回退昵称且没有房间码', () => {
    expect(readStoredCredentials(undefined, '陈sir')).toEqual({ nickname: '陈sir', roomCode: '' });
  });

  it('读取已存昵称与房间码，非法昵称回退', () => {
    const storage = createStorage([
      [NICKNAME_STORAGE_KEY, '陈sir'],
      [LAST_ROOM_CODE_STORAGE_KEY, 'ab2d'],
    ]);
    expect(readStoredCredentials(storage, 'fallback')).toEqual({
      nickname: '陈sir',
      roomCode: 'AB2D',
    });
    const dirty = createStorage([[NICKNAME_STORAGE_KEY, '<>']]);
    expect(readStoredCredentials(dirty, 'fallback').nickname).toBe('fallback');
  });

  it('创建房间码 0000 不作为回座房间码', () => {
    const storage = createStorage([[LAST_ROOM_CODE_STORAGE_KEY, '0000']]);
    expect(readStoredCredentials(storage, '陈sir').roomCode).toBe('');
  });

  it('写入后可以读回，清除房间码不影响昵称', () => {
    const storage = createStorage();
    storeCredentials(storage, { nickname: '阿陈', roomCode: 'AB2D' });
    expect(readStoredCredentials(storage, 'x')).toEqual({ nickname: '阿陈', roomCode: 'AB2D' });
    clearStoredRoomCode(storage);
    expect(readStoredCredentials(storage, 'x')).toEqual({ nickname: '阿陈', roomCode: '' });
  });

  it('房间码规范化会过滤易混淆字符', () => {
    expect(normalizeRoomCode(' ab2d ')).toBe('AB2D');
    expect(normalizeRoomCode('AB1D')).toBe('');
    expect(normalizeRoomCode('ILO0')).toBe('');
    expect(normalizeRoomCode('0000')).toBe('0000');
  });
});

describe('聊天文本校验', () => {
  it('去除首尾空白与控制字符', () => {
    expect(validateChatText('  你好  ')).toBe('你好');
    expect(validateChatText('a\u0000b')).toBe('ab');
  });

  it('空内容与超过 64 字节的消息被拒绝', () => {
    expect(validateChatText('')).toBeNull();
    expect(validateChatText('   ')).toBeNull();
    expect(validateChatText('a'.repeat(64))).toBe('a'.repeat(64));
    expect(validateChatText('a'.repeat(65))).toBeNull();
    expect(validateChatText('你好'.repeat(11))).toBeNull();
  });
});

interface FakeListener {
  (event: unknown): void;
}

class FakeWebSocket {
  static readonly CONNECTING = 0;
  static readonly OPEN = 1;
  static readonly CLOSING = 2;
  static readonly CLOSED = 3;
  static readonly sockets: FakeWebSocket[] = [];
  readonly url: string;
  readyState = FakeWebSocket.CONNECTING;
  binaryType = '';
  readonly sent: Uint8Array[] = [];
  private readonly listeners = new Map<string, FakeListener[]>();

  constructor(url: string) {
    this.url = url;
    FakeWebSocket.sockets.push(this);
  }

  addEventListener(type: string, fn: FakeListener): void {
    const list = this.listeners.get(type) ?? [];
    list.push(fn);
    this.listeners.set(type, list);
  }

  emit(type: string, event: unknown): void {
    for (const fn of this.listeners.get(type) ?? []) fn(event);
  }

  send(bytes: Uint8Array): void {
    this.sent.push(bytes.slice());
  }

  close(): void {
    this.readyState = FakeWebSocket.CLOSED;
    this.emit('close', {});
  }

  open(): void {
    this.readyState = FakeWebSocket.OPEN;
    this.emit('open', {});
  }

  deliver(frame: Uint8Array): void {
    const copy = frame.slice();
    this.emit('message', { data: copy.buffer });
  }
}

const frameBuffer = new Uint8Array(64);

function welcomeFrame(pid: number, roomCode: string): Uint8Array {
  const size = encodeWelcome(
    { pid, roomCode, protocolVersion: PROTOCOL_VERSION, tick: 0, serverTimeMs: 0 },
    frameBuffer,
  );
  return frameBuffer.slice(0, size);
}

function errorFrame(code: number, message: string): Uint8Array {
  const size = encodeError(code, message, frameBuffer);
  return frameBuffer.slice(0, size);
}

function lastJoinOf(socket: FakeWebSocket | undefined): { name: string; roomCode: string } | null {
  if (socket === undefined) return null;
  for (let i = socket.sent.length - 1; i >= 0; i -= 1) {
    const frame = socket.sent[i];
    if (frame === undefined) continue;
    const decoded = decodeJoin(frame);
    if (decoded.ok) return { name: decoded.value.name, roomCode: decoded.value.roomCode };
  }
  return null;
}

function firstErrorCodeOf(socket: FakeWebSocket | undefined): number | null {
  if (socket === undefined) return null;
  for (let i = 0; i < socket.sent.length; i += 1) {
    const frame = socket.sent[i];
    if (frame === undefined) continue;
    const decoded = decodeError(frame);
    if (decoded.ok) return decoded.value.code;
  }
  return null;
}

interface Harness {
  readonly connection: ReturnType<typeof createGameConnection>;
  readonly storage: ClientCredentialStorage;
  readonly returned: string[];
  readonly errors: number[];
  readonly statuses: ConnectionStatus[];
}

function startConnection(): Harness {
  Object.assign(globalThis, { WebSocket: FakeWebSocket });
  FakeWebSocket.sockets.length = 0;
  const storage = createStorage();
  const returned: string[] = [];
  const errors: number[] = [];
  const statuses: ConnectionStatus[] = [];
  const connection = createGameConnection({
    url: 'ws://test',
    view: createSnapshotView(),
    storage,
    onStatus: (status: ConnectionStatus) => statuses.push(status),
    onReturnToLobby: (message: string) => returned.push(message),
    onServerError: (code: number) => errors.push(code),
  });
  return { connection, storage, returned, errors, statuses };
}

describe('断线重连回座', () => {
  afterEach(() => {
    vi.useRealTimers();
  });

  it('重连后按存储的房间码与昵称重新 Join', async () => {
    vi.useFakeTimers();
    const harness = startConnection();
    const first = FakeWebSocket.sockets[0];
    expect(first).toBeDefined();
    if (first === undefined) return;
    expect(lastJoinOf(first)).toBeNull();
    expect(harness.connection.joinRoom('AB2D', '陈sir')).toBe(true);
    first.open();
    expect(lastJoinOf(first)).toEqual({ name: '陈sir', roomCode: 'AB2D' });
    first.deliver(welcomeFrame(1, 'AB2D'));
    expect(harness.storage.getItem(LAST_ROOM_CODE_STORAGE_KEY)).toBe('AB2D');
    harness.storage.setItem(LAST_ROOM_CODE_STORAGE_KEY, 'WX23');
    first.close();
    expect(harness.connection.status).toBe('closed');
    await vi.advanceTimersByTimeAsync(2100);
    const second = FakeWebSocket.sockets[1];
    expect(second).toBeDefined();
    if (second === undefined) return;
    second.open();
    expect(lastJoinOf(second)).toEqual({ name: '陈sir', roomCode: 'WX23' });
    harness.connection.dispose();
  });

  it('错误码 7 退回大厅且不再自动回座', async () => {
    vi.useFakeTimers();
    const harness = startConnection();
    const first = FakeWebSocket.sockets[0];
    expect(first).toBeDefined();
    if (first === undefined) return;
    harness.connection.joinRoom('AB2D', '陈sir');
    first.open();
    first.deliver(errorFrame(7, 'match in progress'));
    expect(harness.errors).toEqual([7]);
    expect(harness.returned).toEqual(['对局已开始，无法加入']);
    expect(harness.storage.getItem(LAST_ROOM_CODE_STORAGE_KEY)).toBe('');
    expect(harness.connection.roomCode).toBe('');
    first.close();
    await vi.advanceTimersByTimeAsync(3000);
    expect(FakeWebSocket.sockets).toHaveLength(1);
    harness.connection.dispose();
  });

  it('错误码 2 清除房间码，聊天长度校验后发送', () => {
    const harness = startConnection();
    const first = FakeWebSocket.sockets[0];
    expect(first).toBeDefined();
    if (first === undefined) return;
    harness.connection.joinRoom('AB2D', '陈sir');
    first.open();
    expect(harness.connection.sendChat('你好')).toBe(true);
    expect(firstErrorCodeOf(first)).toBeNull();
    first.deliver(errorFrame(2, 'room not found'));
    expect(harness.storage.getItem(LAST_ROOM_CODE_STORAGE_KEY)).toBe('');
    expect(harness.connection.sendChat('a'.repeat(70))).toBe(false);
    harness.connection.leaveRoom();
    expect(harness.connection.status).toBe('idle');
    harness.connection.dispose();
  });

  it('非法昵称或房间码不会发出 Join', () => {
    const harness = startConnection();
    const first = FakeWebSocket.sockets[0];
    expect(first).toBeDefined();
    if (first === undefined) return;
    first.open();
    expect(harness.connection.joinRoom('AB1D', '陈sir')).toBe(false);
    expect(harness.connection.joinRoom('AB2D', 'a b')).toBe(false);
    expect(lastJoinOf(first)).toBeNull();
    harness.connection.dispose();
  });
});
