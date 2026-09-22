import { createCommand, type Command } from '../command.ts';
import { MAX_ENTITIES } from '../config/index.ts';
import { TWO_PI, clamp, wrapAngle } from '../math.ts';
import type { Snapshot, SnapshotEntity } from '../snapshot.ts';
import { createSimEvent } from '../world.ts';
import type { EntityKind, SimEvent } from '../world.ts';
import {
  ENTITY_KIND_CODE,
  ENTITY_KIND_NAMES,
  EVENT_TYPE,
  LIMITS,
  OPCODE,
  QUANT,
  SNAPSHOT_HEADER_BYTES,
  SNAPSHOT_RECORD_BYTES,
  U8_MAX,
} from './protocol.ts';
import type { ErrorFrame, MatchState, Pong, Welcome } from './protocol.ts';

export type DecodeFailure =
  'truncated' | 'bad-opcode' | 'bad-length' | 'bad-value' | 'unknown-event' | 'name-invalid';

export type DecodeResult<T> = { ok: true; value: T } | { ok: false; reason: DecodeFailure };

const textEncoder = new TextEncoder();
const textDecoder = new TextDecoder();

function ok<T>(value: T): DecodeResult<T> {
  return { ok: true, value };
}

function fail<T>(reason: DecodeFailure): DecodeResult<T> {
  return { ok: false, reason };
}

function u8(bytes: Uint8Array, offset: number): number {
  return bytes[offset] ?? 0;
}

function u16(bytes: Uint8Array, offset: number): number {
  return (bytes[offset] ?? 0) | ((bytes[offset + 1] ?? 0) << 8);
}

function u32(bytes: Uint8Array, offset: number): number {
  const low =
    (bytes[offset] ?? 0) | ((bytes[offset + 1] ?? 0) << 8) | ((bytes[offset + 2] ?? 0) << 16);
  return low + (bytes[offset + 3] ?? 0) * 0x1000000;
}

function i8(bytes: Uint8Array, offset: number): number {
  const value = bytes[offset] ?? 0;
  return value >= 0x80 ? value - 0x100 : value;
}

function i16(bytes: Uint8Array, offset: number): number {
  const value = u16(bytes, offset);
  return value >= 0x8000 ? value - 0x10000 : value;
}

function putU8(bytes: Uint8Array, offset: number, value: number): void {
  bytes[offset] = value & 0xff;
}

function putU16(bytes: Uint8Array, offset: number, value: number): void {
  bytes[offset] = value & 0xff;
  bytes[offset + 1] = (value >>> 8) & 0xff;
}

function putU32(bytes: Uint8Array, offset: number, value: number): void {
  bytes[offset] = value & 0xff;
  bytes[offset + 1] = (value >>> 8) & 0xff;
  bytes[offset + 2] = (value >>> 16) & 0xff;
  bytes[offset + 3] = (value >>> 24) & 0xff;
}

function putI8(bytes: Uint8Array, offset: number, value: number): void {
  putU8(bytes, offset, value < 0 ? value + 0x100 : value);
}

function putI16(bytes: Uint8Array, offset: number, value: number): void {
  putU16(bytes, offset, value < 0 ? value + 0x10000 : value);
}

function writeUtf8(bytes: Uint8Array, offset: number, text: string, maxBytes: number): number {
  const encoded = textEncoder.encode(text);
  const length = encoded.length < maxBytes ? encoded.length : maxBytes;
  for (let i = 0; i < length; i += 1) bytes[offset + i] = encoded[i] ?? 0;
  return length;
}

function readUtf8(bytes: Uint8Array, offset: number, length: number): string {
  return textDecoder.decode(bytes.subarray(offset, offset + length));
}

export function truncateUtf8(text: string, maxBytes: number): string {
  if (textEncoder.encode(text).length <= maxBytes) return text;
  let result = '';
  let used = 0;
  for (const char of text) {
    const width = textEncoder.encode(char).length;
    if (used + width > maxBytes) break;
    result += char;
    used += width;
  }
  return result;
}

export function utf8Length(text: string): number {
  return textEncoder.encode(text).length;
}

export function quantizePosition(meters: number): number {
  const centimeters = Math.round(meters * QUANT.centimeterPerMeter);
  if (centimeters > QUANT.maxPositionCm) return QUANT.maxPositionCm;
  if (centimeters < QUANT.minPositionCm) return QUANT.minPositionCm;
  return centimeters;
}

export function dequantizePosition(centimeters: number): number {
  return centimeters / QUANT.centimeterPerMeter;
}

export function quantizeAngle(radians: number): number {
  let units = Math.round((wrapAngle(radians) / TWO_PI) * QUANT.angleUnits) % QUANT.angleUnits;
  if (units < 0) units += QUANT.angleUnits;
  return units === 0 ? 0 : units;
}

export function dequantizeAngle(units: number): number {
  return wrapAngle((units / QUANT.angleUnits) * TWO_PI);
}

export function quantizeAxis(axis: number): number {
  return (
    clamp(Math.round(axis * QUANT.moveAxisScale), -QUANT.moveAxisScale, QUANT.moveAxisScale) | 0
  );
}

export function dequantizeAxis(value: number): number {
  return value / QUANT.moveAxisScale;
}

export function quantizeRatio(ratio: number): number {
  return clamp(Math.round(ratio * QUANT.hpUnits), 0, QUANT.hpUnits) | 0;
}

