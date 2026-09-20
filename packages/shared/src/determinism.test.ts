import { describe, expect, it } from 'vitest';

import { createCommand, type Command } from './command.ts';
import { createRng } from './rng.ts';
import { stepWorld } from './sim.ts';
import { createSnapshot, snapshotWorld, type Snapshot } from './snapshot.ts';
import { createWorld } from './world.ts';

const TICKS = 1000;
const PLAYER_SLOTS = 4;
const COMMAND_POOL_SIZE = 600;

class ByteStream {
  private readonly buffer = new Uint8Array(1 << 21);
  private readonly view = new DataView(this.buffer.buffer);
  length = 0;

  private ensure(extra: number): void {
    if (this.length + extra > this.buffer.length) throw new Error('byte stream overflow');
  }

  u8(value: number): void {
    this.ensure(1);
    this.view.setUint8(this.length, value & 0xff);
    this.length += 1;
  }

  u16(value: number): void {
    this.ensure(2);
    this.view.setUint16(this.length, value & 0xffff, true);
    this.length += 2;
  }

  i32(value: number): void {
    this.ensure(4);
    this.view.setInt32(this.length, value | 0, true);
    this.length += 4;
  }

  f64(value: number): void {
    this.ensure(8);
    this.view.setFloat64(this.length, value, true);
    this.length += 8;
  }

  toBytes(): Uint8Array {
    return this.buffer.slice(0, this.length);
  }
}

function kindCode(kind: Snapshot['entities'][number]['kind']): number {
  if (kind === 'player') return 1;
  if (kind === 'sheep') return 2;
  if (kind === 'projectile') return 3;
  return 4;
}

function encodeSnapshot(stream: ByteStream, snapshot: Snapshot): void {
  stream.i32(snapshot.tick);
  stream.f64(snapshot.serverTimeMs);
  stream.u8(snapshot.truncated ? 1 : 0);
  stream.u16(snapshot.entities.length);
  for (let i = 0; i < snapshot.entities.length; i += 1) {
    const entity = snapshot.entities[i];
    if (entity === undefined) continue;
    stream.u16(entity.id);
    stream.u8(kindCode(entity.kind));
    stream.u8(entity.flags);
    stream.f64(entity.x);
    stream.f64(entity.y);
    stream.f64(entity.z);
    stream.f64(entity.yaw);
    stream.f64(entity.pitch);
    stream.f64(entity.hpRatio);
    stream.u8(entity.state);
  }
}

function buildCommandPool(seed: number): Command[] {
  const rng = createRng(seed, 'ai');
  const pool: Command[] = [];
  for (let i = 0; i < COMMAND_POOL_SIZE; i += 1) {
    const command = createCommand();
    command.seq = i % 65536;
    command.tick = i;
    command.moveX = rng() * 2 - 1;
    command.moveY = rng() * 2 - 1;
    command.yaw = (rng() * 2 - 1) * Math.PI;
    command.pitch = (rng() * 2 - 1) * 0.6;
    command.buttons = rng() < 0.25 ? 2 : 0;
    command.switchTo = rng() < 0.34 ? 0 : 1;
    pool.push(command);
  }
  return pool;
}

function runReplay(seed: number): Uint8Array {
  const world = createWorld(seed);
  const pool = buildCommandPool(seed);
  const out = createSnapshot();
  const stream = new ByteStream();
  const batch: Command[] = [];

  for (let tick = 0; tick < TICKS; tick += 1) {
    batch.length = 0;
    for (let slot = 0; slot < PLAYER_SLOTS; slot += 1) {
      const command = pool[(tick * PLAYER_SLOTS + slot) % COMMAND_POOL_SIZE];
      if (command === undefined) throw new Error('command pool is empty');
      batch.push(command);
    }
    stepWorld(world, batch, 50);
    encodeSnapshot(stream, snapshotWorld(world, out));
  }

  return stream.toBytes();
}

function firstMismatch(a: Uint8Array, b: Uint8Array): number {
  if (a.length !== b.length) return -2;
  for (let i = 0; i < a.length; i += 1) {
    if (a[i] !== b[i]) return i;
  }
  return -1;
}

describe('确定性', () => {
  it('同种子 + 同 600 条命令序列跑 1000 tick：快照字节序列逐字节一致', () => {
    const first = runReplay(2024);
    const second = runReplay(2024);

    expect(first.length).toBeGreaterThan(1000);
    expect(second.length).toBe(first.length);
    expect(firstMismatch(first, second)).toBe(-1);
  });

  it('不同种子产生不同轨迹（证明编码器没有空转）', () => {
    expect(firstMismatch(runReplay(1), runReplay(2))).not.toBe(-1);
    expect(runReplay(1).length).toBe(runReplay(2).length);
  });

  it('1000 tick 后 tick 与 timeMs 精确等于 1000 / 50000', () => {
    const world = createWorld(2024);
    for (let i = 0; i < TICKS; i += 1) stepWorld(world, [], 50);
    expect(world.tick).toBe(TICKS);
    expect(world.timeMs).toBe(TICKS * 50);
    expect(world.liveCount).toBe(4);
  });
});
