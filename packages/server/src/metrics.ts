const newline = String.fromCharCode(10);
const sampleRingSize = 128;

export interface Metrics {
  ticks: number;
  tickSkips: number;
  tickIntervalSamples: number;
  tickIntervalSumMs: number;
  tickIntervalMaxMs: number;
  tickIntervalRing: Float64Array;
  tickIntervalCursor: number;
  tickScheduleErrorSamples: number;
  tickScheduleErrorSumMs: number;
  tickScheduleErrorMaxMs: number;
  tickScheduleErrorRing: Float64Array;
  tickScheduleErrorCursor: number;
  tickWorkSamples: number;
  tickWorkSumMs: number;
  tickWorkMaxMs: number;
  tickWorkRing: Float64Array;
  tickWorkCursor: number;
  simDriftSamples: number;
  simDriftMaxAbs: number;
  simDriftLastMs: number;
  simDriftRing: Float64Array;
  simDriftCursor: number;
  roomBudgetExceeded: number;
  snapshotsSent: number;
  snapshotBytes: number;
  snapshotBytesMax: number;
  snapshotRecords: number;
  eventsSent: number;
  /** 事件池打满后丢弃的事件数（累计，O04 §5）。 */
  eventsDropped: number;
  framesIn: number;
  framesOut: number;
  bytesIn: number;
  bytesOut: number;
  malformedFrames: number;
  rateLimitedFrames: number;
  droppedFrames: number;
  chatMessages: number;
  pings: number;
  joins: number;
  leaves: number;
  roomsCreated: number;
  roomsReclaimed: number;
  errorsSent: number;
  startMatchRequests: number;
  interactRequests: number;
  respawnRequests: number;
  maxEntitiesObserved: number;
  shotsFired: number;
  hits: number;
  damage: number;
  speedViolations: number;
  poseSuspects: number;
  poseRejected: number;
  sheepAlive: number;
  waveCurrent: number;
  spawns: number;
  hardCorrectTotal: number;
  rewindClampedCount: number;
  matchesFinished: number;
  corruptLines: number;
  graceStarts: number;
  graceReconnects: number;
  graceTimeouts: number;
}

export interface PrometheusGauges {
  readonly rooms: number;
  readonly connections: number;
  readonly players: number;
  readonly uptimeSeconds: number;
  readonly oversizedFrames: number;
  /** O03：因出站积压未入队而丢弃的帧数（累计）。 */
  readonly slowClientDrops: number;
  /** O03：所有连接当前持有的未写完成字节数之和。 */
  readonly sendQueueBytes: number;
  readonly clientLagTicksAvg: number;
  readonly graceActive: number;
}

export function createMetrics(): Metrics {
  return {
    ticks: 0,
    tickSkips: 0,
    tickIntervalSamples: 0,
    tickIntervalSumMs: 0,
    tickIntervalMaxMs: 0,
    tickIntervalRing: new Float64Array(sampleRingSize),
    tickIntervalCursor: 0,
    tickScheduleErrorSamples: 0,
    tickScheduleErrorSumMs: 0,
    tickScheduleErrorMaxMs: 0,
    tickScheduleErrorRing: new Float64Array(sampleRingSize),
    tickScheduleErrorCursor: 0,
    tickWorkSamples: 0,
    tickWorkSumMs: 0,
    tickWorkMaxMs: 0,
    tickWorkRing: new Float64Array(sampleRingSize),
    tickWorkCursor: 0,
    simDriftSamples: 0,
    simDriftMaxAbs: 0,
    simDriftLastMs: 0,
    simDriftRing: new Float64Array(sampleRingSize),
    simDriftCursor: 0,
    roomBudgetExceeded: 0,
    snapshotsSent: 0,
    snapshotBytes: 0,
    snapshotBytesMax: 0,
    snapshotRecords: 0,
    eventsSent: 0,
    eventsDropped: 0,
    framesIn: 0,
    framesOut: 0,
    bytesIn: 0,
    bytesOut: 0,
    malformedFrames: 0,
    rateLimitedFrames: 0,
    droppedFrames: 0,
    chatMessages: 0,
    pings: 0,
    joins: 0,
    leaves: 0,
    roomsCreated: 0,
    roomsReclaimed: 0,
    errorsSent: 0,
    startMatchRequests: 0,
    interactRequests: 0,
    respawnRequests: 0,
    maxEntitiesObserved: 0,
    shotsFired: 0,
    hits: 0,
    damage: 0,
    speedViolations: 0,
    poseSuspects: 0,
    poseRejected: 0,
    sheepAlive: 0,
    waveCurrent: 0,
    spawns: 0,
    hardCorrectTotal: 0,
    rewindClampedCount: 0,
    matchesFinished: 0,
    corruptLines: 0,
    graceStarts: 0,
    graceReconnects: 0,
    graceTimeouts: 0,
  };
}

