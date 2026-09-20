import type { MatchStatePlayer } from '@ac/shared';

export function playerCountOf(players: readonly MatchStatePlayer[]): number {
  let count = 0;
  for (let i = 0; i < players.length; i += 1) {
    const player = players[i];
    if (player !== undefined && player.pid > 0) count += 1;
  }
  return count;
}

export function readyCountOf(players: readonly MatchStatePlayer[]): number {
  let ready = 0;
  for (let i = 0; i < players.length; i += 1) {
    const player = players[i];
    if (player !== undefined && player.pid > 0 && player.ready) ready += 1;
  }
  return ready;
}

export function allPlayersReady(players: readonly MatchStatePlayer[]): boolean {
  const ready = readyCountOf(players);
  return ready > 0 && ready === playerCountOf(players);
}

export function hostPidOf(players: readonly MatchStatePlayer[]): number {
  let host = 0;
  for (let i = 0; i < players.length; i += 1) {
    const player = players[i];
    if (player === undefined || player.pid <= 0) continue;
    if (host === 0 || player.pid < host) host = player.pid;
  }
  return host;
}

export function selfPlayerOf(
  players: readonly MatchStatePlayer[],
  pid: number,
): MatchStatePlayer | undefined {
  for (let i = 0; i < players.length; i += 1) {
    const player = players[i];
    if (player !== undefined && player.pid === pid) return player;
  }
  return undefined;
}
