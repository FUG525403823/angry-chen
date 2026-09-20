import { appendFile, mkdir, readFile } from 'node:fs/promises';
import { join } from 'node:path';

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
}

export const DEFAULT_DATA_DIR = './data';
export const MATCHES_FILE_NAME = 'matches.ndjson';

export interface JsonMatchStoreOptions {
  readonly dir?: string;
  readonly metrics?: Metrics;
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
  corruptLines = 0;
  private readonly records: MatchResultRecord[] = [];
  private writeChain: Promise<void> = Promise.resolve();
  private readonly metrics: Metrics | undefined;

  constructor(options: JsonMatchStoreOptions = {}) {
    this.dir = options.dir ?? DEFAULT_DATA_DIR;
    this.filePath = join(this.dir, MATCHES_FILE_NAME);
    this.metrics = options.metrics;
  }

  async load(): Promise<void> {
    let content: string;
    try {
      content = await readFile(this.filePath, 'utf8');
    } catch {
      return;
    }
    this.corruptLines = 0;
    const lines = content.split('\n');
    for (let i = 0; i < lines.length; i += 1) {
      const line = lines[i]?.trim() ?? '';
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
    }
    if (this.metrics !== undefined) this.metrics.corruptLines = this.corruptLines;
  }

  appendMatchResult(record: MatchResultRecord): Promise<void> {
    this.writeChain = this.writeChain.then(async () => {
      await mkdir(this.dir, { recursive: true });
      await appendFile(this.filePath, JSON.stringify(record) + String.fromCharCode(10), 'utf8');
      this.records.push(record);
    });
    return this.writeChain;
  }

  flush(): Promise<void> {
    return this.writeChain;
  }

  listTopScores(limit: number): Promise<MatchResultRecord[]> {
    const sorted = this.records.slice().sort((a, b) => {
      const byKills = recordMaxKills(b) - recordMaxKills(a);
      if (byKills !== 0) return byKills;
      return b.startedAtMs - a.startedAtMs;
    });
    return Promise.resolve(limit > 0 ? sorted.slice(0, limit) : []);
  }

  getRecentMatches(limit: number): Promise<MatchResultRecord[]> {
    const sorted = this.records.slice().sort((a, b) => b.startedAtMs - a.startedAtMs);
    return Promise.resolve(limit > 0 ? sorted.slice(0, limit) : []);
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
