import type { IncomingMessage } from 'node:http';
import type { Duplex } from 'node:stream';

import {
  BUTTON,
  BUTTON_MASK_ALL,
  CLIENT_OPCODE_LIST,
  INPUT,
  OPCODE,
  createCommand,
} from '@ac/shared';
import { describe, expect, it } from 'vitest';

import {
  MAX_FRAME_BYTES,
  RATE_LIMIT_MAX_MESSAGES,
  RATE_LIMIT_STRIKES_BEFORE_DISCONNECT,
  RATE_LIMIT_WINDOW_MS,
  ROOM_CODE_ALPHABET,
  ROOM_CODE_LENGTH,
  ROOM_CODE_SPACE,
  checkRateLimit,
  createJoinThrottle,
  createOriginGuard,
  createRateLimitState,
  isAllowedClientOpcode,
  isFrameWithinLimit,
  isOriginAllowed,
  isValidRoomCodeFormat,
  parseAllowedOrigins,
  pickRoomCode,
  sanitizeChatText,
  sanitizeCommandFields,
  sanitizeNickname,
  securityChecklist,
} from './security.ts';

function fakeRequest(origin?: string): IncomingMessage {
  return {
    headers: origin === undefined ? {} : { origin },
  } as unknown as IncomingMessage;
}

function fakeSocket(): { destroyed: boolean; destroy(): void } {
  return {
    destroyed: false,
    destroy(): void {
      this.destroyed = true;
    },
  };
}

