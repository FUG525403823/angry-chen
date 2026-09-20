const newline = String.fromCharCode(10);
const jitterRingSize = 128;

export const TICK_JITTER_BUCKETS_MS: readonly number[] = Object.freeze([1, 2, 4, 8, 16, 32]);

export interface Metrics {
  ticks: number;
  tickSkips: number;
  tickJitterSamples: number;
  tickJitterSumMs: number;
  tickJitterMaxMs: number;
  tickJitterRing: Float64Array;
  tickJitterCursor: number;
  tickJitterBucketCounts: Float64Array;
  snapshotsSent: number;
  snapshotBytes: number;
  snapshotBytesMax: number;
  snapshotRecords: number;
  eventsSent: number;
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
  readonly clientLagTicksAvg: number;
  readonly graceActive: number;
}

export function createMetrics(): Metrics {
  return {
    ticks: 0,
    tickSkips: 0,
    tickJitterSamples: 0,
    tickJitterSumMs: 0,
    tickJitterMaxMs: 0,
    tickJitterRing: new Float64Array(jitterRingSize),
    tickJitterCursor: 0,
    tickJitterBucketCounts: new Float64Array(TICK_JITTER_BUCKETS_MS.length),
    snapshotsSent: 0,
    snapshotBytes: 0,
    snapshotBytesMax: 0,
    snapshotRecords: 0,
    eventsSent: 0,
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

export function recordTickJitter(metrics: Metrics, deltaMs: number): void {
  const sample = Math.abs(deltaMs);
  metrics.tickJitterSamples += 1;
  metrics.tickJitterSumMs += sample;
  if (sample > metrics.tickJitterMaxMs) metrics.tickJitterMaxMs = sample;
  metrics.tickJitterRing[metrics.tickJitterCursor] = sample;
  metrics.tickJitterCursor = (metrics.tickJitterCursor + 1) % jitterRingSize;
  for (let i = 0; i < TICK_JITTER_BUCKETS_MS.length; i += 1) {
    const bound = TICK_JITTER_BUCKETS_MS[i];
    if (bound !== undefined && sample <= bound) {
      metrics.tickJitterBucketCounts[i] = (metrics.tickJitterBucketCounts[i] ?? 0) + 1;
      break;
    }
  }
}

export function percentileFromSorted(sorted: readonly number[], percentile: number): number {
  if (sorted.length === 0) return 0;
  const clamped = Math.min(Math.max(percentile, 0), 1);
  const index = Math.min(sorted.length - 1, Math.floor(sorted.length * clamped));
  return sorted[index] ?? 0;
}

export function tickJitterPercentile(metrics: Metrics, percentile: number): number {
  const filled = Math.min(metrics.tickJitterSamples, jitterRingSize);
  if (filled === 0) return 0;
  const sorted = Array.from(metrics.tickJitterRing.subarray(0, filled)).sort((a, b) => a - b);
  return percentileFromSorted(sorted, percentile);
}

export function tickJitterP50(metrics: Metrics): number {
  return tickJitterPercentile(metrics, 0.5);
}

export function tickJitterP95(metrics: Metrics): number {
  return tickJitterPercentile(metrics, 0.95);
}

export function tickJitterBucketCounts(metrics: Metrics): readonly number[] {
  return Array.from(metrics.tickJitterBucketCounts);
}

export function tickJitterAvg(metrics: Metrics): number {
  if (metrics.tickJitterSamples === 0) return 0;
  return metrics.tickJitterSumMs / metrics.tickJitterSamples;
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
    'ac_tick_jitter_ms_p95',
    'simulation tick jitter p95 in ms',
    'gauge',
    Number(tickJitterP95(metrics).toFixed(3)),
  );
  push(
    'ac_tick_jitter_ms_p50',
    'simulation tick jitter p50 in ms',
    'gauge',
    Number(tickJitterP50(metrics).toFixed(3)),
  );
  push(
    'ac_tick_jitter_ms_avg',
    'simulation tick jitter average in ms',
    'gauge',
    Number(tickJitterAvg(metrics).toFixed(3)),
  );
  push(
    'ac_tick_jitter_ms_max',
    'simulation tick jitter maximum in ms',
    'gauge',
    Number(metrics.tickJitterMaxMs.toFixed(3)),
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
    'commands rejected by pose validation',
    'counter',
    metrics.speedViolations,
  );
  push(
    'ac_hard_correct_total',
    'authoritative corrections forced on clients',
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
  lines.push('# HELP ac_tick_jitter_ms simulation tick jitter histogram in ms');
  lines.push('# TYPE ac_tick_jitter_ms histogram');
  const bucketCounts = metrics.tickJitterBucketCounts;
  let cumulative = 0;
  for (let i = 0; i < TICK_JITTER_BUCKETS_MS.length; i += 1) {
    const bound = TICK_JITTER_BUCKETS_MS[i];
    if (bound === undefined) continue;
    cumulative += bucketCounts[i] ?? 0;
    lines.push('ac_tick_jitter_ms_bucket{le="' + String(bound) + '"} ' + String(cumulative));
  }
  lines.push('ac_tick_jitter_ms_bucket{le="+Inf"} ' + String(metrics.tickJitterSamples));
  lines.push('ac_tick_jitter_ms_sum ' + String(Number(metrics.tickJitterSumMs.toFixed(3))));
  lines.push('ac_tick_jitter_ms_count ' + String(metrics.tickJitterSamples));
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