function recordRing(ring: Float64Array, cursor: number, sample: number): number {
  ring[cursor] = sample;
  return (cursor + 1) % ring.length;
}

/**
 * 旧口径（对照）：相邻两次实际 tick 的间隔误差。它同时混入调度误差与 OS 定时器粒度，
 * 因此 O02 起判定改用 `recordTickScheduleError`，本指标只保留作对照（`ac_tick_interval_error_ms_p95`）。
 */
export function recordTickInterval(metrics: Metrics, deltaMs: number): void {
  const sample = Math.abs(deltaMs);
  metrics.tickIntervalSamples += 1;
  metrics.tickIntervalSumMs += sample;
  if (sample > metrics.tickIntervalMaxMs) metrics.tickIntervalMaxMs = sample;
  metrics.tickIntervalCursor = recordRing(
    metrics.tickIntervalRing,
    metrics.tickIntervalCursor,
    sample,
  );
}

/** 新口径：实际 tick 时刻 − 理想时刻（首 tick + n × 50ms），衡量调度质量而非定时器粒度。 */
export function recordTickScheduleError(metrics: Metrics, errorMs: number): void {
  const sample = Math.abs(errorMs);
  metrics.tickScheduleErrorSamples += 1;
  metrics.tickScheduleErrorSumMs += sample;
  if (sample > metrics.tickScheduleErrorMaxMs) metrics.tickScheduleErrorMaxMs = sample;
  metrics.tickScheduleErrorCursor = recordRing(
    metrics.tickScheduleErrorRing,
    metrics.tickScheduleErrorCursor,
    sample,
  );
}

/** 单次 runTick 的工作耗时：把「调度问题」与「计算问题」分开。 */
export function recordTickWork(metrics: Metrics, workMs: number): void {
  const sample = workMs > 0 ? workMs : 0;
  metrics.tickWorkSamples += 1;
  metrics.tickWorkSumMs += sample;
  if (sample > metrics.tickWorkMaxMs) metrics.tickWorkMaxMs = sample;
  metrics.tickWorkCursor = recordRing(metrics.tickWorkRing, metrics.tickWorkCursor, sample);
}

/**
 * 漂移：`(模拟时间 − 基准模拟时间) − (真实时间 − 基准真实时间)`，带符号。
 * 环里保留近期样本（供 gauge 与判定），`simDriftMaxAbs` 记录全时段最大值。
 */
export function recordSimDrift(metrics: Metrics, driftMs: number): void {
  metrics.simDriftSamples += 1;
  metrics.simDriftLastMs = driftMs;
  const abs = Math.abs(driftMs);
  if (abs > metrics.simDriftMaxAbs) metrics.simDriftMaxAbs = abs;
  metrics.simDriftCursor = recordRing(metrics.simDriftRing, metrics.simDriftCursor, driftMs);
}

export function percentileFromSorted(sorted: readonly number[], percentile: number): number {
  if (sorted.length === 0) return 0;
  const clamped = Math.min(Math.max(percentile, 0), 1);
  const index = Math.min(sorted.length - 1, Math.floor(sorted.length * clamped));
  return sorted[index] ?? 0;
}

function ringPercentile(ring: Float64Array, samples: number, percentile: number): number {
  const filled = Math.min(samples, ring.length);
  if (filled === 0) return 0;
  const sorted = Array.from(ring.subarray(0, filled)).sort((a, b) => a - b);
  return percentileFromSorted(sorted, percentile);
}

function ringAvg(sumMs: number, samples: number): number {
  if (samples === 0) return 0;
  return sumMs / samples;
}

