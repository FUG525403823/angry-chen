import { BUTTON, createCommand, type Command } from '@ac/shared';
import { describe, expect, it } from 'vitest';

import { COMMAND_INTERVAL_MS, createInputSampler } from './sampler.ts';

function createHarness(startSeq?: number) {
  const emitted: Command[] = [];
  let clockMs = 0;
  const command = createCommand();
  if (startSeq !== undefined) {
    // 采样器内部从 0 开始递增，这里通过先发若干帧把 seq 推到目标值
  }
  const sampler = createInputSampler({
    now: () => clockMs,
    emit: (draft: Command) => emitted.push({ ...draft }),
    command,
    getTick: () => 42,
  });
  if (startSeq !== undefined) {
    for (let i = 0; i < startSeq; i += 1) sampler.flush();
    emitted.length = 0;
  }
  return {
    sampler,
    emitted,
    advance(ms: number): void {
      clockMs += ms;
    },
  };
}

describe('输入采样器', () => {
  it('首次 update 立即发送，随后按 30Hz 节奏补发', () => {
    const h = createHarness();
    expect(h.sampler.update()).toBe(true);
    expect(h.emitted.length).toBe(1);
    h.advance(COMMAND_INTERVAL_MS - 1);
    expect(h.sampler.update()).toBe(false);
    expect(h.emitted.length).toBe(1);
    h.advance(1);
    expect(h.sampler.update()).toBe(true);
    expect(h.emitted.length).toBe(2);
    expect(h.sampler.update()).toBe(false);
    expect(h.emitted[1]?.tick).toBe(42);
  });

  it('按键按下与抬起都立即补发一帧', () => {
    const h = createHarness();
    expect(h.sampler.setKey('KeyW', true)).toBe(true);
    expect(h.emitted.length).toBe(1);
    expect(h.emitted[0]?.moveX).toBe(1);
    expect(h.sampler.setKey('KeyW', false)).toBe(true);
    expect(h.emitted.length).toBe(2);
    expect(h.emitted[1]?.moveX).toBe(0);
    expect(h.sampler.setKey('KeyZ', true)).toBe(false);
    expect(h.emitted.length).toBe(2);
  });

  it('WASD 组合出前后与左右轴，S/A 为负向', () => {
    const h = createHarness();
    h.sampler.setKey('KeyW', true);
    h.sampler.setKey('KeyS', true);
    expect(h.sampler.state.moveX).toBe(0);
    h.sampler.setKey('KeyS', false);
    h.sampler.setKey('KeyD', true);
    expect(h.sampler.state.moveX).toBe(1);
    expect(h.sampler.state.moveY).toBe(1);
    h.sampler.setKey('KeyD', false);
    h.sampler.setKey('KeyA', true);
    expect(h.sampler.state.moveY).toBe(-1);
  });

  it('Shift/Space 等按键写入对应的位标志', () => {
    const h = createHarness();
    h.sampler.setKey('ShiftLeft', true);
    expect(h.sampler.state.buttons & BUTTON.sprint).toBe(BUTTON.sprint);
    h.sampler.setKey('Space', true);
    expect(h.sampler.state.buttons & BUTTON.jump).toBe(BUTTON.jump);
    h.sampler.setKey('ShiftLeft', false);
    expect(h.sampler.state.buttons & BUTTON.sprint).toBe(0);
    expect(h.sampler.state.buttons & BUTTON.jump).toBe(BUTTON.jump);
  });

  it('鼠标位移按 0.001 × 灵敏度换算，俯仰被夹在 ±π/2', () => {
    const h = createHarness();
    h.sampler.setSensitivity(1);
    h.sampler.addMouse(10, 0);
    expect(h.sampler.state.yaw).toBeCloseTo(0.01, 6);
    h.sampler.addMouse(0, 100000);
    expect(h.sampler.state.pitch).toBeCloseTo(-Math.PI / 2, 6);
    h.sampler.addMouse(0, -100000);
    expect(h.sampler.state.pitch).toBeCloseTo(Math.PI / 2, 6);
    h.sampler.setSensitivity(2);
    h.sampler.setYaw(0);
    h.sampler.addMouse(10, 0);
    expect(h.sampler.state.yaw).toBeCloseTo(0.02, 6);
  });

  it('seq 单调递增并回绕到 uint16 范围', () => {
    const h = createHarness(0xffff);
    h.sampler.flush();
    expect(h.emitted[0]?.seq).toBe(0xffff);
    h.sampler.flush();
    expect(h.emitted[1]?.seq).toBe(0);
    expect(h.sampler.getSeq()).toBe(0x10001);
  });
});
