import { mkdir, writeFile } from 'node:fs/promises';
import { join } from 'node:path';

import { snapshotBytesAvg, tickJitterP50, tickJitterP95, type Metrics } from './metrics.ts';

export const REPORTS_DIR_NAME = 'reports';
export const REPORT_FILE_EXTENSION = '.json';

export interface MatchDiagnostics {
  matchId: string;
  durationMs: number;
  ticks: { total: number; skipped: number; jitterMsP50: number; jitterMsP95: number };
  net: {
    snapshotBytesAvg: number;
    snapshotBytesMax: number;
    messagesInTotal: number;
    malformedInTotal: number;
    rateLimitedTotal: number;
  };
  fair: {
    hardCorrectTotal: number;
    poseSuspectTotal: number;
    poseRejectedTotal: number;
    rewindClampedTotal: number;
    speedViolationsTotal: number;
    shotsFiredTotal: number;
    hitsTotal: number;
  };
  grace: { starts: number; reconnects: number; timeouts: number };
  peak: { entities: number; players: number };
}

export interface MatchCounters {
  ticks: number;
  skipped: number;
  graceStarts: number;
  graceReconnects: number;
  graceTimeouts: number;
  peakEntities: number;
  peakPlayers: number;
}

export function createMatchCounters(): MatchCounters {
  return {
    ticks: 0,
    skipped: 0,
    graceStarts: 0,
    graceReconnects: 0,
    graceTimeouts: 0,
    peakEntities: 0,
    peakPlayers: 0,
  };
}

export function resetMatchCounters(counters: MatchCounters): void {
  counters.ticks = 0;
  counters.skipped = 0;
  counters.graceStarts = 0;
  counters.graceReconnects = 0;
  counters.graceTimeouts = 0;
  counters.peakEntities = 0;
  counters.peakPlayers = 0;
}

export function roundTo(value: number, digits: number): number {
  const factor = Math.pow(10, digits);
  return Math.round(value * factor) / factor;
}

export interface MatchDiagnosticsInput {
  readonly matchId: string;
  readonly startedAtMs: number;
  readonly endedAtMs: number;
  readonly counters: MatchCounters;
  readonly metrics: Metrics;
}

export function buildMatchDiagnostics(input: MatchDiagnosticsInput): MatchDiagnostics {
  const counters = input.counters;
  const metrics = input.metrics;
  const durationMs = input.endedAtMs > input.startedAtMs ? input.endedAtMs - input.startedAtMs : 0;
  return {
    matchId: input.matchId,
    durationMs,
    ticks: {
      total: counters.ticks,
      skipped: counters.skipped,
      jitterMsP50: roundTo(tickJitterP50(metrics), 3),
      jitterMsP95: roundTo(tickJitterP95(metrics), 3),
    },
    net: {
      snapshotBytesAvg: roundTo(snapshotBytesAvg(metrics), 2),
      snapshotBytesMax: metrics.snapshotBytesMax,
      messagesInTotal: metrics.framesIn,
      malformedInTotal: metrics.malformedFrames,
      rateLimitedTotal: metrics.rateLimitedFrames,
    },
    fair: {
      hardCorrectTotal: metrics.hardCorrectTotal,
      poseSuspectTotal: metrics.poseSuspects,
      poseRejectedTotal: metrics.poseRejected,
      rewindClampedTotal: metrics.rewindClampedCount,
      speedViolationsTotal: metrics.speedViolations,
      shotsFiredTotal: metrics.shotsFired,
      hitsTotal: metrics.hits,
    },
    grace: {
      starts: counters.graceStarts,
      reconnects: counters.graceReconnects,
      timeouts: counters.graceTimeouts,
    },
    peak: {
      entities: counters.peakEntities,
      players: counters.peakPlayers,
    },
  };
}

export function matchReportFileName(matchId: string): string {
  const safe = matchId.replace(/[^A-Za-z0-9._-]+/g, '_');
  const name = safe.length === 0 ? 'match' : safe;
  return name + REPORT_FILE_EXTENSION;
}

export function matchReportPath(dataDir: string, matchId: string): string {
  return join(dataDir, REPORTS_DIR_NAME, matchReportFileName(matchId));
}

export async function writeMatchReport(
  dataDir: string,
  diagnostics: MatchDiagnostics,
): Promise<string> {
  const dir = join(dataDir, REPORTS_DIR_NAME);
  await mkdir(dir, { recursive: true });
  const filePath = join(dir, matchReportFileName(diagnostics.matchId));
  await writeFile(filePath, JSON.stringify(diagnostics, null, 2) + String.fromCharCode(10), 'utf8');
  return filePath;
}
