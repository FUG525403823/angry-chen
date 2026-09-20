import { CLIENT_OPCODE_LIST, LIMITS, sanitizeChat, sanitizeName } from '@ac/shared';

export const MAX_FRAME_BYTES = LIMITS.maxFrameBytes;
export const RATE_LIMIT_MAX_MESSAGES = LIMITS.maxMessagesPerSecond;
export const RATE_LIMIT_WINDOW_MS = LIMITS.rateWindowMs;
export const RATE_LIMIT_STRIKES_BEFORE_DISCONNECT = LIMITS.rateStrikesBeforeDisconnect;
export const NICKNAME_MIN_BYTES = LIMITS.minNameBytes;
export const NICKNAME_MAX_BYTES = LIMITS.maxNameBytes;
export const CHAT_MAX_BYTES = LIMITS.maxChatBytes;
export const ROOM_CODE_ALPHABET = LIMITS.roomCodeAlphabet;
export const ROOM_CODE_LENGTH = LIMITS.roomCodeLength;
export const ROOM_CODE_ATTEMPTS = LIMITS.roomCodeAttempts;
export const ROOM_CODE_SPACE = Math.pow(ROOM_CODE_ALPHABET.length, ROOM_CODE_LENGTH);
export const JOIN_THROTTLE_MAX_FAILURES = 5;
export const JOIN_THROTTLE_WINDOW_MS = 10000;
export const ALLOWED_ORIGINS_ENV = 'ALLOWED_ORIGINS';
export const ALLOWED_ORIGINS_WILDCARD = '*';

export const CLIENT_OPCODES: readonly number[] = Object.freeze([...CLIENT_OPCODE_LIST]);

export function isFrameWithinLimit(length: number): boolean {
  return length <= MAX_FRAME_BYTES;
}

export function isAllowedClientOpcode(opcode: number): boolean {
  return CLIENT_OPCODES.includes(opcode);
}

export function sanitizeNickname(raw: string): string | null {
  return sanitizeName(raw);
}

export function sanitizeChatText(raw: string): string {
  return sanitizeChat(raw);
}

export function isValidRoomCodeFormat(code: string): boolean {
  if (code.length !== ROOM_CODE_LENGTH) return false;
  for (const char of code) {
    if (!ROOM_CODE_ALPHABET.includes(char)) return false;
  }
  return true;
}

export function pickRoomCode(random: () => number): string {
  let code = '';
  for (let i = 0; i < ROOM_CODE_LENGTH; i += 1) {
    const raw = Math.floor(random() * ROOM_CODE_ALPHABET.length);
    const index = Math.min(Math.max(raw, 0), ROOM_CODE_ALPHABET.length - 1);
    code += ROOM_CODE_ALPHABET[index] ?? 'A';
  }
  return code;
}

export interface RateLimitState {
  readonly times: Float64Array;
  count: number;
  strikes: number;
}

export type RateLimitVerdict = 'ok' | 'limited' | 'disconnect';

export function createRateLimitState(
  maxMessages: number = RATE_LIMIT_MAX_MESSAGES,
): RateLimitState {
  return { times: new Float64Array(maxMessages + 1), count: 0, strikes: 0 };
}

export function checkRateLimit(
  state: RateLimitState,
  nowMs: number,
  maxMessages: number = RATE_LIMIT_MAX_MESSAGES,
  windowMs: number = RATE_LIMIT_WINDOW_MS,
  strikesBeforeDisconnect: number = RATE_LIMIT_STRIKES_BEFORE_DISCONNECT,
): RateLimitVerdict {
  const times = state.times;
  let kept = 0;
  for (let i = 0; i < state.count; i += 1) {
    const at = times[i];
    if (at !== undefined && nowMs - at < windowMs) {
      times[kept] = at;
      kept += 1;
    }
  }
  state.count = kept;
  if (kept >= maxMessages) {
    state.strikes += 1;
    return state.strikes >= strikesBeforeDisconnect ? 'disconnect' : 'limited';
  }
  if (state.strikes > 0 && kept === 0) state.strikes = 0;
  times[kept] = nowMs;
  state.count = kept + 1;
  return 'ok';
}

export interface JoinThrottle {
  readonly failures: number;
  registerFailure(nowMs: number): boolean;
  reset(): void;
}

export function createJoinThrottle(
  maxFailures: number = JOIN_THROTTLE_MAX_FAILURES,
  windowMs: number = JOIN_THROTTLE_WINDOW_MS,
): JoinThrottle {
  let failures = 0;
  let windowStartMs = 0;
  const state: JoinThrottle = {
    get failures(): number {
      return failures;
    },
    registerFailure(nowMs: number): boolean {
      if (failures === 0 || nowMs - windowStartMs >= windowMs) {
        failures = 0;
        windowStartMs = nowMs;
      }
      failures += 1;
      return failures >= maxFailures;
    },
    reset(): void {
      failures = 0;
      windowStartMs = 0;
    },
  };
  return state;
}

export type OriginPolicyMode = 'any' | 'allowlist';

export interface OriginPolicy {
  readonly mode: OriginPolicyMode;
  readonly origins: readonly string[];
}

export function parseAllowedOrigins(raw: string | undefined): OriginPolicy {
  if (raw === undefined) return { mode: 'any', origins: [] };
  const entries = raw
    .split(',')
    .map((entry) => entry.trim().toLowerCase())
    .filter((entry) => entry.length > 0);
  if (entries.length === 0) return { mode: 'any', origins: [] };
  if (entries.includes(ALLOWED_ORIGINS_WILDCARD)) return { mode: 'any', origins: [] };
  return { mode: 'allowlist', origins: entries };
}

export function isOriginAllowed(policy: OriginPolicy, origin: string | undefined): boolean {
  if (policy.mode === 'any') return true;
  if (origin === undefined || origin.trim() === '') return true;
  return policy.origins.includes(origin.trim().toLowerCase());
}
