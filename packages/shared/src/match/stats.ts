export interface PlayerMatchStats {
  kills: number;
  headshots: number;
  shotsFired: number;
  hits: number;
  revives: number;
  downs: number;
  aliveMs: number;
  downedMs: number;
}

export function createPlayerMatchStats(): PlayerMatchStats {
  return {
    kills: 0,
    headshots: 0,
    shotsFired: 0,
    hits: 0,
    revives: 0,
    downs: 0,
    aliveMs: 0,
    downedMs: 0,
  };
}

export function resetPlayerMatchStats(stats: PlayerMatchStats): void {
  stats.kills = 0;
  stats.headshots = 0;
  stats.shotsFired = 0;
  stats.hits = 0;
  stats.revives = 0;
  stats.downs = 0;
  stats.aliveMs = 0;
  stats.downedMs = 0;
}

export function noteKill(stats: PlayerMatchStats, headshot: boolean): void {
  stats.kills += 1;
  if (headshot) stats.headshots += 1;
}

export function noteShot(stats: PlayerMatchStats): void {
  stats.shotsFired += 1;
}

export function noteHit(stats: PlayerMatchStats): void {
  stats.hits += 1;
}

export function noteDown(stats: PlayerMatchStats): void {
  stats.downs += 1;
}

export function noteRevive(stats: PlayerMatchStats): void {
  stats.revives += 1;
}

export function accumulateAlive(stats: PlayerMatchStats, dtMs: number): void {
  stats.aliveMs += dtMs;
}

export function accumulateDowned(stats: PlayerMatchStats, dtMs: number): void {
  stats.downedMs += dtMs;
}

export function accuracyOf(stats: PlayerMatchStats): number {
  return stats.hits / Math.max(1, stats.shotsFired);
}

export function survivalMsOf(stats: PlayerMatchStats, matchDurationMs: number): number {
  const survival = matchDurationMs - stats.downedMs;
  return survival > 0 ? survival : 0;
}