export function tickIntervalPercentile(metrics: Metrics, percentile: number): number {
  return ringPercentile(metrics.tickIntervalRing, metrics.tickIntervalSamples, percentile);
}

export function tickIntervalP50(metrics: Metrics): number {
  return tickIntervalPercentile(metrics, 0.5);
}

export function tickIntervalP95(metrics: Metrics): number {
  return tickIntervalPercentile(metrics, 0.95);
}

export function tickIntervalAvg(metrics: Metrics): number {
  return ringAvg(metrics.tickIntervalSumMs, metrics.tickIntervalSamples);
}

export function tickScheduleErrorPercentile(metrics: Metrics, percentile: number): number {
  return ringPercentile(
    metrics.tickScheduleErrorRing,
    metrics.tickScheduleErrorSamples,
    percentile,
  );
}

export function tickScheduleErrorP50(metrics: Metrics): number {
  return tickScheduleErrorPercentile(metrics, 0.5);
}

export function tickScheduleErrorP95(metrics: Metrics): number {
  return tickScheduleErrorPercentile(metrics, 0.95);
}

export function tickWorkPercentile(metrics: Metrics, percentile: number): number {
  return ringPercentile(metrics.tickWorkRing, metrics.tickWorkSamples, percentile);
}

export function tickWorkP95(metrics: Metrics): number {
  return tickWorkPercentile(metrics, 0.95);
}

export function tickWorkP99(metrics: Metrics): number {
  return tickWorkPercentile(metrics, 0.99);
}

export function tickWorkAvg(metrics: Metrics): number {
  return ringAvg(metrics.tickWorkSumMs, metrics.tickWorkSamples);
}

/** 近期样本里 |漂移| 的最大值（Prometheus gauge 用；单次毛刺不会长期污染判定）。 */
export function simDriftMsMaxAbs(metrics: Metrics): number {
  const filled = Math.min(metrics.simDriftSamples, metrics.simDriftRing.length);
  let max = 0;
  for (let i = 0; i < filled; i += 1) {
    const value = Math.abs(metrics.simDriftRing[i] ?? 0);
    if (value > max) max = value;
  }
  return max;
}

export function snapshotBytesAvg(metrics: Metrics): number {
  if (metrics.snapshotsSent === 0) return 0;
  return metrics.snapshotBytes / metrics.snapshotsSent;
}

export function snapshotRecordsAvg(metrics: Metrics): number {
  if (metrics.snapshotsSent === 0) return 0;
  return metrics.snapshotRecords / metrics.snapshotsSent;
}