export function dequantizeRatio(units: number): number {
  return units / QUANT.hpUnits;
}

export function encodeCommand(command: Command, out: Uint8Array): number {
  putU8(out, 0, OPCODE.inputCmd);
  putU16(out, 1, command.seq & 0xffff);
  putU32(out, 3, command.tick >>> 0);
  putI8(out, 7, quantizeAxis(command.moveX));
  putI8(out, 8, quantizeAxis(command.moveY));
  putU16(out, 9, quantizeAngle(command.yaw));
  putU16(out, 11, quantizeAngle(command.pitch));
  putU8(out, 13, command.buttons & 0x7f);
  putU8(out, 14, command.switchTo & 0xff);
  return 15;
}

export function decodeCommand(
  frame: Uint8Array,
  out: Command = createCommand(),
): DecodeResult<Command> {
  if (u8(frame, 0) !== OPCODE.inputCmd) return fail('bad-opcode');
  if (frame.length !== 15) return fail('bad-length');
  out.seq = u16(frame, 1);
  out.tick = u32(frame, 3);
  out.moveX = dequantizeAxis(i8(frame, 7));
  out.moveY = dequantizeAxis(i8(frame, 8));
  out.yaw = dequantizeAngle(u16(frame, 9));
  out.pitch = dequantizeAngle(u16(frame, 11));
  out.buttons = u8(frame, 13) & 0x7f;
  const weapon = u8(frame, 14);
  out.switchTo = weapon === 1 ? 1 : weapon === 2 ? 2 : 0;
  return ok(out);
}

export const NEW_ROOM_CODE = '0000';

export function isValidRoomCode(code: string): boolean {
  if (code.length !== LIMITS.roomCodeLength) return false;
  if (code === NEW_ROOM_CODE) return true;
  for (let i = 0; i < code.length; i += 1) {
    if (!LIMITS.roomCodeAlphabet.includes(code[i] ?? '')) return false;
  }
  return true;
}

function stripControlChars(text: string): string {
  let result = '';
  for (const char of text) {
    const code = char.codePointAt(0) ?? 0;
    if (code < 0x20 || code === 0x7f) continue;
    result += char;
  }
  return result;
}

function stripForbiddenNameChars(text: string): string {
  let result = '';
  for (const char of text) {
    const code = char.codePointAt(0) ?? 0;
    if (code === 0x3c || code === 0x3e || code === 0x26 || code === 0x22 || code === 0x27) continue;
    result += char;
  }
  return result;
}

function isAllowedNameChar(char: string): boolean {
  const code = char.codePointAt(0) ?? 0;
  if (code >= 0x30 && code <= 0x39) return true;
  if (code >= 0x41 && code <= 0x5a) return true;
  if (code >= 0x61 && code <= 0x7a) return true;
  if (code === 0x5f) return true;
  if (code >= 0x3400 && code <= 0x4dbf) return true;
  if (code >= 0x4e00 && code <= 0x9fff) return true;
  if (code >= 0xf900 && code <= 0xfaff) return true;
  return false;
}

export function sanitizeChat(message: string): string {
  return truncateUtf8(stripControlChars(message), LIMITS.maxChatBytes);
}

export function sanitizeName(raw: string): string | null {
  const stripped = stripForbiddenNameChars(stripControlChars(raw)).trim();
  if (stripped.length === 0) return null;
  for (const char of stripped) {
    if (!isAllowedNameChar(char)) return null;
  }
  const bytes = utf8Length(stripped);
  if (bytes < LIMITS.minNameBytes || bytes > LIMITS.maxNameBytes) return null;
  return stripped;
}

export interface JoinRequest {
  protocolVersion: number;
  name: string;
  roomCode: string;
  /** O08：会话令牌（空串 = 无令牌，按新玩家处理）。 */
  token: string;
}

/** O08：令牌线上是 8 字节 ASCII 十六进制，尾部 0x00 用于补位；返回 null 表示格式非法。 */
function readToken(frame: Uint8Array, offset: number): string | null {
  let length = LIMITS.tokenBytes;
  while (length > 0 && (frame[offset + length - 1] ?? 0) === 0) length -= 1;
  let token = '';
  for (let i = 0; i < length; i += 1) {
    const code = frame[offset + i] ?? 0;
    const isHex =
      (code >= 0x30 && code <= 0x39) ||
      (code >= 0x41 && code <= 0x46) ||
      (code >= 0x61 && code <= 0x66);
    if (!isHex) return null;
    token += String.fromCharCode(code);
  }
  return token;
}

/** O08：把令牌写进 8 字节槽位，不足部分补 0x00（空令牌 = 全 0）。 */
function writeToken(out: Uint8Array, offset: number, token: string): number {
  const length = writeUtf8(out, offset, token, LIMITS.tokenBytes);
  for (let i = length; i < LIMITS.tokenBytes; i += 1) out[offset + i] = 0;
  return offset + LIMITS.tokenBytes;
}

export function encodeJoin(request: JoinRequest, out: Uint8Array): number {
  const nameLength = writeUtf8(out, 3, request.name, LIMITS.maxNameBytes);
  putU8(out, 0, OPCODE.join);
  putU8(out, 1, request.protocolVersion & 0xff);
  putU8(out, 2, nameLength);
  for (let i = 0; i < LIMITS.roomCodeLength; i += 1) {
    out[3 + nameLength + i] = (request.roomCode.charCodeAt(i) || 0x30) & 0xff;
  }
  return writeToken(out, 3 + nameLength + LIMITS.roomCodeLength, request.token);
}

