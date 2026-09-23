import { describe, expect, it } from 'vitest';

import {
  AMMO_ACK_GRACE_SEQ,
  AMMO_IDLE_HEAL_MS,
  AMMO_SLOT_COUNT,
  createAmmoLedger,
} from './ammoLedger.ts';

/** 本地开火与对账都发生在同一帧附近（间隔远小于 AMMO_IDLE_HEAL_MS）。 */
const T0 = 1000;
const FRAME = T0 + 16;
/** 停火足够久（超过自愈阈值）。 */
const IDLE = T0 + AMMO_IDLE_HEAL_MS + 100;

describe('O07 弹药账本（显示值 = 权威值 − 未确认开火；开火当帧就扣、只降不升）', () => {
  it('①本地开火当帧就扣：显示值立刻从 30 掉到 29，不等服务器往返', () => {
    const ledger = createAmmoLedger();
    expect(ledger.reconcile(30, 120, 0, T0).mag).toBe(30);
    ledger.noteLocalShot(11, 0, T0);
    const view = ledger.reconcile(30, 120, 0, FRAME);
    expect(view.mag).toBe(29);
    expect(view.gateMag).toBe(29);
    expect(view.pending).toBe(1);
    expect(view.rejected).toBe(0);
  });

  it('②连打 3 发（权威还没到）→ 显示 27，权威随后下降时不再二次下降', () => {
    const ledger = createAmmoLedger();
    ledger.reconcile(30, 120, 0, T0);
    for (const seq of [11, 12, 13]) ledger.noteLocalShot(seq, 0, T0);
    expect(ledger.reconcile(30, 120, 0, FRAME).mag).toBe(27);
    ledger.noteServerAck(13);
    const view = ledger.reconcile(27, 120, 0, FRAME);
    expect(view.mag).toBe(27);
    expect(view.gateMag).toBe(27);
    expect(view.pending).toBe(0);
    expect(view.rejected).toBe(0);
    expect(ledger.rejectedTotal).toBe(0);
  });

  it('③被服务器丢掉一发 → 开火过程中数字不回升，停火后静默回到权威值', () => {
    const ledger = createAmmoLedger();
    ledger.reconcile(30, 120, 0, T0);
    ledger.noteLocalShot(11, 0, T0);
    expect(ledger.reconcile(30, 120, 0, FRAME).mag).toBe(29);
    // 服务器收下了这条命令（ack 越过水位）但权威弹匣没降 ⇒ 判为被拒
    ledger.noteServerAck(11 + AMMO_ACK_GRACE_SEQ + 1);
    const bounced = ledger.reconcile(30, 120, 0, T0 + 200);
    expect(bounced.rejected).toBe(1);
    expect(bounced.pending).toBe(0);
    expect(bounced.mag, '被拒不许把数字弹回 30（真人试玩：开枪时子弹数跳动）').toBe(29);
    // 停火超过自愈阈值后，允许悄悄回到权威值，避免长期与权威不符
    expect(ledger.reconcile(30, 120, 0, IDLE).mag).toBe(30);
    expect(ledger.rejectedTotal).toBe(1);
  });

  it('④权威只降 1 发 → 只消掉最早 1 发，其余保留 pending', () => {
    const ledger = createAmmoLedger();
    ledger.reconcile(30, 120, 0, T0);
    for (const seq of [11, 12, 13]) ledger.noteLocalShot(seq, 0, T0);
    expect(ledger.reconcile(30, 120, 0, FRAME).pending).toBe(3);
    ledger.noteServerAck(13);
    const view = ledger.reconcile(29, 120, 0, FRAME);
    expect(view.rejected).toBe(0);
    expect(view.pending).toBe(2);
    expect(view.mag).toBe(27);
    expect(view.gateMag).toBe(27);
  });

  it('⑤ack 先到、权威弹匣后到（相邻两包）→ 不记被拒、数字不回跳', () => {
    const ledger = createAmmoLedger();
    ledger.reconcile(30, 120, 0, T0);
    ledger.noteLocalShot(11, 0, T0);
    expect(ledger.reconcile(30, 120, 0, FRAME).mag).toBe(29);
    ledger.noteServerAck(11);
    const view = ledger.reconcile(30, 120, 0, FRAME);
    expect(view.rejected).toBe(0);
    expect(view.overridden).toBe(false);
    expect(view.mag).toBe(29);
  });

  it('⑥换弹完成（权威上升）→ 清空 pending，显示值跟着权威一起上升', () => {
    const ledger = createAmmoLedger();
    ledger.reconcile(4, 120, 0, T0);
    ledger.noteLocalShot(21, 0, T0);
    ledger.noteLocalShot(22, 0, T0);
    expect(ledger.reconcile(4, 120, 0, FRAME).mag).toBe(2);
    const view = ledger.reconcile(30, 120, 0, FRAME);
    expect(view.mag).toBe(30);
    expect(view.gateMag).toBe(30);
    expect(view.pending).toBe(0);
    expect(view.rejected).toBe(0);
    expect(view.reserve).toBe(120);
  });

  it('⑦命令缓冲溢出 → 计数并钳到权威值，不出现负弹药', () => {
    const ledger = createAmmoLedger(4);
    ledger.reconcile(5, 120, 0, T0);
    for (let seq = 1; seq <= 9; seq += 1) ledger.noteLocalShot(seq, 0, T0);
    const view = ledger.reconcile(5, 120, 0, FRAME);
    expect(view.rejected).toBeGreaterThan(0);
    expect(view.overridden).toBe(true);
    // 溢出意味着未确认开火被"赦免"（不再挂账），显示值退回权威值 5 —— 这也是安全方向：
    // 闸门跟着回到 5，玩家不会因为溢出而打出超过权威剩余的子弹。
    expect(view.mag).toBe(5);
    expect(view.gateMag).toBe(5);
    expect(view.pending).toBe(0);
    expect(view.mag).toBeGreaterThanOrEqual(0);
    expect(ledger.rejectedTotal).toBeGreaterThan(0);
  });

  it('⑧非当前槽位的开火不污染当前槽位显示值；ack 回退被忽略', () => {
    const ledger = createAmmoLedger();
    ledger.reconcile(30, 120, 0, T0);
    ledger.noteServerAck(50);
    ledger.noteServerAck(40);
    ledger.noteLocalShot(51, 1, T0);
    expect(ledger.reconcile(30, 120, 0, FRAME).mag).toBe(30);
    expect(ledger.reconcile(30, 120, 1, FRAME).mag).toBe(29);
    ledger.reset();
    expect(ledger.reconcile(7, 60, 0, T0).pending).toBe(0);
    expect(AMMO_SLOT_COUNT).toBe(3);
  });
});
