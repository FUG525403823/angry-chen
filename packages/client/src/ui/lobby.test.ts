import { ERROR_CODE } from '@ac/shared';
import { describe, expect, it } from 'vitest';

import {
  CREATE_ROOM_CODE,
  errorMessageFor,
  filterNicknameInput,
  filterRoomCodeInput,
  nicknameIsValid,
  roomCodeIsValid,
} from './lobby.ts';

describe('大厅输入净化', () => {
  it('昵称剔除非法字符并截断到 12 字节', () => {
    expect(filterNicknameInput('<a&b>')).toBe('ab');
    expect(filterNicknameInput('中文名字很长很长很长很长')).toBe('中文名字');
    expect(filterNicknameInput('a'.repeat(30))).toBe('a'.repeat(12));
  });

  it('房间码转大写并过滤易混淆字符', () => {
    expect(filterRoomCodeInput('ab0c1d2e')).toBe('ABCD');
    expect(filterRoomCodeInput('ilo')).toBe('');
    expect(filterRoomCodeInput('q7w9')).toBe('Q7W9');
  });

  it('昵称合法性遵循 1–12 字节规则', () => {
    expect(nicknameIsValid('陈sir')).toBe(true);
    expect(nicknameIsValid('a'.repeat(12))).toBe(true);
    expect(nicknameIsValid('a'.repeat(13))).toBe(false);
    expect(nicknameIsValid('')).toBe(false);
    expect(nicknameIsValid('a b')).toBe(false);
    expect(nicknameIsValid('a😀')).toBe(false);
    expect(nicknameIsValid('ab&cd')).toBe(true);
  });

  it('房间码合法性接受 0000 表示创建房间', () => {
    expect(CREATE_ROOM_CODE).toBe('0000');
    expect(roomCodeIsValid('AB2D')).toBe(true);
    expect(roomCodeIsValid('0000')).toBe(true);
    expect(roomCodeIsValid('AB1D')).toBe(false);
    expect(roomCodeIsValid('AB2')).toBe(false);
  });
});

describe('大厅错误码文案', () => {
  it('把服务器错误码映射成具体提示', () => {
    expect(errorMessageFor(ERROR_CODE.roomNotFound, 'raw')).toBe('房间不存在或已结束');
    expect(errorMessageFor(ERROR_CODE.roomFull, 'raw')).toBe('房间已满（最多 4 人）');
    expect(errorMessageFor(ERROR_CODE.matchInProgress, 'raw')).toBe('对局已开始，无法加入');
    expect(errorMessageFor(ERROR_CODE.notHost, 'raw')).toBe('只有房主可以开始对局');
    expect(errorMessageFor(ERROR_CODE.invalidName, 'raw')).toBe('昵称不合法（1–12 字节）');
  });

  it('未知错误码回落到服务器文案', () => {
    expect(errorMessageFor(99, 'server says')).toBe('server says');
  });
});