export function decodeJoin(frame: Uint8Array): DecodeResult<JoinRequest> {
  if (u8(frame, 0) !== OPCODE.join) return fail('bad-opcode');
  if (frame.length < 6) return fail('truncated');
  const nameLength = u8(frame, 2);
  if (nameLength < LIMITS.minNameBytes || nameLength > LIMITS.maxNameBytes)
    return fail('name-invalid');
  const base = 3 + nameLength + LIMITS.roomCodeLength;
  // O08：只接受两种长度 —— 旧布局（无令牌）与新布局（+8 字节令牌）。
  if (frame.length !== base && frame.length !== base + LIMITS.tokenBytes) return fail('bad-length');
  const roomCode = readUtf8(frame, 3 + nameLength, LIMITS.roomCodeLength);
  if (!isValidRoomCode(roomCode)) return fail('bad-value');
  let token = '';
  if (frame.length === base + LIMITS.tokenBytes) {
    const parsed = readToken(frame, base);
    if (parsed === null) return fail('bad-value');
    token = parsed;
  }
  return ok({
    protocolVersion: u8(frame, 1),
    name: readUtf8(frame, 3, nameLength),
    roomCode,
    token,
  });
}

export interface ReadyRequest {
  ready: boolean;
  weapon: number;
}

export function encodeReady(request: ReadyRequest, out: Uint8Array): number {
  putU8(out, 0, OPCODE.ready);
  putU8(out, 1, request.ready ? 1 : 0);
  putU8(out, 2, request.weapon & 0xff);
  return 3;
}

export function decodeReady(frame: Uint8Array): DecodeResult<ReadyRequest> {
  if (u8(frame, 0) !== OPCODE.ready) return fail('bad-opcode');
  if (frame.length !== 3) return fail('bad-length');
  return ok({ ready: u8(frame, 1) !== 0, weapon: u8(frame, 2) });
}

export function encodeChat(message: string, out: Uint8Array): number {
  const length = writeUtf8(out, 2, sanitizeChat(message), LIMITS.maxChatBytes);
  putU8(out, 0, OPCODE.chat);
  putU8(out, 1, length);
  return 2 + length;
}

export function decodeChat(frame: Uint8Array): DecodeResult<string> {
  if (u8(frame, 0) !== OPCODE.chat) return fail('bad-opcode');
  if (frame.length < 2) return fail('truncated');
  const length = u8(frame, 1);
  if (length > LIMITS.maxChatBytes) return fail('bad-length');
  if (frame.length !== 2 + length) return fail('bad-length');
  return ok(sanitizeChat(readUtf8(frame, 2, length)));
}

export interface ChatMessage {
  pid: number;
  text: string;
}

export function encodeChatMessage(message: ChatMessage, out: Uint8Array): number {
  putU8(out, 0, OPCODE.chatMessage);
  putU16(out, 1, message.pid & 0xffff);
  const length = writeUtf8(out, 4, sanitizeChat(message.text), LIMITS.maxChatBytes);
  putU8(out, 3, length);
  return 4 + length;
}

export function decodeChatMessage(frame: Uint8Array): DecodeResult<ChatMessage> {
  if (u8(frame, 0) !== OPCODE.chatMessage) return fail('bad-opcode');
  if (frame.length < 4) return fail('truncated');
  const length = u8(frame, 3);
  if (length > LIMITS.maxChatBytes) return fail('bad-length');
  if (frame.length !== 4 + length) return fail('bad-length');
  return ok({ pid: u16(frame, 1), text: sanitizeChat(readUtf8(frame, 4, length)) });
}

export function encodeInteract(act: number, out: Uint8Array): number {
  putU8(out, 0, OPCODE.interact);
  putU8(out, 1, act !== 0 ? 1 : 0);
  return 2;
}

export function decodeInteract(frame: Uint8Array): DecodeResult<number> {
  if (u8(frame, 0) !== OPCODE.interact) return fail('bad-opcode');
  if (frame.length !== 2) return fail('bad-length');
  return ok(u8(frame, 1) !== 0 ? 1 : 0);
}

export interface PingRequest {
  clientTimeMs: number;
  lastRecvTick: number;
}

export function encodePing(request: PingRequest, out: Uint8Array): number {
  putU8(out, 0, OPCODE.ping);
  putU32(out, 1, request.clientTimeMs >>> 0);
  putU32(out, 5, request.lastRecvTick >>> 0);
  return 9;
}

export function decodePing(frame: Uint8Array): DecodeResult<PingRequest> {
  if (u8(frame, 0) !== OPCODE.ping) return fail('bad-opcode');
  if (frame.length !== 9) return fail('bad-length');
  return ok({ clientTimeMs: u32(frame, 1), lastRecvTick: u32(frame, 5) });
}

export function encodeSimpleFrame(opcode: number, out: Uint8Array): number {
  putU8(out, 0, opcode);
  return 1;
}

export function decodeSimpleFrame(frame: Uint8Array, opcode: number): DecodeResult<true> {
  if (u8(frame, 0) !== opcode) return fail('bad-opcode');
  if (frame.length !== 1) return fail('bad-length');
  return ok(true);
}