export function renderPrometheus(metrics: Metrics, gauges: PrometheusGauges): string {
  const lines: string[] = [];
  const push = (name: string, help: string, type: string, value: number): void => {
    lines.push('# HELP ' + name + ' ' + help);
    lines.push('# TYPE ' + name + ' ' + type);
    lines.push(name + ' ' + String(value));
  };
  push(
    'ac_tick_schedule_error_ms_p95',
    'p95 of (actual tick instant - ideal instant), ideal = first tick + n x 50ms',
    'gauge',
    Number(tickScheduleErrorP95(metrics).toFixed(3)),
  );
  push(
    'ac_tick_schedule_error_ms_p50',
    'p50 of (actual tick instant - ideal instant)',
    'gauge',
    Number(tickScheduleErrorP50(metrics).toFixed(3)),
  );
  push(
    'ac_tick_schedule_error_ms_max',
    'largest observed schedule error since startup',
    'gauge',
    Number(metrics.tickScheduleErrorMaxMs.toFixed(3)),
  );
  push(
    'ac_tick_work_ms_p95',
    'p95 of wall time spent inside a single runTick',
    'gauge',
    Number(tickWorkP95(metrics).toFixed(3)),
  );
  push(
    'ac_tick_work_ms_p99',
    'p99 of wall time spent inside a single runTick',
    'gauge',
    Number(tickWorkP99(metrics).toFixed(3)),
  );
  push(
    'ac_tick_work_ms_max',
    'largest runTick wall time since startup',
    'gauge',
    Number(metrics.tickWorkMaxMs.toFixed(3)),
  );
  push(
    'ac_sim_drift_ms',
    'max recent |simulated time delta - real time delta| in ms',
    'gauge',
    Number(simDriftMsMaxAbs(metrics).toFixed(3)),
  );
  push(
    'ac_sim_drift_ms_last',
    'signed simulated-time drift of the most recent tick in ms',
    'gauge',
    Number(metrics.simDriftLastMs.toFixed(3)),
  );
  push(
    'ac_room_budget_exceeded_total',
    'rooms that yielded the event loop because of the per-call tick budget',
    'counter',
    metrics.roomBudgetExceeded,
  );
  push(
    'ac_tick_interval_error_ms_p95',
    'p95 of (interval between consecutive ticks - 50ms); legacy diagnostic, scheduled error is the criterion',
    'gauge',
    Number(tickIntervalP95(metrics).toFixed(3)),
  );
  // 旧名保留为别名：docs/运维手册.md、tools/soak.mjs、http.test.ts 仍按此名引用（与 interval p95 同值）。
  push(
    'ac_tick_jitter_ms_p95',
    'alias of ac_tick_interval_error_ms_p95',
    'gauge',
    Number(tickIntervalP95(metrics).toFixed(3)),
  );
  push(
    'ac_tick_jitter_ms_p50',
    'alias of the interval-based p50 diagnostic',
    'gauge',
    Number(tickIntervalP50(metrics).toFixed(3)),
  );
  push(
    'ac_tick_jitter_ms_avg',
    'alias of the interval-based average diagnostic',
    'gauge',
    Number(tickIntervalAvg(metrics).toFixed(3)),
  );
  push(
    'ac_tick_jitter_ms_max',
    'largest observed tick interval error since startup',
    'gauge',
    Number(metrics.tickIntervalMaxMs.toFixed(3)),
  );
  push(
    'ac_snapshot_bytes_avg',
    'average snapshot frame size in bytes',
    'gauge',
    Number(snapshotBytesAvg(metrics).toFixed(2)),
  );
  push(
    'ac_snapshot_bytes_max',
    'largest snapshot frame size in bytes',
    'gauge',
    metrics.snapshotBytesMax,
  );
  push(
    'ac_snapshot_records_avg',
    'average snapshot entity records per frame',
    'gauge',
    Number(snapshotRecordsAvg(metrics).toFixed(2)),
  );
  push(
    'ac_malformed_frames_total',
    'frames rejected by codec or dispatch',
    'counter',
    metrics.malformedFrames,
  );
  push(
    'ac_oversized_frames_total',
    'frames larger than the frame limit',
    'counter',
    gauges.oversizedFrames,
  );
  push(
    'ac_slow_client_drops_total',
    'frames dropped because the connection outbound backlog exceeded the limit',
    'counter',
    gauges.slowClientDrops,
  );
  push(
    'ac_send_queue_bytes',
    'bytes copied into send queues and not yet written, summed over connections',
    'gauge',
    gauges.sendQueueBytes,
  );
  push(
    'ac_rate_limited_frames_total',
    'frames dropped by the per-connection rate limiter',
    'counter',
    metrics.rateLimitedFrames,
  );
  push(
    'ac_dropped_frames_total',
    'frames dropped for other reasons',
    'counter',
    metrics.droppedFrames,
  );
  push('ac_ticks_total', 'simulation ticks executed', 'counter', metrics.ticks);
  push('ac_tick_skips_total', 'ticks skipped by the catch-up limit', 'counter', metrics.tickSkips);
  push('ac_snapshots_sent_total', 'snapshot frames sent', 'counter', metrics.snapshotsSent);
  push('ac_snapshot_bytes_total', 'snapshot bytes sent', 'counter', metrics.snapshotBytes);
  push('ac_events_sent_total', 'simulation events sent', 'counter', metrics.eventsSent);
  push(
    'ac_events_dropped_total',
    'events dropped because the per-tick event pool was exhausted',
    'counter',
    metrics.eventsDropped,
  );
  push('ac_frames_in_total', 'inbound frames accepted', 'counter', metrics.framesIn);
  push('ac_frames_out_total', 'outbound frames sent', 'counter', metrics.framesOut);
  push('ac_bytes_in_total', 'inbound bytes accepted', 'counter', metrics.bytesIn);
  push('ac_bytes_out_total', 'outbound bytes sent', 'counter', metrics.bytesOut);
  push(
    'ac_chat_messages_total',
    'chat messages sanitized and accepted',
    'counter',
    metrics.chatMessages,
  );
  push('ac_pings_total', 'ping frames answered', 'counter', metrics.pings);
  push('ac_shots_fired_total', 'weapon fire commands resolved', 'counter', metrics.shotsFired);
  push('ac_hits_total', 'pellets that hit an entity', 'counter', metrics.hits);
  push('ac_damage_total', 'total damage dealt', 'counter', metrics.damage);
  push('ac_sheep_alive', 'living sheep entities', 'gauge', metrics.sheepAlive);
  push('ac_wave_current', 'current wave number', 'gauge', metrics.waveCurrent);
  push('ac_spawns_total', 'sheep spawns by the director', 'counter', metrics.spawns);
  push(
    'ac_speed_violations_total',
    'ticks flagged by the pose speed or vertical check',
    'counter',
    metrics.speedViolations,
  );
  push(
    'ac_pose_suspect_total',
    'pose anomalies explained by authoritative derived movement (recorded, not corrected)',
    'counter',
    metrics.poseSuspects,
  );
  push(
    'ac_pose_rejected_total',
    'pose rejections forced by an illegal position',
    'counter',
    metrics.poseRejected,
  );
  push(
    'ac_hard_correct_total',
    'authoritative corrections actually forced on clients',
    'counter',
    metrics.hardCorrectTotal,
  );
  push(
    'ac_rewind_clamped_total',
    'rewind requests clamped to the 200ms limit',
    'counter',
    metrics.rewindClampedCount,
  );
  push('ac_joins_total', 'successful joins', 'counter', metrics.joins);
  push('ac_leaves_total', 'leaves and disconnects', 'counter', metrics.leaves);
  push('ac_rooms_created_total', 'rooms created', 'counter', metrics.roomsCreated);
  push('ac_rooms_reclaimed_total', 'idle rooms reclaimed', 'counter', metrics.roomsReclaimed);
  push('ac_errors_sent_total', 'error frames sent to clients', 'counter', metrics.errorsSent);
  push(
    'ac_start_match_requests_total',
    'startMatch requests',
    'counter',
    metrics.startMatchRequests,
  );
  push('ac_interact_requests_total', 'interact requests', 'counter', metrics.interactRequests);
  push('ac_respawn_requests_total', 'respawn requests', 'counter', metrics.respawnRequests);
  push(
    'ac_max_entities_observed',
    'peak active entity count observed',
    'gauge',
    metrics.maxEntitiesObserved,
  );
  push(
    'ac_client_lag_ticks_avg',
    'average client acknowledged-tick lag',
    'gauge',
    Number(gauges.clientLagTicksAvg.toFixed(2)),
  );
  push(
    'ac_grace_active',
    'sessions inside the disconnect grace period',
    'gauge',
    gauges.graceActive,
  );
  push(
    'ac_matches_finished_total',
    'matches that reached the ended phase',
    'counter',
    metrics.matchesFinished,
  );
  push(
    'ac_corrupt_lines_total',
    'NDJSON match lines skipped as corrupt',
    'counter',
    metrics.corruptLines,
  );
  push(
    'ac_grace_starts_total',
    'sessions that entered the disconnect grace period',
    'counter',
    metrics.graceStarts,
  );
  push(
    'ac_grace_reconnects_total',
    'sessions that reconnected inside the grace period',
    'counter',
    metrics.graceReconnects,
  );
  push(
    'ac_grace_timeouts_total',
    'sessions removed after the grace period expired',
    'counter',
    metrics.graceTimeouts,
  );
  push('ac_rooms', 'live rooms', 'gauge', gauges.rooms);
  push('ac_connections', 'open connections', 'gauge', gauges.connections);
  push('ac_players', 'sessions in rooms', 'gauge', gauges.players);
  push(
    'ac_uptime_seconds',
    'server uptime in seconds',
    'gauge',
    Number(gauges.uptimeSeconds.toFixed(1)),
  );
  return lines.join(newline) + newline;
}
