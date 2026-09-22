import { describe, expect, it } from 'vitest';

import { createLocalTimers } from './localTimers.ts';

describe('O07 本地倒计时（每帧推进 + 权威重同步）', () => {
  it('①推进 16ms × 60 → 剩余减少 960ms', () => {
    const timers = createLocalTimers();
    timers.resyncReload(100, 2000);
    expect(timers.advance(0).reloadLeftMs).toBe(1000);
    for (let i = 0; i < 60; i += 1) timers.advance(16);
    expect(timers.advance(0).reloadLeftMs).toBe(40);
  });

  it('②权威值更紧急 → 立即采用', () => {
    const timers = createLocalTimers();
    timers.resyncReload(100, 2000);
    timers.advance(900);
    expect(timers.advance(0).reloadLeftMs).toBe(100);
    timers.resyncReload(10, 2000);
    expect(timers.advance(0).reloadLeftMs).toBe(100);
  });

  it('③权威值倒退（迟到帧）→ 不增加本地剩余', () => {
    const timers = createLocalTimers();
    timers.resyncReload(100, 2000);
    timers.advance(900);
    timers.resyncReload(150, 2000);
    expect(timers.advance(0).reloadLeftMs).toBe(100);
  });

  it('④下限 0，不出现负值；归零后可采用新一轮权威值', () => {
    const timers = createLocalTimers();
    timers.resyncReload(10, 2000);
    timers.advance(5000);
    expect(timers.advance(0).reloadLeftMs).toBe(0);
    timers.advance(500);
    expect(timers.advance(0).reloadLeftMs).toBe(0);
    timers.resyncReload(200, 2000);
    expect(timers.advance(0).reloadLeftMs).toBe(2000);
    timers.resyncReload(30, 2000);
    expect(timers.advance(0).reloadLeftMs).toBe(300 + 50); // 权威 + 单帧容差
  });

  it('⑤狂暴/换弹独立推进与重同步（含单帧容差）', () => {
    const timers = createLocalTimers();
    timers.resyncReload(100, 2000);
    timers.resyncRage(80);
    timers.advance(250);
    const view = timers.advance(0);
    expect(view.reloadLeftMs).toBe(750);
    expect(view.rageLeftMs).toBe(7750);
    timers.resyncRage(70);
    expect(timers.advance(0).rageLeftMs).toBe(7050);
    expect(timers.advance(0).reloadLeftMs).toBe(750);
    timers.resyncRage(0);
    expect(timers.advance(0).rageLeftMs).toBe(0);
    timers.advance(1000);
    expect(timers.advance(0).reloadLeftMs).toBe(0);
  });
});
