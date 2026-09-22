import { createReadStream } from 'node:fs';
import { appendFile, mkdir } from 'node:fs/promises';
import { join } from 'node:path';
import { createInterface } from 'node:readline';

import type { Metrics } from '../metrics.ts';

export interface MatchResultPlayer {
  name: string;
  kills: number;
  headshots: number;
  shotsFired: number;
  hits: number;
  revives: number;
  downs: number;
  aliveMs: number;
  leftMidMatch: boolean;
}

export interface MatchResultRecord {
  matchId: string;
  startedAtMs: number;
  durationMs: number;
  waveReached: number;
  winnerTeam: 0 | 1;
  playerCount: number;
  players: MatchResultPlayer[];
}

export interface MatchStore {
  appendMatchResult(record: MatchResultRecord): Promise<void>;
  listTopScores(limit: number): Promise<MatchResultRecord[]>;
  getRecentMatches(limit: number): Promise<MatchResultRecord[]>;
  /** O09：写入/淘汰计数，HTTP 读缓存据此失效（可选，便于内存实现省略）。 */
  readonly version?: number;
  /** O09：当前常驻记录数（可选；渲染 `ac_records_retained` 时同步）。 */
  readonly recordCount?: number;
}

export const DEFAULT_DATA_DIR = './data';
export const MATCHES_FILE_NAME = 'matches.ndjson';
/** O09：常驻战绩记录上限（超出按 `startedAtMs` 最旧淘汰；NDJSON 文件本身不截断）。 */
export const DEFAULT_MAX_RECORDS = 10000;

export interface JsonMatchStoreOptions {
  readonly dir?: string;
  readonly metrics?: Metrics;
  /** O09：常驻记录上限，默认 `DEFAULT_MAX_RECORDS`。 */
  readonly maxRecords?: number;
}

/** O09：top 视图的缓存条目（`maxKills` 每条记录只算一次）。 */
interface ScoreEntry {
  record: MatchResultRecord;
  maxKills: number;
}

/** O09：与「全排序取前 N」一致的比较器（击杀降序 → 时间降序 → `matchId` 升序决胜）。 */
function compareTop(a: ScoreEntry, b: ScoreEntry): number {
  if (b.maxKills !== a.maxKills) return b.maxKills - a.maxKills;
  if (b.record.startedAtMs !== a.record.startedAtMs) {
    return b.record.startedAtMs - a.record.startedAtMs;
  }
  return a.record.matchId < b.record.matchId ? -1 : a.record.matchId > b.record.matchId ? 1 : 0;
}

function compareRecent(a: MatchResultRecord, b: MatchResultRecord): number {
  if (b.startedAtMs !== a.startedAtMs) return b.startedAtMs - a.startedAtMs;
  return a.matchId < b.matchId ? -1 : a.matchId > b.matchId ? 1 : 0;
}

function recordMaxKills(record: MatchResultRecord): number {
  let best = 0;
  for (let i = 0; i < record.players.length; i += 1) {
    const player = record.players[i];
    if (player !== undefined && player.kills > best) best = player.kills;
  }
  return best;
}

function isMatchResultRecord(value: unknown): value is MatchResultRecord {
  if (typeof value !== 'object' || value === null) return false;
  return (
    'matchId' in value &&
    typeof value.matchId === 'string' &&
    'startedAtMs' in value &&
    typeof value.startedAtMs === 'number' &&
    'durationMs' in value &&
    typeof value.durationMs === 'number' &&
    'waveReached' in value &&
    typeof value.waveReached === 'number' &&
    'winnerTeam' in value &&
    (value.winnerTeam === 0 || value.winnerTeam === 1) &&
    'playerCount' in value &&
    typeof value.playerCount === 'number' &&
    'players' in value &&
    Array.isArray(value.players)
  );
}

export class JsonMatchStore implements MatchStore {
  readonly dir: string;
  readonly filePath: string;
  readonly maxRecords: number;
  corruptLines = 0;
  private writeVersion = 0;
  private readonly records: MatchResultRecord[] = [];
  private topCache: ScoreEntry[] | null = null;
  private recentCache: MatchResultRecord[] | null = null;
  private writeChain: Promise<void> = Promise.resolve();
  private readonly metrics: Metrics | undefined;

