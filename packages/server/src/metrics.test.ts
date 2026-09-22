import { describe, expect, it } from 'vitest';

import {
  createMetrics,
  percentileFromSorted,
  recordSimDrift,
  recordTickInterval,
  recordTickScheduleError,
  recordTickWork,
  renderPrometheus,
  simDriftMsMaxAbs,
  tickIntervalAvg,
  tickIntervalP95,
  tickScheduleErrorP50,
  tickScheduleErrorP95,
  tickWorkP95,
  tickWorkP99,
} from './metrics.ts';

const gauges = {
  rooms: 0,
  connections: 0,
  players: 0,
  uptimeSeconds: 0,
  oversizedFrames: 0,
  slowClientDrops: 0,
  sendQueueBytes: 0,
  clientLagTicksAvg: 0,
  graceActive: 0,
  snapshotRateX10: 200,
};

describe('调度 / 漂移 / 工作量指标', () => {
  it('O03 背压指标随 gauges 渲染', () => {
    const text = renderPrometheus(createMetrics(), {
      ...gauges,
      slowClientDrops: 7,
      sendQueueBytes: 4096,
    });
    expect(text).toContain('ac_slow_client_drops_total 7');
    expect(text).toContain('ac_send_queue_bytes 4096');
    expect(text).toContain('# TYPE ac_slow_client_drops_total counter');
    expect(text).toContain('# TYPE ac_send_queue_bytes gauge');
  });

  it('空指标返回 0 而不是 NaN', () => {
    const metrics = createMetrics();
    expect(tickScheduleErrorP95(metrics)).toBe(0);
    expect(tickScheduleErrorP50(metrics)).toBe(0);
    expect(tickWorkP95(metrics)).toBe(0);
    expect(tickWorkP99(metrics)).toBe(0);
    expect(tickIntervalP95(metrics)).toBe(0);
    expect(tickIntervalAvg(metrics)).toBe(0);
    expect(simDriftMsMaxAbs(metrics)).toBe(0);
  });

  it('分位数只在环内最近样本上计算（覆盖环回绕）', () => {
    const metrics = createMetrics();
    for (let i = 0; i < 200; i += 1) recordTickScheduleError(metrics, i);

    expect(metrics.tickScheduleErrorSamples).toBe(200);
    expect(metrics.tickScheduleErrorMaxMs).toBe(199);
    // 环容量 128：只剩样本 72..199，排序后 index = floor(128 × p)
    expect(tickScheduleErrorP95(metrics)).toBe(193);
    expect(tickScheduleErrorP50(metrics)).toBe(136);
    expect(percentileFromSorted([1, 2, 3, 4], 0.95)).toBe(4);
  });

  it('工作量按整段耗时累加，负数按 0 计', () => {
    const metrics = createMetrics();
    recordTickWork(metrics, 1.5);
    recordTickWork(metrics, -3);
    recordTickWork(metrics, 2.5);
    expect(metrics.tickWorkSamples).toBe(3);
    expect(metrics.tickWorkSumMs).toBe(4);
    expect(metrics.tickWorkMaxMs).toBe(2.5);
    expect(tickWorkP95(metrics)).toBe(2.5);
    expect(tickWorkP99(metrics)).toBe(2.5);
  });

  it('旧口径（interval）与新口径（schedule error）分别记账', () => {
    const metrics = createMetrics();
    recordTickInterval(metrics, -12);
    recordTickScheduleError(metrics, -7);
    expect(tickIntervalP95(metrics)).toBe(12);
    expect(tickScheduleErrorP95(metrics)).toBe(7);
    expect(tickIntervalAvg(metrics)).toBe(12);
  });

  it('漂移：近期最大值取绝对值，全时段最大值独立保留', () => {
    const metrics = createMetrics();
    recordSimDrift(metrics, -40);
    recordSimDrift(metrics, 10);
    expect(simDriftMsMaxAbs(metrics)).toBe(40);
    expect(metrics.simDriftMaxAbs).toBe(40);
    expect(metrics.simDriftLastMs).toBe(10);

    for (let i = 0; i < 128; i += 1) recordSimDrift(metrics, 1);
    expect(simDriftMsMaxAbs(metrics)).toBe(1); // 近窗口已经不含 -40
    expect(metrics.simDriftMaxAbs).toBe(40); // 全时段最大值保留
    expect(metrics.simDriftLastMs).toBe(1);
  });

  it('renderPrometheus 输出 O02 的六条新指标', () => {
    const text = renderPrometheus(createMetrics(), gauges);
    for (const name of [
      'ac_tick_schedule_error_ms_p95',
      'ac_tick_work_ms_p95',
      'ac_tick_work_ms_p99',
      'ac_sim_drift_ms',
      'ac_room_budget_exceeded_total',
      'ac_tick_interval_error_ms_p95',
    ]) {
      expect(text).toContain(name);
    }
    // 旧名保留为别名，运维手册/soak/http.test.ts 的既有引用不失效
    expect(text).toContain('ac_tick_jitter_ms_p95');
  });
});