export function encodeWelcome(welcome: Welcome, out: Uint8Array): number {
  putU8(out, 0, OPCODE.welcome);
  putU16(out, 1, welcome.pid & 0xffff);
  for (let i = 0; i < LIMITS.roomCodeLength; i += 1) {
    out[3 + i] = (welcome.roomCode.charCodeAt(i) || 0x30) & 0xff;
  }
  putU8(out, 7, welcome.protocolVersion & 0xff);
  putU32(out, 8, welcome.tick >>> 0);
  putU32(out, 12, welcome.serverTimeMs >>> 0);
  return writeToken(out, 16, welcome.token);
}

export function decodeWelcome(frame: Uint8Array): DecodeResult<Welcome> {
  if (u8(frame, 0) !== OPCODE.welcome) return fail('bad-opcode');
  if (frame.length !== 16 + LIMITS.tokenBytes) return fail('bad-length');
  return ok({
    pid: u16(frame, 1),
    roomCode: readUtf8(frame, 3, LIMITS.roomCodeLength),
    protocolVersion: u8(frame, 7),
    tick: u32(frame, 8),
    serverTimeMs: u32(frame, 12),
    token: readToken(frame, 16) ?? '',
  });
}

export function encodePong(pong: Pong, out: Uint8Array): number {
  putU8(out, 0, OPCODE.pong);
  putU32(out, 1, pong.clientTimeMs >>> 0);
  putU32(out, 5, pong.serverTimeMs >>> 0);
  putU8(out, 9, pong.snapshotRateX10 & 0xff);
  return 10;
}

export function decodePong(frame: Uint8Array): DecodeResult<Pong> {
  if (u8(frame, 0) !== OPCODE.pong) return fail('bad-opcode');
  if (frame.length !== 10) return fail('bad-length');
  return ok({
    clientTimeMs: u32(frame, 1),
    serverTimeMs: u32(frame, 5),
    snapshotRateX10: u8(frame, 9),
  });
}

export function encodeError(code: number, message: string, out: Uint8Array): number {
  const length = writeUtf8(out, 3, truncateUtf8(message, 255), 255);
  putU8(out, 0, OPCODE.error);
  putU8(out, 1, code & 0xff);
  putU8(out, 2, length);
  return 3 + length;
}

export function decodeError(frame: Uint8Array): DecodeResult<ErrorFrame> {
  if (u8(frame, 0) !== OPCODE.error) return fail('bad-opcode');
  if (frame.length < 3) return fail('truncated');
  const length = u8(frame, 2);
  if (frame.length !== 3 + length) return fail('bad-length');
  return ok({ code: u8(frame, 1), message: readUtf8(frame, 3, length) });
}

export interface SnapshotRecord {
  id: number;
  kind: EntityKind;
  flags: number;
  xCm: number;
  yCm: number;
  zCm: number;
  yawUnits: number;
  pitchUnits: number;
  hpRatioUnits: number;
  state: number;
}

export interface SnapshotBaseline {
  tick: number;
  readonly present: Uint8Array;
  readonly nextPresent: Uint8Array;
  /**
   * 上一 tick 已发布实体 id 的升序紧凑列表（前 presentCount 项有效）。
   * 删除列表只遍历它，把 O(MAX_ENTITIES) 全扫降到 O(本帧增删数)。
   */
  readonly presentIds: Uint16Array;
  presentCount: number;
  /** 本帧正在构建的下一份紧凑列表（编码结束时整体换到 presentIds）。 */
  readonly nextPresentIds: Uint16Array;
  nextPresentCount: number;
  readonly bytes: Uint8Array;
  readonly record: Uint8Array;
}

export interface SnapshotMirror {
  tick: number;
  serverTimeMs: number;
  lastAckedSeq: number;
  baselineTick: number;
  recordCount: number;
  removedCount: number;
  readonly present: Uint8Array;
  readonly bytes: Uint8Array;
  /** 当前存在实体 id 的升序紧凑列表（前 idCount 项有效；每帧只按增删维护）。 */
  readonly ids: Uint16Array;
  idCount: number;
  readonly record: SnapshotRecord;
}

/** 升序紧凑列表插入（保持升序、去重）；返回新计数。 */
function addIdAscending(list: Uint16Array, count: number, id: number): number {
  let lo = 0;
  let hi = count;
  while (lo < hi) {
    const mid = (lo + hi) >>> 1;
    if ((list[mid] ?? 0) < id) lo = mid + 1;
    else hi = mid;
  }
  if (lo < count && list[lo] === id) return count;
  if (count < list.length) list.copyWithin(lo + 1, lo, count);
  list[lo] = id;
  return count + 1;
}

/** 升序紧凑列表移除（不存在则原样返回）；返回新计数。 */
function removeIdAscending(list: Uint16Array, count: number, id: number): number {
  let lo = 0;
  let hi = count;
  while (lo < hi) {
    const mid = (lo + hi) >>> 1;
    if ((list[mid] ?? 0) < id) lo = mid + 1;
    else hi = mid;
  }
  if (lo >= count || list[lo] !== id) return count;
  list.copyWithin(lo, lo + 1, count);
  return count - 1;
}

