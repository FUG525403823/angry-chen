import { createMatchStatePlayer } from '@ac/shared';
import { describe, expect, it } from 'vitest';

import { allPlayersReady, hostPidOf, playerCountOf, readyCountOf } from './roster.ts';

function player(pid: number, ready: boolean) {
  const entry = createMatchStatePlayer();
  entry.pid = pid;
  entry.name = 'p' + String(pid);
  entry.ready = ready;
  return entry;
}

describe('房间名册派生值', () => {
  it('房主是最小存活 pid（协议帧不含 hostId）', () => {
    expect(hostPidOf([player(3, false), player(1, false), player(2, false)])).toBe(1);
    expect(hostPidOf([player(0, false)])).toBe(0);
    expect(hostPidOf([])).toBe(0);
  });

  it('统计人数与准备人数', () => {
    const players = [player(1, true), player(2, false), player(3, true)];
    expect(playerCountOf(players)).toBe(3);
    expect(readyCountOf(players)).toBe(2);
    expect(allPlayersReady(players)).toBe(false);
  });

  it('全员准备才算可开始，空房不算', () => {
    expect(allPlayersReady([player(1, true), player(2, true)])).toBe(true);
    expect(allPlayersReady([player(1, false)])).toBe(false);
    expect(allPlayersReady([])).toBe(false);
  });
});
