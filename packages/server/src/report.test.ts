import { mkdir, mkdtemp, open, readFile, readdir, rm, utimes, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

import { MATCH_PHASE } from '@ac/shared';
import { describe, expect, it } from 'vitest';

import { SILENT_LOGGER } from './log.ts';
import { endMatch } from './match/controller.ts';
import { createNullMatchStore } from './match/store.ts';
import {
  createMetrics,
  recordSimDrift,
  recordTickInterval,
  recordTickScheduleError,
  recordTickWork,
} from './metrics.ts';
import {
  buildMatchDiagnostics,
  createMatchCounters,
  DEFAULT_REPORT_RETENTION,
  matchReportFileName,
  matchReportPath,
  REPORTS_DIR_NAME,
  reportsDir,
  resetMatchCounters,
  retainReports,
  writeMatchReport,
} from './report.ts';
import { createRoom, type RoomDeps } from './room.ts';

async function waitForJson(path: string): Promise<Record<string, unknown>> {
  for (let attempt = 0; attempt < 200; attempt += 1) {
    try {
      return JSON.parse(await readFile(path, 'utf8')) as Record<string, unknown>;
    } catch {
      await new Promise((resolve) => setTimeout(resolve, 5));
    }
  }
  throw new Error('report not written: ' + path);
}

describe('单场诊断报告', () => {
  it('按冻结 schema 汇总 per-room 计数与进程指标', () => {
    const metrics = createMetrics();
    metrics.snapshotsSent = 2;
    metrics.snapshotBytes = 100;
    metrics.snapshotBytesMax = 777;
    metrics.framesIn = 42;
    metrics.malformedFrames = 3;
    metrics.rateLimitedFrames = 2;
    metrics.hardCorrectTotal = 4;
    metrics.poseSuspects = 7;
    metrics.poseRejected = 2;
    metrics.rewindClampedCount = 1;
    metrics.speedViolations = 5;
    metrics.shotsFired = 60;
    metrics.hits = 21;
    for (const jitter of [1, 2, 3, 4, 5]) recordTickInterval(metrics, jitter);
    for (const error of [0, 1, 2, 3, 12]) recordTickScheduleError(metrics, error);
    for (const work of [0.5, 1, 1.5, 2, 4]) recordTickWork(metrics, work);
    recordSimDrift(metrics, 3);
    recordSimDrift(metrics, -40);

    const counters = createMatchCounters();
    counters.ticks = 10;
    counters.skipped = 2;
    counters.graceStarts = 1;
    counters.graceReconnects = 1;
    counters.peakEntities = 40;
    counters.peakPlayers = 4;

    const diagnostics = buildMatchDiagnostics({
      matchId: 'M-1',
      startedAtMs: 1000,
      endedAtMs: 2500,
      counters,
      metrics,
    });
    expect(diagnostics.matchId).toBe('M-1');
    expect(diagnostics.durationMs).toBe(1500);
    expect(diagnostics.ticks.total).toBe(10);
    expect(diagnostics.ticks.skipped).toBe(2);
    expect(diagnostics.ticks.jitterMsP50).toBeGreaterThan(0);
    expect(diagnostics.ticks.jitterMsP95).toBeGreaterThan(diagnostics.ticks.jitterMsP50);
    expect(diagnostics.ticks.scheduleErrorMsP95).toBe(12);
    expect(diagnostics.ticks.workMsP95).toBe(4);
    expect(diagnostics.ticks.workMsP99).toBe(4);
    expect(diagnostics.ticks.simDriftMsMax).toBe(40);
    expect(diagnostics.net).toEqual({
      snapshotBytesAvg: 50,
      snapshotBytesMax: 777,
      messagesInTotal: 42,
      malformedInTotal: 3,
      rateLimitedTotal: 2,
    });
    expect(diagnostics.fair).toEqual({
      hardCorrectTotal: 4,
      poseSuspectTotal: 7,
      poseRejectedTotal: 2,
      rewindClampedTotal: 1,
      speedViolationsTotal: 5,
      shotsFiredTotal: 60,
      hitsTotal: 21,
    });
    expect(diagnostics.grace).toEqual({ starts: 1, reconnects: 1, timeouts: 0 });
    expect(diagnostics.peak).toEqual({ entities: 40, players: 4 });

    resetMatchCounters(counters);
    expect(counters).toEqual({
      ticks: 0,
      skipped: 0,
      graceStarts: 0,
      graceReconnects: 0,
      graceTimeouts: 0,
      peakEntities: 0,
      peakPlayers: 0,
    });
  });

  it('文件名净化并写入 DATA_DIR/reports/<matchId>.json', async () => {
    expect(matchReportFileName('AB-123')).toBe('AB-123.json');
    expect(matchReportFileName('a/b c')).toBe('a_b_c.json');
    expect(matchReportFileName('')).toBe('match.json');

    const dir = await mkdtemp(join(tmpdir(), 'ac-report-'));
    const diagnostics = buildMatchDiagnostics({
      matchId: 'AB-123',
      startedAtMs: 0,
      endedAtMs: 0,
      counters: createMatchCounters(),
      metrics: createMetrics(),
    });
    const filePath = await writeMatchReport(dir, diagnostics);
    expect(filePath).toBe(matchReportPath(dir, 'AB-123'));
    const parsed = JSON.parse(await readFile(filePath, 'utf8')) as Record<string, unknown>;
    expect(parsed).toEqual(JSON.parse(JSON.stringify(diagnostics)) as unknown);
    await rm(dir, { recursive: true, force: true });
  });

  it('对局结束时落盘报告', async () => {
    const dir = await mkdtemp(join(tmpdir(), 'ac-report-end-'));
    const now = 2_000_000;
    const startedAtMs = now - 1000;
    const metrics = createMetrics();
    recordTickInterval(metrics, 2);
    metrics.snapshotBytesMax = 321;
    const room = createRoom('RP01', 7, now);
    const deps: RoomDeps = {
      metrics,
      store: createNullMatchStore(),
      dataDir: dir,
      now: () => now,
      log: SILENT_LOGGER,
      monotonicNow: () => now,
    };
    room.phase = MATCH_PHASE.playing;
    room.match.startedAtMs = startedAtMs;
    room.match.counters.ticks = 20;
    room.match.counters.peakEntities = 12;
    room.match.counters.peakPlayers = 2;

    endMatch(room, deps, now, 0);

    const matchId = 'RP01-' + String(startedAtMs);
    const parsed = await waitForJson(matchReportPath(dir, matchId));
    expect(parsed['matchId']).toBe(matchId);
    expect(parsed['durationMs']).toBe(1000);
    expect(parsed['ticks']).toEqual({
      total: 20,
      skipped: 0,
      jitterMsP50: 2,
      jitterMsP95: 2,
      scheduleErrorMsP95: 0,
      workMsP95: 0,
      workMsP99: 0,
      simDriftMsMax: 0,
    });
    expect(parsed['net']).toMatchObject({ snapshotBytesMax: 321 });
    expect(parsed['peak']).toEqual({ entities: 12, players: 2 });
    await rm(dir, { recursive: true, force: true });
  });

  it('O09：retainReports 按 mtime 只留最近 N 份（0 = 不清理；目录不存在不抛）', async () => {
    const dir = await mkdtemp(join(tmpdir(), 'ac-retain-'));
    expect(reportsDir(dir)).toBe(join(dir, REPORTS_DIR_NAME));
    const reports = reportsDir(dir);
    await mkdir(reports, { recursive: true });
    for (let i = 0; i < 5; i += 1) {
      const file = join(reports, 'r-' + String(i) + '.json');
      await writeFile(file, '{}');
      const stamp = 1_700_000_000 + i * 10;
      await utimes(file, stamp, stamp);
    }
    await writeFile(join(reports, 'keep-me.txt'), 'x');

    expect(DEFAULT_REPORT_RETENTION).toBe(200);
    expect(await retainReports(reports, 2)).toBe(3);
    expect((await readdir(reports)).sort()).toEqual(['keep-me.txt', 'r-3.json', 'r-4.json']);

    expect(await retainReports(reports, 0)).toBe(0);
    expect((await readdir(reports)).length).toBe(3);
    expect(await retainReports(join(dir, 'missing'), 2)).toBe(0);

    await rm(dir, { recursive: true, force: true });
  });

  it('O09：单个文件删除失败时 retainReports 不抛出（对局结束路径不能被清理拖累）', async () => {
    const dir = await mkdtemp(join(tmpdir(), 'ac-retain-fail-'));
    const reports = reportsDir(dir);
    await mkdir(reports, { recursive: true });
    for (let i = 0; i < 3; i += 1) {
      const file = join(reports, 'f-' + String(i) + '.json');
      await writeFile(file, '{}');
      const stamp = 1_700_000_000 + i * 10;
      await utimes(file, stamp, stamp);
    }
    // Windows 上打开句柄会让 unlink 失败（EBUSY/EPERM）：无论成功与否都要求「不抛出」。
    const handle = await open(join(reports, 'f-0.json'), 'r');
    let removed = -1;
    let thrown = '';
    try {
      removed = await retainReports(reports, 1);
    } catch (error) {
      thrown = String(error);
    }
    await handle.close();
    expect(thrown).toBe('');
    expect(removed).toBeGreaterThanOrEqual(0);
    expect(removed).toBeLessThanOrEqual(2);
    await rm(dir, { recursive: true, force: true });
  });
});