/** 仅供测试断言不变量：presentIds[0, presentCount) 是否严格升序。 */
export function baselinePresentIdsSorted(baseline: SnapshotBaseline): boolean {
  for (let i = 1; i < baseline.presentCount; i += 1) {
    if ((baseline.presentIds[i - 1] ?? 0) >= (baseline.presentIds[i] ?? 0)) return false;
  }
  return true;
}

/** 仅供测试断言不变量：mirror.ids[0, idCount) 是否严格升序。 */
export function mirrorIdsSorted(mirror: SnapshotMirror): boolean {
  for (let i = 1; i < mirror.idCount; i += 1) {
    if ((mirror.ids[i - 1] ?? 0) >= (mirror.ids[i] ?? 0)) return false;
  }
  return true;
}

function createRecord(): SnapshotRecord {
  return {
    id: 0,
    kind: 'player',
    flags: 0,
    xCm: 0,
    yCm: 0,
    zCm: 0,
    yawUnits: 0,
    pitchUnits: 0,
    hpRatioUnits: 0,
    state: 0,
  };
}

export function createSnapshotBaseline(): SnapshotBaseline {
  const size = MAX_ENTITIES + 1;
  return {
    tick: 0,
    present: new Uint8Array(size),
    nextPresent: new Uint8Array(size),
    presentIds: new Uint16Array(size),
    presentCount: 0,
    nextPresentIds: new Uint16Array(size),
    nextPresentCount: 0,
    bytes: new Uint8Array(size * SNAPSHOT_RECORD_BYTES),
    record: new Uint8Array(SNAPSHOT_RECORD_BYTES),
  };
}

export function createSnapshotMirror(): SnapshotMirror {
  const size = MAX_ENTITIES + 1;
  return {
    tick: 0,
    serverTimeMs: 0,
    lastAckedSeq: 0,
    baselineTick: 0,
    recordCount: 0,
    removedCount: 0,
    present: new Uint8Array(size),
    bytes: new Uint8Array(size * SNAPSHOT_RECORD_BYTES),
    ids: new Uint16Array(size),
    idCount: 0,
    record: createRecord(),
  };
}

export function quantizeSnapshotEntity(
  entity: SnapshotEntity,
  out: Uint8Array,
  offset = 0,
): number {
  putU16(out, offset, entity.id & 0xffff);
  putU8(out, offset + 2, ENTITY_KIND_CODE[entity.kind] | ((entity.flags & 0x3f) << 2));
  putI16(out, offset + 3, quantizePosition(entity.x));
  putI16(out, offset + 5, quantizePosition(entity.y));
  putI16(out, offset + 7, quantizePosition(entity.z));
  putU16(out, offset + 9, quantizeAngle(entity.yaw));
  putU16(out, offset + 11, quantizeAngle(entity.pitch));
  putU8(out, offset + 13, quantizeRatio(entity.hpRatio));
  putU8(out, offset + 14, clamp(Math.round(entity.state), 0, 255) | 0);
  return SNAPSHOT_RECORD_BYTES;
}

export function readSnapshotRecord(
  bytes: Uint8Array,
  offset: number,
  out: SnapshotRecord,
): SnapshotRecord {
  out.id = u16(bytes, offset);
  const kindFlags = u8(bytes, offset + 2);
  out.kind = ENTITY_KIND_NAMES[kindFlags & 0x03] ?? 'player';
  out.flags = (kindFlags >> 2) & 0x3f;
  out.xCm = i16(bytes, offset + 3);
  out.yCm = i16(bytes, offset + 5);
  out.zCm = i16(bytes, offset + 7);
  out.yawUnits = u16(bytes, offset + 9);
  out.pitchUnits = u16(bytes, offset + 11);
  out.hpRatioUnits = u8(bytes, offset + 13);
  out.state = u8(bytes, offset + 14);
  return out;
}

function copyBytes(
  target: Uint8Array,
  targetOffset: number,
  source: Uint8Array,
  sourceOffset: number,
  length: number,
): void {
  for (let i = 0; i < length; i += 1) target[targetOffset + i] = source[sourceOffset + i] ?? 0;
}

function recordDiffers(bytes: Uint8Array, offset: number, record: Uint8Array): boolean {
  for (let i = 0; i < SNAPSHOT_RECORD_BYTES; i += 1) {
    if ((bytes[offset + i] ?? 0) !== (record[i] ?? 0)) return true;
  }
  return false;
}

