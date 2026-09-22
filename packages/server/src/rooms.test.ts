import { describe, expect, it } from 'vitest';

import { NEW_ROOM_CODE, PROTOCOL_VERSION } from '@ac/shared';

import { createHarness } from './testing/harness.ts';

/** O08：宽限期重连的身份判定只认令牌（昵称不再参与）。 */
describe('O08 会话令牌与宽限期重连', () => {
  it('①令牌正确 → 复用 pid 与名额，graceReconnects 计数 +1', async () => {
    const harness = createHarness();
    const alice = harness.connect();
    alice.join('alice', NEW_ROOM_CODE);
    const welcome = alice.welcome();
    const code = welcome?.roomCode ?? '';
    const pid = welcome?.pid ?? 0;
    const token = welcome?.token ?? '';
    expect(token).toMatch(/^[0-9a-f]{8}$/);
    const room = harness.game.rooms.rooms.get(code);
    if (room === undefined) throw new Error('room missing');

    alice.disconnect();
    harness.advance(2000);
    const back = harness.connect();
    back.join('alice', code, PROTOCOL_VERSION, token);
    expect(back.welcome()?.pid).toBe(pid);
    expect(room.sessions.length).toBe(1);
    expect(room.sessions[0]?.disconnectedAtMs).toBeNull();
    expect(harness.game.metrics.graceReconnects).toBe(1);
    await harness.close();
  });

  it('②令牌错误 → 不匹配（按新玩家入房，老会话仍在宽限期）', async () => {
    const harness = createHarness();
    const alice = harness.connect();
    alice.join('alice', NEW_ROOM_CODE);
    const code = alice.welcome()?.roomCode ?? '';
    const pid = alice.welcome()?.pid ?? 0;
    const room = harness.game.rooms.rooms.get(code);
    if (room === undefined) throw new Error('room missing');

    alice.disconnect();
    harness.advance(2000);
    const impostor = harness.connect();
    impostor.join('alice', code, PROTOCOL_VERSION, 'deadbeef');
    const welcome = impostor.welcome();
    expect(welcome?.pid).not.toBe(pid);
    expect(room.sessions.length).toBe(2);
    expect(harness.game.metrics.graceReconnects).toBe(0);
    await harness.close();
  });

  it('③无令牌 → 不匹配（即使昵称相同）', async () => {
    const harness = createHarness();
    const alice = harness.connect();
    alice.join('alice', NEW_ROOM_CODE);
    const code = alice.welcome()?.roomCode ?? '';
    const pid = alice.welcome()?.pid ?? 0;
    const room = harness.game.rooms.rooms.get(code);
    if (room === undefined) throw new Error('room missing');

    alice.disconnect();
    harness.advance(2000);
    const impostor = harness.connect();
    impostor.join('alice', code, PROTOCOL_VERSION, '');
    expect(impostor.welcome()?.pid).not.toBe(pid);
    expect(harness.game.metrics.graceReconnects).toBe(0);
    await harness.close();
  });

  it('④宽限期过期后即使用正确令牌也不再回座（按新玩家入房）', async () => {
    const harness = createHarness();
    const alice = harness.connect();
    alice.join('alice', NEW_ROOM_CODE);
    const code = alice.welcome()?.roomCode ?? '';
    const token = alice.welcome()?.token ?? '';
    const room = harness.game.rooms.rooms.get(code);
    if (room === undefined) throw new Error('room missing');

    alice.disconnect();
    harness.advance(31000);
    expect(room.sessions.length).toBe(0);

    const back = harness.connect();
    back.join('alice', code, PROTOCOL_VERSION, token);
    const welcome = back.welcome();
    expect(welcome).toBeDefined();
    expect(welcome?.token).not.toBe(token);
    expect(harness.game.metrics.graceReconnects).toBe(0);
    expect(room.sessions.length).toBe(1);
    await harness.close();
  });
});
