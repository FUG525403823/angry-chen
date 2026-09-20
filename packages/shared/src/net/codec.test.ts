import { describe, expect, it } from 'vitest';

import { createCommand, type Command } from '../command.ts';
import { LIMITS, OPCODE } from './protocol.ts';
import {
  decodeChat,
  decodeCommand,
  decodeError,
  decodeInteract,
  decodeJoin,
  decodeMatchState,
  decodePing,
  decodePong,
  decodeReady,
  decodeSimpleFrame,
  decodeWelcome,
  encodeChat,
  encodeInteract,
  encodeCommand,
  encodeError,
  encodeJoin,
  encodePing,
  encodePong,
  encodeReady,
  encodeSimpleFrame,
  encodeWelcome,
  sanitizeChat,
  sanitizeName,
  truncateUtf8,
  utf8Length,
} from './codec.ts';

const buffer = new Uint8Array(LIMITS.maxFrameBytes);

function expectOk<T>(result: { ok: true; value: T } | { ok: false; reason: string }): T {
  if (!result.ok) throw new Error('decode failed: ' + result.reason);
  return result.value;
}

function expectFail(result: { ok: boolean; reason?: string }, reason: string): void {
  expect(result.ok).toBe(false);
  expect(result.reason).toBe(reason);
}

describe('命令编解码', () => {
  it('合法命令往返后量化值一致', () => {
    const command = createCommand();
    command.seq = 4096;
    command.tick = 123456;
    command.moveX = 1;
    command.moveY = -0.5;
    command.yaw = Math.PI / 3;
    command.pitch = -Math.PI / 4;
    command.buttons = 0b1010101;
    command.switchTo = 2;

    const size = encodeCommand(command, buffer);
    expect(size).toBe(15);
    const decoded = expectOk(decodeCommand(buffer.subarray(0, size)));

    expect(decoded.seq).toBe(4096);
    expect(decoded.tick).toBe(123456);
    expect(decoded.moveX).toBeCloseTo(1, 6);
    expect(decoded.moveY).toBeCloseTo(-0.5, 2);
    expect(decoded.yaw).toBeCloseTo(Math.PI / 3, 3);
    expect(decoded.pitch).toBeCloseTo(-Math.PI / 4, 3);
    expect(decoded.buttons).toBe(0b1010101);
    expect(decoded.switchTo).toBe(2);
  });

  it('越界输入被量化夹取，switchTo 越界归零', () => {
    const command = createCommand();
    command.moveX = 9;
    command.moveY = -9;
    command.buttons = 0xff;
    (command as unknown as { switchTo: number }).switchTo = 9;
    const size = encodeCommand(command, buffer);
    const decoded = expectOk(decodeCommand(buffer.subarray(0, size)));
    expect(decoded.moveX).toBeCloseTo(1, 6);
    expect(decoded.moveY).toBeCloseTo(-1, 6);
    expect(decoded.buttons).toBe(0x7f);
    expect(decoded.switchTo).toBe(0);
  });

  it('复用外部 Command 对象', () => {
    const out: Command = createCommand();
    const size = encodeCommand(createCommand(), buffer);
    const decoded = expectOk(decodeCommand(buffer.subarray(0, size), out));
    expect(decoded).toBe(out);
  });

  it('恶意帧：错误 opcode、长度不符', () => {
    expectFail(decodeCommand(new Uint8Array(15)), 'bad-opcode');
    const size = encodeCommand(createCommand(), buffer);
    expectFail(decodeCommand(buffer.subarray(0, size - 1)), 'bad-length');
    expectFail(decodeCommand(buffer.subarray(0, size + 1)), 'bad-length');
  });
});