export function encodeSnapshot(
  snapshot: Snapshot,
  baseline: SnapshotBaseline,
  lastAckedSeq: number,
  out: Uint8Array,
  forceFull = false,
): number {
  let offset = 0;
  putU8(out, offset, OPCODE.snapshot);
  offset += 1;
  putU32(out, offset, snapshot.tick >>> 0);
  offset += 4;
  putU32(out, offset, snapshot.serverTimeMs >>> 0);
  offset += 4;
  putU16(out, offset, lastAckedSeq & 0xffff);
  offset += 2;
  putU32(out, offset, forceFull ? 0 : baseline.tick >>> 0);
  offset += 4;
  const countOffset = offset;
  offset += 1;

  const entities = snapshot.entities;
  let count = 0;
  for (let i = 0; i < entities.length; i += 1) {
    const entity = entities[i];
    if (entity === undefined) continue;
    const id = entity.id;
    if (id < 1 || id > MAX_ENTITIES) continue;
    const base = id * SNAPSHOT_RECORD_BYTES;
    quantizeSnapshotEntity(entity, baseline.record, 0);
    if (
      !forceFull &&
      baseline.present[id] === 1 &&
      !recordDiffers(baseline.bytes, base, baseline.record)
    ) {
      baseline.nextPresent[id] = 1;
      baseline.nextPresentCount = addIdAscending(
        baseline.nextPresentIds,
        baseline.nextPresentCount,
        id,
      );
      continue;
    }
    if (count >= LIMITS.maxSnapshotRecordsPerFrame) break;
    copyBytes(baseline.bytes, base, baseline.record, 0, SNAPSHOT_RECORD_BYTES);
    copyBytes(out, offset, baseline.record, 0, SNAPSHOT_RECORD_BYTES);
    offset += SNAPSHOT_RECORD_BYTES;
    count += 1;
    baseline.nextPresent[id] = 1;
    baseline.nextPresentCount = addIdAscending(
      baseline.nextPresentIds,
      baseline.nextPresentCount,
      id,
    );
  }

  const removedOffset = offset;
  offset += 1;
  let removed = 0;
  // 只遍历上一帧的紧凑 id 列表（升序）：输出顺序与全扫版本逐字节一致。
  for (let i = 0; i < baseline.presentCount; i += 1) {
    const id = baseline.presentIds[i] ?? 0;
    if (baseline.nextPresent[id] === 1) continue;
    if (removed >= LIMITS.maxRemovedPerFrame) {
      baseline.nextPresent[id] = 1;
      baseline.nextPresentCount = addIdAscending(
        baseline.nextPresentIds,
        baseline.nextPresentCount,
        id,
      );
      continue;
    }
    putU16(out, offset, id);
    offset += 2;
    removed += 1;
  }

  putU8(out, countOffset, count);
  putU8(out, removedOffset, removed);
  baseline.present.set(baseline.nextPresent);
  baseline.nextPresent.fill(0);
  const nextCount = baseline.nextPresentCount;
  for (let i = 0; i < nextCount; i += 1) {
    baseline.presentIds[i] = baseline.nextPresentIds[i] ?? 0;
  }
  baseline.presentCount = nextCount;
  baseline.nextPresentCount = 0;
  baseline.tick = snapshot.tick >>> 0;
  return offset;
}

export function decodeSnapshot(
  frame: Uint8Array,
  mirror: SnapshotMirror,
): DecodeResult<SnapshotMirror> {
  if (u8(frame, 0) !== OPCODE.snapshot) return fail('bad-opcode');
  if (frame.length < SNAPSHOT_HEADER_BYTES) return fail('truncated');
  let offset = 1;
  const tick = u32(frame, offset);
  offset += 4;
  const serverTimeMs = u32(frame, offset);
  offset += 4;
  const lastAckedSeq = u16(frame, offset);
  offset += 2;
  const baselineTick = u32(frame, offset);
  offset += 4;
  const count = u8(frame, offset);
  offset += 1;
  if (frame.length < offset + count * SNAPSHOT_RECORD_BYTES + 1) return fail('truncated');
  if (mirror.tick !== 0 && tick < mirror.tick) return ok(mirror);
  if (baselineTick === 0) {
    mirror.present.fill(0);
    mirror.idCount = 0;
  }
  for (let i = 0; i < count; i += 1) {
    const recordOffset = offset;
    readSnapshotRecord(frame, recordOffset, mirror.record);
    offset += SNAPSHOT_RECORD_BYTES;
    const id = mirror.record.id;
    if (id < 1 || id > MAX_ENTITIES) return fail('bad-value');
    copyBytes(mirror.bytes, id * SNAPSHOT_RECORD_BYTES, frame, recordOffset, SNAPSHOT_RECORD_BYTES);
    if (mirror.present[id] !== 1) {
      mirror.present[id] = 1;
      mirror.idCount = addIdAscending(mirror.ids, mirror.idCount, id);
    }
  }
  const removedCount = u8(frame, offset);
  offset += 1;
  if (frame.length !== offset + removedCount * 2) return fail('bad-length');
  for (let i = 0; i < removedCount; i += 1) {
    const id = u16(frame, offset);
    offset += 2;
    if (id < 1 || id > MAX_ENTITIES) return fail('bad-value');
    if (mirror.present[id] === 1) {
      mirror.present[id] = 0;
      mirror.idCount = removeIdAscending(mirror.ids, mirror.idCount, id);
    }
  }
  mirror.tick = tick;
  mirror.serverTimeMs = serverTimeMs;
  mirror.lastAckedSeq = lastAckedSeq;
  mirror.baselineTick = baselineTick;
  mirror.recordCount = count;
  mirror.removedCount = removedCount;
  return ok(mirror);
}

export const EVENT_FRAME_HEADER_BYTES = 6;

