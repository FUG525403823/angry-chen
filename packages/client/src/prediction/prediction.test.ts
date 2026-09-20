import { CONFIG, createCommand, type Command } from '@ac/shared';
import { describe, expect, it } from 'vitest';
import { createErrorSmoother, SMOOTHING_EPSILON_M } from '../render/errorSmoother.ts';
import { createCommandBuffer, seqDiff } from './commandBuffer.ts';
import { createPredictor } from './predictor.ts';
import { HARD_CORRECT_THRESHOLD_M, createLocalAuthority, createReconciler } from './reconciler.ts';

const moveConfig = {
  player: CONFIG.player,
  arena: CONFIG.arena,
  radius: CONFIG.entity.radiusByKind.player,
};

function moveCommand(seq: number, moveX: number, yaw = 0): Command {
  const command = createCommand();
  command.seq = seq;
  command.tick = seq;
  command.moveX = moveX;
  command.yaw = yaw;
  return command;
}

describe('命令缓冲', () => {
  it('seq 回绕安全比较', () => {
    expect(seqDiff(0, 65535)).toBe(1);
    expect(seqDiff(65535, 0)).toBe(-1);
    expect(seqDiff(10, 10)).toBe(0);
  });

  it('按 seq 确认、保持顺序、溢出丢最旧', () => {
    const buffer = createCommandBuffer(2);
    buffer.push(moveCommand(1, 1));
    buffer.push(moveCommand(2, 1));
    expect(buffer.size).toBe(2);
    buffer.push(moveCommand(3, 1));
    expect(buffer.size).toBe(2);
    expect(buffer.overflowCount).toBe(1);
    expect(buffer.at(0)?.seq).toBe(2);

    expect(buffer.ackUpTo(2)).toBe(1);
    expect(buffer.size).toBe(1);
    expect(buffer.at(0)?.seq).toBe(3);
    expect(buffer.ackUpTo(3)).toBe(1);
    expect(buffer.size).toBe(0);
    expect(buffer.ackUpTo(1)).toBe(0);
  });
});

describe('预测器子步', () => {
  it('按固定 50ms 子步推进，单帧最多 5 步', () => {
    const predictor = createPredictor(moveConfig);
    predictor.setAuthoritative(0, 0, 20, 0, 0);
    const command = moveCommand(1, 1);
    expect(predictor.advance(150, command)).toBe(3);
    expect(predictor.state.pos.z - 20).toBeCloseTo(CONFIG.player.moveSpeed * 0.15, 6);
    expect(predictor.advance(1000, command)).toBe(5);
    expect(predictor.pendingMs).toBeLessThanOrEqual(50);
  });

  it('渲染位置在子步内线性外推且不写回状态', () => {
    const predictor = createPredictor(moveConfig);
    predictor.setAuthoritative(0, 0, 20, 0, 0);
    predictor.advance(75, moveCommand(1, 1));
    const before = predictor.state.pos.z;
    const out = predictor.renderPosition({ x: 0, y: 0, z: 0 });
    expect(out.z).toBeGreaterThan(before);
    expect(predictor.state.pos.z).toBe(before);
  });
});

describe('和解', () => {
  it('±1cm 量化误差内收敛，缓冲被清空', () => {
    const predictor = createPredictor(moveConfig);
    const buffer = createCommandBuffer();
    const smoother = createErrorSmoother();
    const reconciler = createReconciler();
    const authority = createLocalAuthority();

    predictor.setAuthoritative(0, 0, 20, 0, 0);
    const commands = [moveCommand(1, 1), moveCommand(2, 1), moveCommand(3, 1)];
    for (const command of commands) {
      buffer.push(command);
      predictor.stepWith(command);
    }
    const truth = predictor.state.pos.z;

    authority.found = true;
    authority.x = 0;
    authority.y = 0;
    authority.z = truth - 0.008;
    authority.yaw = 0;
    authority.pitch = 0;
    authority.lastAckedSeq = 3;

    reconciler.reconcile(authority, buffer, predictor, smoother);
    expect(buffer.size).toBe(0);
    expect(reconciler.lastErrorM).toBeLessThanOrEqual(0.01);
    expect(predictor.state.pos.z).toBeCloseTo(truth - 0.008, 6);
    expect(reconciler.hardCorrectCount).toBe(0);
  });

  it('0.5m 误差被吸收成视觉偏移，1.5m 才硬纠正', () => {
    const predictor = createPredictor(moveConfig);
    const buffer = createCommandBuffer();
    const smoother = createErrorSmoother();
    const reconciler = createReconciler();
    const authority = createLocalAuthority();

    predictor.setAuthoritative(0, 0, 10, 0, 0);
    authority.found = true;
    authority.x = 0;
    authority.y = 0;
    authority.z = 10.5;
    authority.lastAckedSeq = 0;
    reconciler.reconcile(authority, buffer, predictor, smoother);

    expect(reconciler.hardCorrectCount).toBe(0);
    expect(smoother.z).toBeCloseTo(-0.5, 6);
    expect(predictor.state.pos.z + smoother.z).toBeCloseTo(10, 6);

    authority.z = 11.6;
    reconciler.reconcile(authority, buffer, predictor, smoother);
    expect(reconciler.hardCorrectCount).toBe(1);
    expect(smoother.magnitude).toBe(0);
    expect(reconciler.lastErrorM).toBeCloseTo(1.1, 6);
    expect(reconciler.maxErrorM).toBeCloseTo(1.1, 6);
    expect(reconciler.maxErrorM).toBeGreaterThan(HARD_CORRECT_THRESHOLD_M);
  });
});

describe('误差平滑器', () => {
  it('按 exp(-dt/0.12) 衰减并在阈值下归零', () => {
    const smoother = createErrorSmoother();
    smoother.add(0.5, 0, 0);
    smoother.decay(120);
    expect(smoother.x).toBeCloseTo(0.5 * Math.exp(-1), 6);
    for (let i = 0; i < 200; i += 1) smoother.decay(16.7);
    expect(smoother.magnitude).toBeLessThan(SMOOTHING_EPSILON_M);
    expect([smoother.x, smoother.y, smoother.z]).toEqual([0, 0, 0]);
  });

  it('只影响渲染偏移，不触碰模拟状态', () => {
    const predictor = createPredictor(moveConfig);
    const smoother = createErrorSmoother();
    const buffer = createCommandBuffer();
    const reconciler = createReconciler();
    predictor.setAuthoritative(3, 0, 4, 0.5, 0.1);
    smoother.add(0.4, 0.1, -0.2);
    const snapshot = {
      x: predictor.state.pos.x,
      y: predictor.state.pos.y,
      z: predictor.state.pos.z,
      yaw: predictor.state.yaw,
    };
    smoother.decay(50);
    const authority = createLocalAuthority();
    authority.found = true;
    authority.x = snapshot.x + 0.5;
    authority.y = snapshot.y;
    authority.z = snapshot.z;
    authority.yaw = snapshot.yaw;
    authority.lastAckedSeq = 0;
    reconciler.reconcile(authority, buffer, predictor, smoother);
    smoother.decay(1000);

    expect(predictor.state.pos.x).toBe(snapshot.x + 0.5);
    expect(predictor.state.pos.z).toBe(snapshot.z);
    expect(predictor.state.yaw).toBe(snapshot.yaw);
  });
});
