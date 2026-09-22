import {
  ERROR_CODE,
  LIMITS,
  NEW_ROOM_CODE,
  OPCODE,
  PROTOCOL_VERSION,
  decodeChatMessage,
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
  encodeSimpleFrame,
  isValidRoomCode,
  sanitizeChat,
  sanitizeName,
  utf8Length,
  type ChatMessage,
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
export const MAX_CHAT_BYTES = LIMITS.maxChatBytes;
export const NICKNAME_STORAGE_KEY = 'ac.nickname.v1';
export const LAST_ROOM_CODE_STORAGE_KEY = 'ac.lastRoomCode.v1';
/** O08：会话身份令牌（**每标签页独立**，存 `sessionStorage`；不可用时回退注入 storage）。 */
export const TOKEN_STORAGE_KEY = 'ac.sessionToken.v1';

export const ERROR_MESSAGES: Record<number, string> = Object.freeze({
  [ERROR_CODE.protocolMismatch]: '协议版本不一致，请刷新页面',
  [ERROR_CODE.roomNotFound]: '房间不存在或已结束',
  [ERROR_CODE.roomFull]: '房间已满（最多 4 人）',
  [ERROR_CODE.rateLimited]: '发送过于频繁，已被限流',
  [ERROR_CODE.malformedFrame]: '客户端帧格式错误',
  [ERROR_CODE.invalidName]: '昵称不合法（1–12 字节）',
  [ERROR_CODE.matchInProgress]: '对局已开始，无法加入',
  [ERROR_CODE.serverShutdown]: '服务器正在关闭',
  [ERROR_CODE.notHost]: '只有房主可以开始对局',
});

export type ConnectionStatus = 'idle' | 'connecting' | 'online' | 'closed' | 'error';

export interface ClientCredentialStorage {
  getItem(key: string): string | null;
  setItem(key: string, value: string): void;
}

export interface StoredCredentials {
  nickname: string;
  roomCode: string;
}

export function normalizeRoomCode(raw: string): string {
  const code = raw.trim().toUpperCase();
  return isValidRoomCode(code) ? code : '';
}

function readKey(storage: ClientCredentialStorage | undefined, key: string): string | null {
  if (storage === undefined) return null;
  try {
    return storage.getItem(key);
  } catch {
    return null;
  }
}

export function readStoredCredentials(
  storage: ClientCredentialStorage | undefined,
  fallbackNickname: string,
): StoredCredentials {
  const saved = sanitizeName(readKey(storage, NICKNAME_STORAGE_KEY) ?? '');
  const storedCode = normalizeRoomCode(readKey(storage, LAST_ROOM_CODE_STORAGE_KEY) ?? '');
  return {
    nickname: saved ?? fallbackNickname,
    roomCode: storedCode === NEW_ROOM_CODE ? '' : storedCode,
  };
}

export function storeCredentials(
  storage: ClientCredentialStorage | undefined,
  credentials: StoredCredentials,
): void {
  if (storage === undefined) return;
  try {
    storage.setItem(NICKNAME_STORAGE_KEY, credentials.nickname);
    storage.setItem(LAST_ROOM_CODE_STORAGE_KEY, credentials.roomCode);
  } catch {
    return;
  }
}

export function clearStoredRoomCode(storage: ClientCredentialStorage | undefined): void {
  if (storage === undefined) return;
  try {
    storage.setItem(LAST_ROOM_CODE_STORAGE_KEY, '');
  } catch {
    return;
  }
}

export function validateChatText(text: string): string | null {
  const trimmed = text.trim();
  if (trimmed.length === 0) return null;
  if (utf8Length(trimmed) > MAX_CHAT_BYTES) return null;
  const cleaned = sanitizeChat(trimmed).trim();
  return cleaned.length === 0 ? null : cleaned;
}

export interface ConnectionOptions {
  readonly url: string;
  readonly view: SnapshotViewInternal;
  readonly name?: string;
  readonly roomCode?: string;
  readonly storage?: ClientCredentialStorage | undefined;
  readonly autoReconnect?: boolean;
  onStatus?(status: ConnectionStatus, detail: string): void;
  onWelcome?(welcome: Welcome): void;
  /** 快照成功应用后回调：本地预测的和解时机。 */
  onSnapshotApplied?(bytesIn: number): void;
  onMatchState?(state: MatchState): void;
  onEvents?(events: readonly SimEvent[]): void;
  onChat?(message: ChatMessage): void;
  /** 错误码 7：房间已开局且宽限期不可用，客户端必须退回大厅。 */
  onReturnToLobby?(message: string): void;
  onServerError?(code: number, message: string): void;
}

export interface GameConnection {
  readonly status: ConnectionStatus;
  readonly lastServerSnapshotRateX10: number;
  readonly roomCode: string;
  joinRoom(roomCode: string, nickname: string): boolean;
  leaveRoom(): void;
  sendReady(ready: boolean, weapon?: number): void;
  sendStartMatch(): void;
  sendChat(text: string): boolean;
  sendInteract(): void;
  sendCommand(command: Command): void;
  dispose(): void;
}

export function clientClockMs(): number {
  return Date.now() % 4294967296;
}

export function elapsedMs(startMs: number, endMs: number): number {
  return (endMs - startMs) >>> 0;
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
  let resuming = false;
  let autoJoinSuppressed = false;
  let lastWeapon = 0;
  let credentials: StoredCredentials = {
    nickname: sanitizeName(options.name ?? '') ?? '',
    roomCode: normalizeRoomCode(options.roomCode ?? ''),
  };
  let activeRoomCode = credentials.roomCode;

  function setStatus(next: ConnectionStatus, detail = ''): void {
    status = next;
    options.onStatus?.(next, detail);
  }

  function send(bytes: Uint8Array, size: number): boolean {
    if (socket === undefined || socket.readyState !== WebSocket.OPEN) return false;
    socket.send(bytes.subarray(0, size));
    return true;
  }

  function joinTarget(): StoredCredentials | undefined {
    if (autoJoinSuppressed) return undefined;
    if (credentials.nickname === '' || credentials.roomCode === '') return undefined;
    return credentials;
  }

  function tokenStore(): ClientCredentialStorage | undefined {
    try {
      const storage = globalThis.sessionStorage as ClientCredentialStorage | undefined;
      if (storage !== undefined) return storage;
    } catch {
      // 隐私模式等场景取不到 sessionStorage：回退到注入的 storage。
    }
    return options.storage;
  }

  function readToken(): string {
    return readKey(tokenStore(), TOKEN_STORAGE_KEY) ?? '';
  }

  function writeToken(token: string): void {
    try {
      tokenStore()?.setItem(TOKEN_STORAGE_KEY, token);
    } catch {
      // 存储不可用时静默降级：重连会退化成新玩家（对局中则被拒绝）。
    }
  }

  function sendJoin(target: StoredCredentials): void {
    send(
      out,
      encodeJoin(
        {
          protocolVersion: PROTOCOL_VERSION,
          name: target.nickname,
          roomCode: target.roomCode,
          token: readToken(),
        },
        out,
      ),
    );
  }

  function resumeCredentials(): void {
    const stored = readStoredCredentials(options.storage, credentials.nickname);
    credentials = {
      nickname: stored.nickname !== '' ? stored.nickname : credentials.nickname,
      roomCode: stored.roomCode !== '' ? stored.roomCode : credentials.roomCode,
    };
  }

  function handleWelcome(frame: Uint8Array): void {
    const decoded = decodeWelcome(frame);
    if (!decoded.ok) return;
    // O08：服务器下发的会话令牌写入本标签页的 sessionStorage（重连时上行）。
    if (decoded.value.token !== '') writeToken(decoded.value.token);
    if (decoded.value.roomCode !== activeRoomCode) {
      // 换房间（含断线后重新入新房）时 tick 会从头开始，必须清空关键帧缓冲，
      // 否则单调丢弃规则会让画面永久冻结（P04 评审 SPEC-2）。
      activeRoomCode = decoded.value.roomCode;
      options.view.reset();
    }
    credentials = { nickname: credentials.nickname, roomCode: decoded.value.roomCode };
    storeCredentials(options.storage, credentials);
    options.view.setLocalPlayerId(decoded.value.pid);
    options.onWelcome?.(decoded.value);
  }

  function handleErrorFrame(frame: Uint8Array): void {
    const decoded = decodeError(frame);
    if (!decoded.ok) return;
    const { code, message } = decoded.value;
    options.onServerError?.(code, message);
    // O08：下列错误说明"带令牌的重连被拒/会话已失效"，令牌必须清掉，否则会反复失败。
    if (
      code === ERROR_CODE.protocolMismatch ||
      code === ERROR_CODE.matchInProgress ||
      code === ERROR_CODE.roomNotFound
    ) {
      writeToken('');
    }
    if (code === ERROR_CODE.matchInProgress) {
      autoJoinSuppressed = true;
      credentials = { nickname: credentials.nickname, roomCode: '' };
      clearStoredRoomCode(options.storage);
      options.onReturnToLobby?.(
        (ERROR_MESSAGES[ERROR_CODE.matchInProgress] ?? message) +
          '（会话已失效，请作为新玩家加入）',
      );
      return;
    }
    if (code === ERROR_CODE.roomNotFound || code === ERROR_CODE.roomFull) {
      autoJoinSuppressed = true;
      credentials = { nickname: credentials.nickname, roomCode: '' };
      clearStoredRoomCode(options.storage);
    }
  }

  function handleFrame(data: ArrayBuffer): void {
    const frame = new Uint8Array(data);
    if (frame.length === 0) return;
    const opcode = frame[0];
    if (opcode === OPCODE.snapshot) {
      if (options.view.applyFrame(frame, frame.length)) options.onSnapshotApplied?.(frame.length);
      return;
    }
    if (opcode === OPCODE.welcome) {
      handleWelcome(frame);
      return;
    }
    if (opcode === OPCODE.matchState) {
      const decoded = decodeMatchState(frame);
      if (decoded.ok) options.onMatchState?.(decoded.value);
      return;
    }
    if (opcode === OPCODE.event) {
      events.length = 0;
      const decoded = decodeEventFrame(frame, events);
      if (decoded.ok) options.onEvents?.(decoded.value);
      return;
    }
    if (opcode === OPCODE.chatMessage) {
      const decoded = decodeChatMessage(frame);
      if (decoded.ok) options.onChat?.(decoded.value);
      return;
    }
    if (opcode === OPCODE.pong) {
      const decoded = decodePong(frame);
      if (decoded.ok) {
        snapshotRateX10 = decoded.value.snapshotRateX10;
        options.view.setSnapshotRateX10(snapshotRateX10);
        options.view.recordRtt(elapsedMs(decoded.value.clientTimeMs, clientClockMs()));
      }
      return;
    }
    if (opcode === OPCODE.error) handleErrorFrame(frame);
  }

  function openSocket(): void {
    if (disposed) return;
    if (socket !== undefined) {
      const state = socket.readyState;
      if (state === WebSocket.OPEN || state === WebSocket.CONNECTING) return;
    }
    if (reconnectTimer !== undefined) {
      clearTimeout(reconnectTimer);
      reconnectTimer = undefined;
    }
    setStatus('connecting');
    const next = new WebSocket(options.url);
    next.binaryType = 'arraybuffer';
    socket = next;
    next.addEventListener('open', () => {
      if (socket !== next || disposed) return;
      setStatus('online');
      reconnectAttempts = 0;
      if (resuming) {
        resuming = false;
        resumeCredentials();
      }
      const target = joinTarget();
      if (target !== undefined) sendJoin(target);
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
      if (socket !== next || disposed) return;
      const data = event.data;
      if (data instanceof ArrayBuffer) handleFrame(data);
    });
    next.addEventListener('error', () => {
      if (socket !== next || disposed) return;
      setStatus('error');
    });
    next.addEventListener('close', () => {
      if (socket !== next || disposed) return;
      socket = undefined;
      if (pingTimer !== undefined) {
        clearInterval(pingTimer);
        pingTimer = undefined;
      }
      setStatus('closed');
      if (autoJoinSuppressed || options.autoReconnect === false) return;
      resuming = true;
      reconnectAttempts += 1;
      const delayMs = Math.min(RECONNECT_MAX_MS, RECONNECT_BASE_MS * 2 ** (reconnectAttempts - 1));
      reconnectTimer = setTimeout(() => {
        reconnectTimer = undefined;
        openSocket();
      }, delayMs);
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
    get roomCode(): string {
      return credentials.roomCode;
    },
    joinRoom(roomCode: string, nickname: string): boolean {
      const name = sanitizeName(nickname);
      const code = normalizeRoomCode(roomCode);
      if (name === null || code === '') return false;
      autoJoinSuppressed = false;
      resuming = false;
      credentials = { nickname: name, roomCode: code };
      storeCredentials(options.storage, credentials);
      activeRoomCode = code;
      if (socket !== undefined && socket.readyState === WebSocket.OPEN) {
        setStatus('online');
        sendJoin(credentials);
        return true;
      }
      reconnectAttempts = 0;
      openSocket();
      return true;
    },
    leaveRoom(): void {
      autoJoinSuppressed = true;
      resuming = false;
      credentials = { nickname: credentials.nickname, roomCode: '' };
      clearStoredRoomCode(options.storage);
      if (pingTimer !== undefined) {
        clearInterval(pingTimer);
        pingTimer = undefined;
      }
      const current = socket;
      socket = undefined;
      if (current !== undefined && current.readyState === WebSocket.OPEN) {
        current.send(out.subarray(0, encodeSimpleFrame(OPCODE.leave, out)));
      }
      setStatus('idle');
    },
    sendReady(ready: boolean, weapon?: number): void {
      if (weapon !== undefined) lastWeapon = weapon === 1 ? 1 : weapon === 2 ? 2 : 0;
      send(out, encodeReady({ ready, weapon: lastWeapon }, out));
    },
    sendStartMatch(): void {
      send(out, encodeSimpleFrame(OPCODE.startMatch, out));
    },
    sendChat(text: string): boolean {
      const cleaned = validateChatText(text);
      if (cleaned === null) return false;
      return send(out, encodeChat(cleaned, out));
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
      const current = socket;
      socket = undefined;
      current?.close();
    },
  };
}