export function encodeEventPayload(event: SimEvent, out: Uint8Array, offset: number): number {
  switch (event.type) {
    case 'playerHit':
      putU8(out, offset, EVENT_TYPE.playerHit);
      putU16(out, offset + 1, event.subjectId & 0xffff);
      putU16(out, offset + 3, event.targetId & 0xffff);
      putU16(out, offset + 5, clamp(Math.round(event.value), 0, 0xffff) | 0);
      putU8(out, offset + 7, event.flags & 0xff);
      return 8;
    case 'sheepKilled':
      putU8(out, offset, EVENT_TYPE.sheepKilled);
      putU16(out, offset + 1, event.targetId & 0xffff);
      putU16(out, offset + 3, event.subjectId & 0xffff);
      putU8(out, offset + 5, clamp(Math.round(event.value), 0, 0xff) | 0);
      return 6;
    case 'waveStart':
      putU8(out, offset, EVENT_TYPE.waveStart);
      putU8(out, offset + 1, clamp(Math.round(event.value), 0, 0xff) | 0);
      putU16(out, offset + 2, clamp(Math.round(event.flags), 0, 0xffff) | 0);
      return 4;
    case 'waveClear':
      putU8(out, offset, EVENT_TYPE.waveClear);
      putU8(out, offset + 1, clamp(Math.round(event.value), 0, 0xff) | 0);
      putU32(out, offset + 2, clamp(Math.round(event.flags), 0, 0xffffffff) >>> 0);
      return 6;
    case 'playerDowned':
      putU8(out, offset, EVENT_TYPE.playerDowned);
      putU16(out, offset + 1, event.subjectId & 0xffff);
      return 3;
    case 'reviveProgress':
      putU8(out, offset, EVENT_TYPE.reviveProgress);
      putU16(out, offset + 1, event.targetId & 0xffff);
      putU16(out, offset + 3, event.subjectId & 0xffff);
      putU16(out, offset + 5, clamp(Math.round(event.value), 0, 0xffff) | 0);
      return 7;
    case 'reviveDone':
      putU8(out, offset, EVENT_TYPE.reviveDone);
      putU16(out, offset + 1, event.targetId & 0xffff);
      putU16(out, offset + 3, event.subjectId & 0xffff);
      return 5;
    case 'rageActivated':
      putU8(out, offset, EVENT_TYPE.rageActivated);
      putU16(out, offset + 1, event.subjectId & 0xffff);
      putU16(out, offset + 3, clamp(Math.round(event.value), 0, 0xffff) | 0);
      return 5;
    case 'matchEnded':
      putU8(out, offset, EVENT_TYPE.matchEnded);
      putU8(out, offset + 1, clamp(Math.round(event.subjectId), 0, 0xff) | 0);
      putU8(out, offset + 2, clamp(Math.round(event.value), 0, 0xff) | 0);
      putU32(out, offset + 3, clamp(Math.round(event.flags), 0, 0xffffffff) >>> 0);
      return 7;
    default:
      return 0;
  }
}

export function encodeEventFrame(
  tick: number,
  events: readonly SimEvent[],
  out: Uint8Array,
): number {
  putU8(out, 0, OPCODE.event);
  putU32(out, 1, tick >>> 0);
  let offset = EVENT_FRAME_HEADER_BYTES;
  let count = 0;
  for (let i = 0; i < events.length; i += 1) {
    const event = events[i];
    if (event === undefined || count >= U8_MAX) continue;
    const written = encodeEventPayload(event, out, offset);
    if (written === 0) continue;
    if (offset + written > LIMITS.maxFrameBytes) break;
    offset += written;
    count += 1;
  }
  putU8(out, 5, count);
  return offset;
}

export function decodeEventFrame(frame: Uint8Array, out: SimEvent[]): DecodeResult<SimEvent[]> {
  if (u8(frame, 0) !== OPCODE.event) return fail('bad-opcode');
  if (frame.length < EVENT_FRAME_HEADER_BYTES) return fail('truncated');
  const tick = u32(frame, 1);
  const count = u8(frame, 5);
  let offset = EVENT_FRAME_HEADER_BYTES;
  out.length = 0;
  for (let i = 0; i < count; i += 1) {
    if (offset >= frame.length) return fail('truncated');
    const type = u8(frame, offset);
    const event = createSimEvent();
    event.tick = tick;
    switch (type) {
      case EVENT_TYPE.playerHit:
        if (offset + 8 > frame.length) return fail('truncated');
        event.type = 'playerHit';
        event.subjectId = u16(frame, offset + 1);
        event.targetId = u16(frame, offset + 3);
        event.value = u16(frame, offset + 5);
        event.flags = u8(frame, offset + 7);
        offset += 8;
        break;
      case EVENT_TYPE.sheepKilled:
        if (offset + 6 > frame.length) return fail('truncated');
        event.type = 'sheepKilled';
        event.targetId = u16(frame, offset + 1);
        event.subjectId = u16(frame, offset + 3);
        event.value = u8(frame, offset + 5);
        offset += 6;
        break;
      case EVENT_TYPE.waveStart:
        if (offset + 4 > frame.length) return fail('truncated');
        event.type = 'waveStart';
        event.value = u8(frame, offset + 1);
        event.flags = u16(frame, offset + 2);
        offset += 4;
        break;
      case EVENT_TYPE.waveClear:
        if (offset + 6 > frame.length) return fail('truncated');
        event.type = 'waveClear';
        event.value = u8(frame, offset + 1);
        event.flags = u32(frame, offset + 2);
        offset += 6;
        break;
      case EVENT_TYPE.playerDowned:
        if (offset + 3 > frame.length) return fail('truncated');
        event.type = 'playerDowned';
        event.subjectId = u16(frame, offset + 1);
        offset += 3;
        break;
      case EVENT_TYPE.reviveProgress:
        if (offset + 7 > frame.length) return fail('truncated');
        event.type = 'reviveProgress';
        event.targetId = u16(frame, offset + 1);
        event.subjectId = u16(frame, offset + 3);
        event.value = u16(frame, offset + 5);
        offset += 7;
        break;
      case EVENT_TYPE.reviveDone:
        if (offset + 5 > frame.length) return fail('truncated');
        event.type = 'reviveDone';
        event.targetId = u16(frame, offset + 1);
        event.subjectId = u16(frame, offset + 3);
        offset += 5;
        break;
      case EVENT_TYPE.rageActivated:
        if (offset + 5 > frame.length) return fail('truncated');
        event.type = 'rageActivated';
        event.subjectId = u16(frame, offset + 1);
        event.value = u16(frame, offset + 3);
        offset += 5;
        break;
      case EVENT_TYPE.matchEnded:
        if (offset + 7 > frame.length) return fail('truncated');
        event.type = 'matchEnded';
        event.subjectId = u8(frame, offset + 1);
        event.value = u8(frame, offset + 2);
        event.flags = u32(frame, offset + 3);
        offset += 7;
        break;
      default:
        return fail('unknown-event');
    }
    out.push(event);
  }
  if (offset !== frame.length) return fail('bad-length');
  return ok(out);
}