describe('安全清单代码化', () => {
  it('冻结的限额常量与 P03/P02 真源一致', () => {
    expect(MAX_FRAME_BYTES).toBe(8192);
    expect(RATE_LIMIT_MAX_MESSAGES).toBe(60);
    expect(RATE_LIMIT_WINDOW_MS).toBe(1000);
    expect(RATE_LIMIT_STRIKES_BEFORE_DISCONNECT).toBe(3);
    expect(ROOM_CODE_LENGTH).toBe(4);
    expect(ROOM_CODE_ALPHABET.length).toBe(31);
    for (const excluded of 'ILO01') expect(ROOM_CODE_ALPHABET.includes(excluded)).toBe(false);
  });

  it('#2 单帧上限 8192 字节', () => {
    expect(isFrameWithinLimit(0)).toBe(true);
    expect(isFrameWithinLimit(MAX_FRAME_BYTES)).toBe(true);
    expect(isFrameWithinLimit(MAX_FRAME_BYTES + 1)).toBe(false);
  });

  it('#4 opcode 白名单只接受 P03 客户端码表', () => {
    for (const opcode of CLIENT_OPCODE_LIST) expect(isAllowedClientOpcode(opcode)).toBe(true);
    expect(isAllowedClientOpcode(0x00)).toBe(false);
    expect(isAllowedClientOpcode(0xff)).toBe(false);
    expect(isAllowedClientOpcode(OPCODE.welcome)).toBe(false);
    expect(isAllowedClientOpcode(OPCODE.snapshot)).toBe(false);
  });

  it('#8 房间码熵为 31 字符表 4 位并只取白名单字符', () => {
    expect(ROOM_CODE_SPACE).toBe(Math.pow(ROOM_CODE_ALPHABET.length, 4));
    expect(ROOM_CODE_SPACE).toBe(923521);
    expect(ROOM_CODE_SPACE).toBeGreaterThan(900000);
    const code = pickRoomCode(() => 0);
    expect(code).toBe('AAAA');
    expect(isValidRoomCodeFormat(code)).toBe(true);
    let seed = 0.123;
    const random = (): number => {
      seed = (seed * 9301 + 49297) % 233280;
      return seed / 233280;
    };
    for (let i = 0; i < 50; i += 1) {
      const sample = pickRoomCode(random);
      expect(sample.length).toBe(4);
      expect(isValidRoomCodeFormat(sample)).toBe(true);
    }
    expect(isValidRoomCodeFormat('ABCD')).toBe(true);
    expect(isValidRoomCodeFormat('ABID')).toBe(false);
    expect(isValidRoomCodeFormat('ABC')).toBe(false);
  });

  it('#3 限流 60 msg/s 滑窗与 3 次超限断开', () => {
    const state = createRateLimitState(2);
    expect(checkRateLimit(state, 0, 2)).toBe('ok');
    expect(checkRateLimit(state, 1, 2)).toBe('ok');
    expect(checkRateLimit(state, 2, 2)).toBe('limited');
    expect(checkRateLimit(state, 3, 2)).toBe('limited');
    expect(checkRateLimit(state, 4, 2)).toBe('disconnect');
    expect(state.strikes).toBe(3);
    expect(checkRateLimit(state, 2000, 2)).toBe('ok');
    expect(state.strikes).toBe(0);
  });

  it('#8 失败 Join 重试节流', () => {
    const throttle = createJoinThrottle(3, 1000);
    expect(throttle.registerFailure(0)).toBe(false);
    expect(throttle.registerFailure(100)).toBe(false);
    expect(throttle.registerFailure(200)).toBe(true);
    expect(throttle.failures).toBe(3);
    throttle.reset();
    expect(throttle.failures).toBe(0);
    const windowed = createJoinThrottle(2, 1000);
    expect(windowed.registerFailure(0)).toBe(false);
    expect(windowed.registerFailure(1000)).toBe(false);
    expect(windowed.registerFailure(1100)).toBe(true);
  });

  it('#7 昵称与聊天净化', () => {
    expect(sanitizeNickname('alice')).toBe('alice');
    expect(sanitizeNickname('陈sir')).toBe('陈sir');
    expect(sanitizeNickname('bad name')).toBeNull();
    expect(sanitizeNickname('a<b')).toBe('ab');
    expect(sanitizeNickname('x</b>')).toBeNull();
    expect(sanitizeNickname('this-name-is-far-too-long')).toBeNull();
    expect(sanitizeChatText('a\u0000b')).toBe('ab');
    expect(sanitizeChatText('x'.repeat(200)).length).toBeLessThanOrEqual(64);
  });

  it('#5 字段范围校验复用 sanitizeCommand', () => {
    const out = createCommand();
    sanitizeCommandFields(
      { moveX: 999, moveY: -999, yaw: Math.PI * 4 + 0.5, buttons: 0xff, switchTo: 2 },
      out,
    );
    expect(out.moveX).toBe(INPUT.moveAxisLimit);
    expect(out.moveY).toBe(-INPUT.moveAxisLimit);
    expect(Math.abs(out.yaw)).toBeLessThanOrEqual(Math.PI);
    expect(out.buttons).toBe(BUTTON_MASK_ALL);
    expect(out.switchTo).toBe(2);

    const noButton = createCommand();
    sanitizeCommandFields({ buttons: 0, switchTo: 2 }, noButton);
    expect(noButton.switchTo).toBe(0);

    const withButton = createCommand();
    sanitizeCommandFields({ buttons: BUTTON.switchWeapon, switchTo: 2 }, withButton);
    expect(withButton.switchTo).toBe(2);
  });

  it('#9 ALLOWED_ORIGINS 解析与白名单判定', () => {
    expect(parseAllowedOrigins(undefined)).toEqual({ mode: 'any', origins: [] });
    expect(parseAllowedOrigins('')).toEqual({ mode: 'any', origins: [] });
    expect(parseAllowedOrigins('*')).toEqual({ mode: 'any', origins: [] });
    const policy = parseAllowedOrigins('https://ok.test, http://localhost:5173');
    expect(policy.mode).toBe('allowlist');
    expect(isOriginAllowed(policy, 'HTTPS://OK.TEST')).toBe(true);
    expect(isOriginAllowed(policy, 'http://localhost:5173')).toBe(true);
    expect(isOriginAllowed(policy, 'https://evil.test')).toBe(false);
    expect(isOriginAllowed(policy, undefined)).toBe(true);
    expect(isOriginAllowed(parseAllowedOrigins(undefined), 'https://anything')).toBe(true);
  });

  it('#9 升级守卫拒绝白名单外来源并放行白名单来源', () => {
    const rejected: (string | undefined)[] = [];
    const guard = createOriginGuard(parseAllowedOrigins('https://ok.test'), (origin) => {
      rejected.push(origin);
    });
    const denied = fakeSocket();
    guard(fakeRequest('https://evil.test'), denied as unknown as Duplex);
    expect(denied.destroyed).toBe(true);
    expect(rejected).toEqual(['https://evil.test']);

    const allowed = fakeSocket();
    guard(fakeRequest('https://ok.test'), allowed as unknown as Duplex);
    expect(allowed.destroyed).toBe(false);

    const noOrigin = fakeSocket();
    guard(fakeRequest(), noOrigin as unknown as Duplex);
    expect(noOrigin.destroyed).toBe(false);
  });

  it('#1-#12 清单有 12 项且每项标注落地位置', () => {
    const checklist = securityChecklist();
    expect(checklist.length).toBe(12);
    expect(checklist.map((entry) => entry.id)).toEqual([1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12]);
    expect(checklist.every((entry) => entry.location.length > 0)).toBe(true);
    expect(checklist.every((entry) => entry.item.length > 0)).toBe(true);
    expect(checklist.filter((entry) => entry.enforcedInSecurity).map((entry) => entry.id)).toEqual([
      2, 3, 4, 5, 7, 8, 9,
    ]);
  });
});