  constructor(options: JsonMatchStoreOptions = {}) {
    this.dir = options.dir ?? DEFAULT_DATA_DIR;
    this.filePath = join(this.dir, MATCHES_FILE_NAME);
    this.maxRecords = Math.max(1, Math.floor(options.maxRecords ?? DEFAULT_MAX_RECORDS));
    this.metrics = options.metrics;
  }

  get version(): number {
    return this.writeVersion;
  }

  get recordCount(): number {
    return this.records.length;
  }

  /** O09：缓存/指标的唯一失效点（`load` / `appendMatchResult` 两处调用）。 */
  private invalidateCaches(): void {
    this.topCache = null;
    this.recentCache = null;
    this.writeVersion += 1;
    if (this.metrics !== undefined) this.metrics.recordsRetained = this.records.length;
  }

  /** O09：超出上限时按 `startedAtMs` 最旧**批量**淘汰（每 `maxRecords/100` 次 append 摊销一次）。 */
  private prune(): void {
    if (this.records.length <= this.maxRecords) return;
    const batch = Math.max(1, Math.floor(this.maxRecords / 100));
    this.records.sort((a, b) => a.startedAtMs - b.startedAtMs);
    this.records.splice(0, Math.min(batch, this.records.length));
  }

  /** O09：流式加载（`createReadStream` + `readline`，不保留整文件字符串），边读边淘汰。 */
  async load(): Promise<void> {
    this.corruptLines = 0;
    this.records.length = 0;
    const stream = createReadStream(this.filePath, { encoding: 'utf8' });
    const reader = createInterface({ input: stream, crlfDelay: Number.POSITIVE_INFINITY });
    try {
      for await (const rawLine of reader) {
        const line = rawLine.trim();
        if (line.length === 0) continue;
        let parsed: unknown;
        try {
          parsed = JSON.parse(line);
        } catch {
          this.corruptLines += 1;
          continue;
        }
        if (!isMatchResultRecord(parsed)) {
          this.corruptLines += 1;
          continue;
        }
        this.records.push(parsed);
        this.prune();
      }
    } catch {
      // 文件不存在/不可读：保持空集合（与旧 `readFile` 的捕获语义一致）。
      this.records.length = 0;
    } finally {
      reader.close();
      stream.destroy();
    }
    this.invalidateCaches();
    if (this.metrics !== undefined) this.metrics.corruptLines = this.corruptLines;
  }

  appendMatchResult(record: MatchResultRecord): Promise<void> {
    this.writeChain = this.writeChain.then(async () => {
      await mkdir(this.dir, { recursive: true });
      await appendFile(this.filePath, JSON.stringify(record) + String.fromCharCode(10), 'utf8');
      this.records.push(record);
      this.prune();
      this.invalidateCaches();
    });
    return this.writeChain;
  }

  flush(): Promise<void> {
    return this.writeChain;
  }

  /** O09：命中缓存时 O(limit)；缓存为 null 时重建一次 O(n log n)。 */
  listTopScores(limit: number): Promise<MatchResultRecord[]> {
    if (limit <= 0) return Promise.resolve([]);
    if (this.topCache === null) {
      const entries: ScoreEntry[] = [];
      for (let i = 0; i < this.records.length; i += 1) {
        const record = this.records[i];
        if (record !== undefined) entries.push({ record, maxKills: recordMaxKills(record) });
      }
      entries.sort(compareTop);
      this.topCache = entries;
    }
    const out: MatchResultRecord[] = [];
    for (let i = 0; i < this.topCache.length && out.length < limit; i += 1) {
      const entry = this.topCache[i];
      if (entry !== undefined) out.push(entry.record);
    }
    return Promise.resolve(out);
  }

  getRecentMatches(limit: number): Promise<MatchResultRecord[]> {
    if (limit <= 0) return Promise.resolve([]);
    if (this.recentCache === null) this.recentCache = this.records.slice().sort(compareRecent);
    return Promise.resolve(this.recentCache.slice(0, limit));
  }
}

export function createJsonMatchStore(options: JsonMatchStoreOptions = {}): JsonMatchStore {
  return new JsonMatchStore(options);
}

export function createNullMatchStore(): MatchStore {
  return {
    appendMatchResult(): Promise<void> {
      return Promise.resolve();
    },
    listTopScores(): Promise<MatchResultRecord[]> {
      return Promise.resolve([]);
    },
    getRecentMatches(): Promise<MatchResultRecord[]> {
      return Promise.resolve([]);
    },
  };
}
