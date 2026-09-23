import {
  CONFIG,
  SERVER_TICK_MS,
  createCommand,
  stepLocalPlayer,
  type Command,
  type MoveState,
} from '@ac/shared';
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

describe('渲染节拍（60fps 渲染 + 30Hz 采样 + 20Hz 和解）', () => {
  // 复刻 main.ts 的逐帧循环：采样 → 预测子步 → 渲染位置 → 误差平滑 → 20Hz 和解。
  // ackLag = 客户端命令被确认的滞后条数（0 = 本机对局，1 = 100ms 下行延迟）。
  function cadence(ackLag: number): {
    frozen: number;
    spikes: number;
    ahead: number;
    hard: number;
  } {
    const predictor = createPredictor(moveConfig);
    const buffer = createCommandBuffer();
    const smoother = createErrorSmoother();
    const reconciler = createReconciler();
    const authority = createLocalAuthority();
    // 服务器侧独立状态：与预测器同用固定 50ms 子步
    const server: MoveState = {
      pos: { x: 0, y: 0, z: 20 },
      vel: { x: 0, y: 0, z: 0 },
      yaw: 0,
      pitch: 0,
    };

    const dtMs = 1000 / 60;
    const expected = CONFIG.player.moveSpeed * (dtMs / 1000);
    predictor.setAuthoritative(0, 0, 20, 0, 0);

    const render = { x: 0, y: 0, z: 0 };
    let command = moveCommand(0, 1);
    let seq = 0;
    let presented = 0;
    let frozen = 0;
    let spikes = 0;
    for (let frame = 0; frame < 120; frame += 1) {
      if (frame % 2 === 0) {
        // 30Hz 采样：与 main.ts 的采样器同频，命令数多于 tick 数
        seq += 1;
        command = moveCommand(seq, 1);
        buffer.push(command);
      }
      predictor.advance(dtMs, command);
      predictor.renderPosition(render);
      smoother.decay(dtMs);
      const next = render.z + smoother.z; // main.ts：相机 = 预测位置 + 误差平滑
      const delta = frame > 0 ? next - presented : expected;
      presented = next;
      if (frame > 0) {
        if (delta < expected * 0.5) frozen += 1;
        if (delta > expected * 1.5) spikes += 1;
      }

      if (frame % 3 === 2) {
        stepLocalPlayer(server, command, moveConfig, SERVER_TICK_MS);
        authority.found = true;
        authority.x = server.pos.x;
        authority.y = server.pos.y;
        authority.z = server.pos.z;
        authority.yaw = 0;
        authority.pitch = 0;
        authority.lastAckedSeq = Math.max(0, command.seq - ackLag);
        reconciler.reconcile(authority, buffer, predictor, smoother);
      }
    }
    return {
      frozen,
      spikes,
      ahead: predictor.state.pos.z - server.pos.z,
      hard: reconciler.hardCorrectCount,
    };
  }

  it('本机对局（零延迟确认）逐帧推进：没有停住的帧，也没有 20Hz 的整段跳变', () => {
    // 旧实现：setAuthoritative 清零累加器与速度 ⇒ 每次和解后的两帧渲染位置原地不动，
    // 相机每 3 帧才挪一次（真人试玩反馈的"人物移动画面卡顿"）。
    const stats = cadence(0);
    expect(stats.frozen).toBe(0);
    expect(stats.spikes).toBe(0);
    expect(stats.hard).toBe(0);
    // 零延迟：确认到最新命令 ⇒ 预测与权威同节拍，不应漂移
    expect(Math.abs(stats.ahead)).toBeLessThan(0.05);
  });

  it('100ms 下行延迟（每次和解回滚重放）同样逐帧推进', () => {
    const stats = cadence(1);
    expect(stats.frozen).toBe(0);
    expect(stats.spikes).toBe(0);
    expect(stats.hard).toBe(0);
    expect(stats.ahead).toBeGreaterThan(0);
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
