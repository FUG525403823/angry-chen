import { describe, expect, it } from 'vitest';

import { AMMO_SLOT_COUNT, createAmmoLedger } from './ammoLedger.ts';

describe('O07 弹药账本（按 ack 水位对账）', () => {
  it('①本地打 3 发、ack 未追上 → 显示值 = 服务器值 − 3（不回跳）', () => {
    const ledger = createAmmoLedger();
    ledger.reconcile(30, 120, 0);
    ledger.noteLocalShot(11, 0);
    ledger.noteLocalShot(12, 0);
    ledger.noteLocalShot(13, 0);
    const view = ledger.reconcile(30, 120, 0);
    expect(view.mag).toBe(27);
    expect(view.pending).toBe(3);
    expect(view.rejected).toBe(0);
    expect(view.overridden).toBe(false);
    const again = ledger.reconcile(30, 120, 0);
    expect(again.mag).toBe(27);
    expect(again.rejected).toBe(0);
  });

  it('②ack 追上且服务器弹药同步下降 → pending === 0、rejected === 0', () => {
    const ledger = createAmmoLedger();
    ledger.reconcile(30, 120, 0);
    for (const seq of [11, 12, 13]) ledger.noteLocalShot(seq, 0);
    expect(ledger.reconcile(30, 120, 0).mag).toBe(27);
    ledger.noteServerAck(13);
    const view = ledger.reconcile(27, 120, 0);
    expect(view.mag).toBe(27);
    expect(view.pending).toBe(0);
    expect(view.rejected).toBe(0);
    expect(view.overridden).toBe(false);
    expect(ledger.rejectedTotal).toBe(0);
  });

  it('③ack 追上但服务器弹药只降了 1 → rejected === 2 且显示值 = 权威值', () => {
    const ledger = createAmmoLedger();
    ledger.reconcile(30, 120, 0);
    for (const seq of [11, 12, 13]) ledger.noteLocalShot(seq, 0);
    expect(ledger.reconcile(30, 120, 0).pending).toBe(3);
    ledger.noteServerAck(13);
    const view = ledger.reconcile(29, 120, 0);
    expect(view.rejected).toBe(2);
    expect(view.overridden).toBe(true);
    expect(view.mag).toBe(29);
    expect(view.pending).toBe(0);
    expect(ledger.rejectedTotal).toBe(2);
  });

  it('④换弹完成（服务器弹药上升）→ 清空 pending、显示值 = 权威值', () => {
    const ledger = createAmmoLedger();
    ledger.reconcile(4, 120, 0);
    ledger.noteLocalShot(21, 0);
    ledger.noteLocalShot(22, 0);
    expect(ledger.reconcile(4, 120, 0).mag).toBe(2);
    const view = ledger.reconcile(30, 120, 0);
    expect(view.mag).toBe(30);
    expect(view.pending).toBe(0);
    expect(view.rejected).toBe(0);
    expect(view.reserve).toBe(120);
  });

  it('⑤命令缓冲溢出 → 计数并钳到权威值，不出现负弹药', () => {
    const ledger = createAmmoLedger(4);
    ledger.reconcile(5, 120, 0);
    for (let seq = 1; seq <= 9; seq += 1) ledger.noteLocalShot(seq, 0);
    const view = ledger.reconcile(5, 120, 0);
    expect(view.rejected).toBeGreaterThan(0);
    expect(view.overridden).toBe(true);
    expect(view.mag).toBe(5);
    expect(view.pending).toBe(0);
    expect(view.mag).toBeGreaterThanOrEqual(0);
    expect(ledger.rejectedTotal).toBeGreaterThan(0);
  });

  it('附加：ack 回退被忽略、非当前槽位的开火不污染显示值', () => {
    const ledger = createAmmoLedger();
    ledger.reconcile(30, 120, 0);
    ledger.noteServerAck(50);
    ledger.noteServerAck(40);
    ledger.noteLocalShot(51, 1);
    expect(ledger.reconcile(30, 120, 0).mag).toBe(30);
    expect(ledger.reconcile(30, 120, 1).mag).toBe(29);
    ledger.reset();
    expect(ledger.reconcile(7, 60, 0).pending).toBe(0);
    expect(AMMO_SLOT_COUNT).toBe(3);
  });
});
