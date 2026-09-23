import { describe, expect, it } from 'vitest';

import { AMMO_ACK_GRACE_SEQ, AMMO_SLOT_COUNT, createAmmoLedger } from './ammoLedger.ts';

describe('O07 弹药账本（显示值 = 权威值，闸门 = 权威 − 未确认开火）', () => {
  it('①本地打 3 发、权威未降 → 显示值仍是权威值，闸门收紧 3 发', () => {
    const ledger = createAmmoLedger();
    ledger.reconcile(30, 120, 0);
    ledger.noteLocalShot(11, 0);
    ledger.noteLocalShot(12, 0);
    ledger.noteLocalShot(13, 0);
    const view = ledger.reconcile(30, 120, 0);
    expect(view.mag).toBe(30);
    expect(view.gateMag).toBe(27);
    expect(view.pending).toBe(3);
    expect(view.rejected).toBe(0);
    expect(view.overridden).toBe(false);
    const again = ledger.reconcile(30, 120, 0);
    expect(again.mag).toBe(30);
    expect(again.gateMag).toBe(27);
    expect(again.rejected).toBe(0);
  });

  it('②ack 追上且权威同步下降 → pending === 0、rejected === 0', () => {
    const ledger = createAmmoLedger();
    ledger.reconcile(30, 120, 0);
    for (const seq of [11, 12, 13]) ledger.noteLocalShot(seq, 0);
    expect(ledger.reconcile(30, 120, 0).gateMag).toBe(27);
    ledger.noteServerAck(13);
    const view = ledger.reconcile(27, 120, 0);
    expect(view.mag).toBe(27);
    expect(view.gateMag).toBe(27);
    expect(view.pending).toBe(0);
    expect(view.rejected).toBe(0);
    expect(view.overridden).toBe(false);
    expect(ledger.rejectedTotal).toBe(0);
  });

  it('③ack 先到、权威弹匣后到（相邻两包）→ 不记被拒、显示不回跳（真人试玩：开枪时数字跳动）', () => {
    const ledger = createAmmoLedger();
    ledger.reconcile(30, 120, 0);
    ledger.noteLocalShot(11, 0);
    expect(ledger.reconcile(30, 120, 0).gateMag).toBe(29);
    // 快照包带着 ack（命令已被服务器处理），但比赛状态包里的权威弹匣还没到
    ledger.noteServerAck(11);
    const view = ledger.reconcile(30, 120, 0);
    expect(view.rejected).toBe(0);
    expect(view.overridden).toBe(false);
    expect(view.mag).toBe(30);
  });

  it('④权威只降 1 发 → 只消掉最早 1 发，其余保留 pending（不误判被拒）', () => {
    const ledger = createAmmoLedger();
    ledger.reconcile(30, 120, 0);
    for (const seq of [11, 12, 13]) ledger.noteLocalShot(seq, 0);
    expect(ledger.reconcile(30, 120, 0).pending).toBe(3);
    ledger.noteServerAck(13);
    const view = ledger.reconcile(29, 120, 0);
    expect(view.rejected).toBe(0);
    expect(view.pending).toBe(2);
    expect(view.mag).toBe(29);
    expect(view.gateMag).toBe(27);
  });

  it('⑤ack 远远越过且权威始终不降 → 过期计被拒，显示值仍等于权威值', () => {
    const ledger = createAmmoLedger();
    ledger.reconcile(30, 120, 0);
    ledger.noteLocalShot(11, 0);
    expect(ledger.reconcile(30, 120, 0).gateMag).toBe(29);
    ledger.noteServerAck(11 + AMMO_ACK_GRACE_SEQ + 1);
    const view = ledger.reconcile(30, 120, 0);
    expect(view.rejected).toBe(1);
    expect(view.overridden).toBe(true);
    expect(view.pending).toBe(0);
    expect(view.mag).toBe(30);
    expect(ledger.rejectedTotal).toBe(1);
  });

  it('⑥换弹完成（权威上升）→ 清空 pending、显示值 = 权威值', () => {
    const ledger = createAmmoLedger();
    ledger.reconcile(4, 120, 0);
    ledger.noteLocalShot(21, 0);
    ledger.noteLocalShot(22, 0);
    expect(ledger.reconcile(4, 120, 0).gateMag).toBe(2);
    const view = ledger.reconcile(30, 120, 0);
    expect(view.mag).toBe(30);
    expect(view.gateMag).toBe(30);
    expect(view.pending).toBe(0);
    expect(view.rejected).toBe(0);
    expect(view.reserve).toBe(120);
  });

  it('⑦命令缓冲溢出 → 计数并钳到权威值，不出现负弹药', () => {
    const ledger = createAmmoLedger(4);
    ledger.reconcile(5, 120, 0);
    for (let seq = 1; seq <= 9; seq += 1) ledger.noteLocalShot(seq, 0);
    const view = ledger.reconcile(5, 120, 0);
    expect(view.rejected).toBeGreaterThan(0);
    expect(view.overridden).toBe(true);
    expect(view.mag).toBe(5);
    expect(view.gateMag).toBe(5);
    expect(view.pending).toBe(0);
    expect(view.mag).toBeGreaterThanOrEqual(0);
    expect(ledger.rejectedTotal).toBeGreaterThan(0);
  });

  it('⑧显示值恒等于权威弹匣，未确认开火只收紧闸门', () => {
    const ledger = createAmmoLedger();
    ledger.reconcile(12, 120, 0);
    ledger.noteLocalShot(11, 0);
    const view = ledger.reconcile(12, 120, 0);
    expect(view.mag).toBe(12);
    expect(view.gateMag).toBe(11);
    expect(view.pending).toBe(1);
  });

  it('附加：ack 回退被忽略、非当前槽位的开火不污染显示值', () => {
    const ledger = createAmmoLedger();
    ledger.reconcile(30, 120, 0);
    ledger.noteServerAck(50);
    ledger.noteServerAck(40);
    ledger.noteLocalShot(51, 1);
    expect(ledger.reconcile(30, 120, 0).mag).toBe(30);
    expect(ledger.reconcile(30, 120, 1).gateMag).toBe(29);
    ledger.reset();
    expect(ledger.reconcile(7, 60, 0).pending).toBe(0);
    expect(AMMO_SLOT_COUNT).toBe(3);
  });
});