export function encodeMatchState(state: MatchState, out: Uint8Array): number {
  putU8(out, 0, OPCODE.matchState);
  putU8(out, 1, state.phase & 0xff);
  putU8(out, 2, state.wave & 0xff);
  putU16(out, 3, state.intermissionMs & 0xffff);
  let offset = 6;
  let count = 0;
  for (let i = 0; i < state.players.length; i += 1) {
    const player = state.players[i];
    if (player === undefined || count >= LIMITS.maxPlayersPerRoom) continue;
    putU16(out, offset, player.pid & 0xffff);
    const nameLength = writeUtf8(out, offset + 3, player.name, LIMITS.maxNameBytes);
    putU8(out, offset + 2, nameLength);
    offset += 3 + nameLength;
    putU8(out, offset, player.ready ? 1 : 0);
    putU8(out, offset + 1, player.weapon & 0xff);
    putU8(out, offset + 2, quantizeRatio(player.hpRatio));
    putU16(out, offset + 3, clamp(Math.round(player.kills), 0, 0xffff) | 0);
    putU8(out, offset + 5, clamp(Math.round(player.mag), 0, 255) | 0);
    putU16(out, offset + 6, clamp(Math.round(player.reserve), 0, 0xffff) | 0);
    putU8(out, offset + 8, clamp(Math.round(player.reloadLeft10Ms), 0, 255) | 0);
    putU8(out, offset + 9, clamp(Math.round(player.rage), 0, 255) | 0);
    putU8(out, offset + 10, clamp(Math.round(player.rageLeft100Ms), 0, 255) | 0);
    putU8(out, offset + 11, player.downed ? 1 : 0);
    putU8(out, offset + 12, clamp(Math.round(player.reviveRatio255), 0, 255) | 0);
    offset += 13;
    count += 1;
  }
  putU8(out, 5, count);
  return offset;
}

export function decodeMatchState(frame: Uint8Array, out?: MatchState): DecodeResult<MatchState> {
  if (u8(frame, 0) !== OPCODE.matchState) return fail('bad-opcode');
  if (frame.length < 6) return fail('truncated');
  const state: MatchState = out ?? {
    phase: 0,
    wave: 0,
    intermissionMs: 0,
    hostId: 0,
    players: [],
  };
  state.phase = u8(frame, 1);
  state.wave = u8(frame, 2);
  state.intermissionMs = u16(frame, 3);
  const count = u8(frame, 5);
  if (count > LIMITS.maxPlayersPerRoom) return fail('bad-value');
  state.players.length = 0;
  let offset = 6;
  for (let i = 0; i < count; i += 1) {
    if (offset + 3 > frame.length) return fail('truncated');
    const pid = u16(frame, offset);
    const nameLength = u8(frame, offset + 2);
    if (nameLength > LIMITS.maxNameBytes) return fail('bad-value');
    if (offset + 3 + nameLength + 13 > frame.length) return fail('truncated');
    const name = readUtf8(frame, offset + 3, nameLength);
    offset += 3 + nameLength;
    const ready = u8(frame, offset) !== 0;
    const weapon = u8(frame, offset + 1);
    const hpRatio = dequantizeRatio(u8(frame, offset + 2));
    const kills = u16(frame, offset + 3);
    const mag = u8(frame, offset + 5);
    const reserve = u16(frame, offset + 6);
    const reloadLeft10Ms = u8(frame, offset + 8);
    const rage = u8(frame, offset + 9);
    const rageLeft100Ms = u8(frame, offset + 10);
    const downed = u8(frame, offset + 11) !== 0;
    const reviveRatio255 = u8(frame, offset + 12);
    offset += 13;
    state.players.push({
      pid,
      name,
      ready,
      weapon,
      hpRatio,
      kills,
      mag,
      reserve,
      reloadLeft10Ms,
      rage,
      rageLeft100Ms,
      downed,
      reviveRatio255,
    });
  }
  if (offset !== frame.length) return fail('bad-length');
  return ok(state);
}
