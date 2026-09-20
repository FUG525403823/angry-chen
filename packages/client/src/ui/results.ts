export const LEADERBOARD_LIMIT = 10;
export const RECENT_MATCH_LIMIT = 5;
export const RESULTS_RETRY_MS = 400;

export interface MatchRecordPlayer {
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

export interface MatchRecord {
  matchId: string;
  startedAtMs: number;
  durationMs: number;
  waveReached: number;
  winnerTeam: number;
  playerCount: number;
  players: MatchRecordPlayer[];
}

export interface JsonResponse {
  ok: boolean;
  json(): Promise<unknown>;
}

export interface JsonFetcher {
  (url: string): Promise<JsonResponse>;
}

export interface ResultsSummary {
  winnerTeam: number;
  waveReached: number;
  durationMs: number;
  players: readonly { name: string; kills: number }[];
}

export function emptyResultsSummary(): ResultsSummary {
  return { winnerTeam: 0, waveReached: 0, durationMs: 0, players: [] };
}

function isObject(value: unknown): value is Record<string, unknown> {
  return typeof value === 'object' && value !== null;
}

function asNumber(value: unknown): number | undefined {
  return typeof value === 'number' && Number.isFinite(value) ? value : undefined;
}

function asString(value: unknown): string | undefined {
  return typeof value === 'string' ? value : undefined;
}

export function parseMatchRecord(value: unknown): MatchRecord | null {
  if (!isObject(value)) return null;
  const matchId = asString(value.matchId);
  const startedAtMs = asNumber(value.startedAtMs);
  const durationMs = asNumber(value.durationMs);
  const waveReached = asNumber(value.waveReached);
  const winnerTeam = asNumber(value.winnerTeam);
  if (
    matchId === undefined ||
    startedAtMs === undefined ||
    durationMs === undefined ||
    waveReached === undefined ||
    winnerTeam === undefined
  ) {
    return null;
  }
  const rawPlayers = value.players;
  const players: MatchRecordPlayer[] = [];
  if (Array.isArray(rawPlayers)) {
    for (let i = 0; i < rawPlayers.length; i += 1) {
      const raw = rawPlayers[i];
      if (!isObject(raw)) continue;
      const name = asString(raw.name);
      if (name === undefined) continue;
      players.push({
        name,
        kills: asNumber(raw.kills) ?? 0,
        headshots: asNumber(raw.headshots) ?? 0,
        shotsFired: asNumber(raw.shotsFired) ?? 0,
        hits: asNumber(raw.hits) ?? 0,
        revives: asNumber(raw.revives) ?? 0,
        downs: asNumber(raw.downs) ?? 0,
        aliveMs: asNumber(raw.aliveMs) ?? 0,
        leftMidMatch: raw.leftMidMatch === true,
      });
    }
  }
  return {
    matchId,
    startedAtMs,
    durationMs,
    waveReached,
    winnerTeam: winnerTeam === 1 ? 1 : 0,
    playerCount: asNumber(value.playerCount) ?? players.length,
    players,
  };
}

export function parseLeaderboardBody(value: unknown): MatchRecord[] {
  if (!isObject(value)) return [];
  const entries = value.entries;
  if (!Array.isArray(entries)) return [];
  const records: MatchRecord[] = [];
  for (let i = 0; i < entries.length; i += 1) {
    const record = parseMatchRecord(entries[i]);
    if (record !== null) records.push(record);
  }
  return records;
}

export function accuracyPercentOf(player: MatchRecordPlayer): number {
  if (player.shotsFired <= 0) return 0;
  const ratio = player.hits / player.shotsFired;
  const clamped = ratio < 0 ? 0 : ratio > 1 ? 1 : ratio;
  return Math.round(clamped * 100);
}

export function formatDurationMs(ms: number): string {
  const totalSeconds = ms > 0 ? Math.round(ms / 1000) : 0;
  const minutes = Math.floor(totalSeconds / 60);
  const seconds = totalSeconds - minutes * 60;
  return String(minutes) + ':' + (seconds < 10 ? '0' : '') + String(seconds);
}

export function winnerBannerOf(winnerTeam: number): string {
  return winnerTeam === 0 ? '胜利：守住牧场' : '失败：羊群获胜';
}

export function pickMatchRecord(
  records: readonly MatchRecord[],
  summary: ResultsSummary,
): MatchRecord | undefined {
  let partial: MatchRecord | undefined;
  for (let i = 0; i < records.length; i += 1) {
    const record = records[i];
    if (record === undefined) continue;
    if (record.winnerTeam !== summary.winnerTeam || record.waveReached !== summary.waveReached) {
      continue;
    }
    if (record.playerCount === summary.players.length) return record;
    if (partial === undefined) partial = record;
  }
  return partial;
}

export function leaderboardRowText(record: MatchRecord): string {
  let best = 0;
  for (let i = 0; i < record.players.length; i += 1) {
    const player = record.players[i];
    if (player !== undefined && player.kills > best) best = player.kills;
  }
  return (
    record.matchId +
    ' · 第' +
    String(record.waveReached) +
    '波 · 最高击杀 ' +
    String(best) +
    ' · ' +
    formatDurationMs(record.durationMs)
  );
}

async function fetchRecords(fetcher: JsonFetcher, url: string): Promise<MatchRecord[]> {
  try {
    const response = await fetcher(url);
    if (!response.ok) return [];
    return parseLeaderboardBody(await response.json());
  } catch {
    return [];
  }
}

export function fetchLeaderboard(
  fetcher: JsonFetcher,
  limit: number = LEADERBOARD_LIMIT,
): Promise<MatchRecord[]> {
  return fetchRecords(fetcher, '/api/leaderboard?limit=' + String(limit));
}

export function fetchRecentMatches(
  fetcher: JsonFetcher,
  limit: number = RECENT_MATCH_LIMIT,
): Promise<MatchRecord[]> {
  return fetchRecords(fetcher, '/api/matches/recent?limit=' + String(limit));
}

export interface ResultsOptions {
  onReturnToLobby(): void;
  fetcher?: JsonFetcher | undefined;
}

export interface Results {
  readonly visible: boolean;
  setSummary(summary: ResultsSummary): void;
  show(): void;
  hide(): void;
  dispose(): void;
}

function button(label: string): HTMLButtonElement {
  const node = document.createElement('button');
  node.type = 'button';
  node.className = 'button';
  node.textContent = label;
  return node;
}

function cell(tag: 'th' | 'td', text: string): HTMLTableCellElement {
  const node = document.createElement(tag);
  node.textContent = text;
  return node;
}

const RESULT_COLUMNS: readonly string[] = Object.freeze([
  '玩家',
  '击杀',
  '爆头',
  '命中率',
  '救援',
  '倒地',
  '存活',
]);

export function createResults(root: HTMLElement, options: ResultsOptions): Results {
  root.classList.add('screen', 'screen-hidden');
  const panel = document.createElement('div');
  panel.className = 'panel panel-wide';
  const banner = document.createElement('h2');
  banner.className = 'panel-title';
  const summary = document.createElement('div');
  summary.className = 'hint';
  const table = document.createElement('table');
  table.className = 'results-table';
  const head = document.createElement('thead');
  const headRow = document.createElement('tr');
  for (const column of RESULT_COLUMNS) headRow.append(cell('th', column));
  head.append(headRow);
  const body = document.createElement('tbody');
  table.append(head, body);
  const leaderboardTitle = document.createElement('h3');
  leaderboardTitle.className = 'panel-subtitle';
  leaderboardTitle.textContent = '排行榜前 10';
  const leaderboard = document.createElement('ul');
  leaderboard.className = 'leaderboard';
  const back = button('返回大厅');
  panel.append(banner, summary, table, leaderboardTitle, leaderboard, back);
  root.append(panel);

  let visible = false;
  let current: ResultsSummary = emptyResultsSummary();
  let record: MatchRecord | undefined;
  let entries: readonly MatchRecord[] = [];
  let loading = false;
  let generation = 0;

  const defaultFetcher: JsonFetcher = (url) => fetch(url);

  function renderRows(): void {
    body.replaceChildren();
    if (record !== undefined) {
      for (let i = 0; i < record.players.length; i += 1) {
        const player = record.players[i];
        if (player === undefined) continue;
        const row = document.createElement('tr');
        row.append(
          cell('td', player.name + (player.leftMidMatch ? '（中途离开）' : '')),
          cell('td', String(player.kills)),
          cell('td', String(player.headshots)),
          cell('td', String(accuracyPercentOf(player)) + '%'),
          cell('td', String(player.revives)),
          cell('td', String(player.downs)),
          cell('td', formatDurationMs(player.aliveMs)),
        );
        body.append(row);
      }
      return;
    }
    for (let i = 0; i < current.players.length; i += 1) {
      const player = current.players[i];
      if (player === undefined) continue;
      const row = document.createElement('tr');
      row.append(
        cell('td', player.name),
        cell('td', String(player.kills)),
        cell('td', '—'),
        cell('td', '—'),
        cell('td', '—'),
        cell('td', '—'),
        cell('td', '—'),
      );
      body.append(row);
    }
  }

  function render(): void {
    const winnerTeam = record === undefined ? current.winnerTeam : record.winnerTeam;
    const waveReached = record === undefined ? current.waveReached : record.waveReached;
    const durationMs = record === undefined ? current.durationMs : record.durationMs;
    banner.textContent = winnerBannerOf(winnerTeam);
    banner.classList.toggle('banner-lose', winnerTeam !== 0);
    summary.textContent =
      '打到第 ' +
      String(waveReached) +
      ' 波 · 用时 ' +
      formatDurationMs(durationMs) +
      (record === undefined ? ' · 统计数据加载中…' : '');
    renderRows();
    leaderboard.replaceChildren();
    if (entries.length === 0) {
      const item = document.createElement('li');
      item.className = 'leaderboard-row';
      item.textContent = loading ? '排行榜加载中…' : '暂无排行榜数据';
      leaderboard.append(item);
      return;
    }
    for (let i = 0; i < entries.length; i += 1) {
      const entry = entries[i];
      if (entry === undefined) continue;
      const item = document.createElement('li');
      item.className = 'leaderboard-row';
      item.textContent = String(i + 1) + '. ' + leaderboardRowText(entry);
      leaderboard.append(item);
    }
  }

  async function load(): Promise<void> {
    const token = generation;
    const fetcher = options.fetcher ?? defaultFetcher;
    let recent = await fetchRecentMatches(fetcher);
    const top = await fetchLeaderboard(fetcher);
    if (token !== generation || !visible) return;
    let found = pickMatchRecord(recent, current);
    if (found === undefined) {
      // 结算记录由服务器异步落盘，HTTP 首次可能读不到，稍后重试一次。
      await new Promise<void>((resolve) => {
        setTimeout(resolve, RESULTS_RETRY_MS);
      });
      if (token !== generation || !visible) return;
      recent = await fetchRecentMatches(fetcher);
      found = pickMatchRecord(recent, current);
    }
    if (found !== undefined) record = found;
    entries = top.slice(0, LEADERBOARD_LIMIT);
    loading = false;
    render();
  }

  back.addEventListener('click', () => {
    hide();
    options.onReturnToLobby();
  });

  function hide(): void {
    visible = false;
    generation += 1;
    loading = false;
    record = undefined;
    entries = [];
    root.classList.add('screen-hidden');
  }

  return {
    get visible(): boolean {
      return visible;
    },
    setSummary(next: ResultsSummary): void {
      current = next;
      if (visible) render();
    },
    show(): void {
      root.classList.remove('screen-hidden');
      if (visible) {
        render();
        return;
      }
      visible = true;
      loading = true;
      generation += 1;
      render();
      void load();
    },
    hide,
    dispose(): void {
      root.replaceChildren();
    },
  };
}