describe('进房与社交帧', () => {
  it('Join 往返保留协议版本、昵称与房间码', () => {
    const size = encodeJoin({ protocolVersion: 1, name: '陈sir', roomCode: 'ABCD' }, buffer);
    const join = expectOk(decodeJoin(buffer.subarray(0, size)));
    expect(join.protocolVersion).toBe(1);
    expect(join.name).toBe('陈sir');
    expect(join.roomCode).toBe('ABCD');
  });

  it('房间码 0000 表示新建，非法字符被拒', () => {
    const size = encodeJoin({ protocolVersion: 1, name: 'a', roomCode: '0000' }, buffer);
    expect(expectOk(decodeJoin(buffer.subarray(0, size))).roomCode).toBe('0000');

    const bad = encodeJoin({ protocolVersion: 1, name: 'a', roomCode: 'ILO0' }, buffer);
    expectFail(decodeJoin(buffer.subarray(0, bad)), 'bad-value');
  });

  it('昵称长度越界被判非法', () => {
    const frame = new Uint8Array(20);
    frame[0] = OPCODE.join;
    frame[1] = 1;
    frame[2] = 13;
    expectFail(decodeJoin(frame.subarray(0, 3 + 13 + 4)), 'name-invalid');
  });

  it('Ready / Ping / 空帧往返', () => {
    let size = encodeReady({ ready: true, weapon: 2 }, buffer);
    expect(expectOk(decodeReady(buffer.subarray(0, size)))).toEqual({ ready: true, weapon: 2 });

    size = encodePing({ clientTimeMs: 4242, lastRecvTick: 99 }, buffer);
    expect(expectOk(decodePing(buffer.subarray(0, size)))).toEqual({
      clientTimeMs: 4242,
      lastRecvTick: 99,
    });

    size = encodeSimpleFrame(OPCODE.leave, buffer);
    expect(size).toBe(1);
    expect(expectOk(decodeSimpleFrame(buffer.subarray(0, size), OPCODE.leave))).toBe(true);
    expectFail(decodeSimpleFrame(buffer.subarray(0, size), OPCODE.respawn), 'bad-opcode');
  });

  it('Chat 剥离控制字符并限制长度', () => {
    const bell = String.fromCharCode(7);
    let size = encodeChat('你好' + bell + '世界', buffer);
    expect(expectOk(decodeChat(buffer.subarray(0, size)))).toBe('你好世界');

    size = encodeChat('x'.repeat(200), buffer);
    const decoded = expectOk(decodeChat(buffer.subarray(0, size)));
    expect(utf8Length(decoded)).toBe(LIMITS.maxChatBytes);
    expect(size).toBeLessThanOrEqual(2 + LIMITS.maxChatBytes);
  });

  it('昵称净化：控制字符与危险字符被剥除，非法字符整体拒绝', () => {
    expect(sanitizeName('  陈sir  ')).toBe('陈sir');
    expect(sanitizeName('a<b>&' + String.fromCharCode(34) + 'c')).toBe('abc');
    expect(sanitizeName('bad name')).toBeNull();
    expect(sanitizeName('')).toBeNull();
    expect(sanitizeName('123456789012345')).toBeNull();
    expect(sanitizeChat('a' + String.fromCharCode(0) + 'b')).toBe('ab');
  });

  it('truncateUtf8 不切碎多字节字符', () => {
    const cut = truncateUtf8('中'.repeat(10), 7);
    expect(utf8Length(cut)).toBe(6);
    expect(cut).toBe('中中');
  });
});

describe('服务器下行帧', () => {
  it('Welcome / Pong / Error 往返', () => {
    let size = encodeWelcome(
      { pid: 7, roomCode: 'WXYZ', protocolVersion: 1, tick: 15, serverTimeMs: 750 },
      buffer,
    );
    expect(expectOk(decodeWelcome(buffer.subarray(0, size)))).toEqual({
      pid: 7,
      roomCode: 'WXYZ',
      protocolVersion: 1,
      tick: 15,
      serverTimeMs: 750,
    });

    size = encodePong({ clientTimeMs: 1, serverTimeMs: 2, snapshotRateX10: 200 }, buffer);
    expect(expectOk(decodePong(buffer.subarray(0, size)))).toEqual({
      clientTimeMs: 1,
      serverTimeMs: 2,
      snapshotRateX10: 200,
    });

    size = encodeError(5, '帧格式非法', buffer);
    expect(expectOk(decodeError(buffer.subarray(0, size)))).toEqual({
      code: 5,
      message: '帧格式非法',
    });
  });

  it('Error 超长消息被安全截断', () => {
    const size = encodeError(1, 'x'.repeat(400), buffer);
    expect(utf8Length(expectOk(decodeError(buffer.subarray(0, size))).message)).toBeLessThanOrEqual(
      255,
    );
    expect(size).toBeLessThanOrEqual(3 + 255);
  });

  it('Interact 帧往返并归一化布尔值', () => {
    let size = encodeInteract(1, buffer);
    expect(size).toBe(2);
    expect(expectOk(decodeInteract(buffer.subarray(0, size)))).toBe(1);

    size = encodeInteract(0, buffer);
    expect(expectOk(decodeInteract(buffer.subarray(0, size)))).toBe(0);

    size = encodeInteract(7, buffer);
    expect(expectOk(decodeInteract(buffer.subarray(0, size)))).toBe(1);

    expectFail(decodeInteract(new Uint8Array([OPCODE.interact, 1, 2, 3])), 'bad-length');
    expectFail(decodeInteract(new Uint8Array([0xff, 1])), 'bad-opcode');
  });

  it('未知 opcode 一律返回失败而不抛异常', () => {
    const frame = new Uint8Array([0xff, 1, 2, 3]);
    expectFail(decodeCommand(frame), 'bad-opcode');
    expectFail(decodeWelcome(frame), 'bad-opcode');
    expectFail(decodePong(frame), 'bad-opcode');
    expectFail(decodeError(frame), 'bad-opcode');
    expectFail(decodeReady(frame), 'bad-opcode');
    expectFail(decodeJoin(frame), 'bad-opcode');
    expectFail(decodeChat(frame), 'bad-opcode');
    expectFail(decodeMatchState(frame), 'bad-opcode');
  });
});
