import { describe, expect, it } from 'vitest';

import {
  DEBUG_REFRESH_MS,
  DRAW_CALL_BUDGET,
  FRAME_TIME_BUDGET_MS,
  MATERIAL_BUDGET,
  PARTICLE_BUDGET,
  TRIANGLE_BUDGET,
  createDebugPanel,
  debugLines,
  withinBudget,
  type DebugSample,
} from './debugPanel.ts';

type MutableSample = { -readonly [K in keyof DebugSample]: DebugSample[K] };

function sample(): MutableSample {
  return {
    fps: 60,
    p95IntervalMs: 12.4,
    p95WorkMs: 4.2,
    tick: 1200,
    snapshotsPerSec: 20,
    inboundBytesPerSec: 4096,
    rttMs: 32,
    serverRateX10: 200,
    liveEntities: 40,
    pooledEntities: 64,
    localPos: { x: 1, y: 2, z: -3 },
    status: 'open',
    roomCode: 'ABCD',
    versionLine: 'build 0.0.0',
    predictionErrorM: 0.02,
    predictionMaxErrorM: 0.4,
    hardCorrects: 1,
    pendingCommands: 2,
    drawCalls: 118,
    triangles: 150000,
    particles: 240,
    materialCount: 18,
  };
}

describe('调试面板性能读数（P09 §4 / §6）', () => {
  it('输出 draw call、三角面、粒子数与帧时间 P95 及其预算', () => {
    const lines = debugLines(sample()).join('\n');
    expect(lines).toContain('p95 12.4/' + String(FRAME_TIME_BUDGET_MS) + 'ms');
    expect(lines).toContain(
      'draw ' + String(DRAW_CALL_BUDGET - 2) + '/' + String(DRAW_CALL_BUDGET),
    );
    expect(lines).toContain('tri 150000/' + String(TRIANGLE_BUDGET));
    expect(lines).toContain(
      'particles ' + String(PARTICLE_BUDGET - 16) + '/' + String(PARTICLE_BUDGET),
    );
    expect(lines).toContain('mats 18/' + String(MATERIAL_BUDGET));
    expect(lines).toContain('budget OK');
    expect(lines).not.toContain('!');
  });

  it('超预算的读数被标记，并把总判定改为 OVER', () => {
    const over = sample();
    over.p95IntervalMs = FRAME_TIME_BUDGET_MS + 0.1;
    over.drawCalls = DRAW_CALL_BUDGET + 1;
    over.triangles = TRIANGLE_BUDGET + 1;
    over.particles = PARTICLE_BUDGET + 1;
    const lines = debugLines(over).join('\n');
    expect(lines).toContain(
      'draw ' + String(DRAW_CALL_BUDGET + 1) + '/' + String(DRAW_CALL_BUDGET) + ' !',
    );
    expect(lines).toContain('budget OVER');
    expect(withinBudget(over)).toBe(false);
  });

  it('恰好等于预算时判定为达标', () => {
    const edge = sample();
    edge.p95IntervalMs = FRAME_TIME_BUDGET_MS;
    edge.drawCalls = DRAW_CALL_BUDGET;
    edge.triangles = TRIANGLE_BUDGET;
    edge.particles = PARTICLE_BUDGET;
    edge.materialCount = MATERIAL_BUDGET;
    expect(withinBudget(edge)).toBe(true);
    expect(debugLines(edge).join('\n')).toContain('budget OK');
  });

  it('F3 开关切换可见性并清空读数', () => {
    const root = document.createElement('div');
    const panel = createDebugPanel(root, false);
    expect(panel.visible).toBe(false);
    expect(root.classList.contains('debug-hidden')).toBe(true);
    expect(panel.toggle()).toBe(true);
    expect(root.classList.contains('debug-hidden')).toBe(false);
    panel.update(sample());
    panel.dispose();
    expect(root.textContent).toBe('');
    expect(DEBUG_REFRESH_MS).toBeGreaterThan(0);
  });
});
