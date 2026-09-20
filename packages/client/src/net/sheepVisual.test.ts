import { SHEEP_STATE } from '@ac/shared';
import { describe, expect, it } from 'vitest';

import {
  SHEEP_VISUAL_FORM,
  createSheepVisualTracker,
  isWindupState,
  sheepFormFromState,
} from './sheepVisual.ts';

describe('羊形解码（快照里没有羊形字段）', () => {
  it('羊王阶段与问界羊远程状态可被识别', () => {
    expect(sheepFormFromState(SHEEP_STATE.kingPhase1)).toBe(SHEEP_VISUAL_FORM.king);
    expect(sheepFormFromState(SHEEP_STATE.kingSummoning)).toBe(SHEEP_VISUAL_FORM.king);
    expect(sheepFormFromState(SHEEP_STATE.ranged)).toBe(SHEEP_VISUAL_FORM.elite);
    expect(sheepFormFromState(SHEEP_STATE.graze)).toBeUndefined();
  });

  it('冲锋羊由 windup/charge 推断，且只有 windup 触发预警', () => {
    expect(sheepFormFromState(SHEEP_STATE.windup)).toBe(SHEEP_VISUAL_FORM.ram);
    expect(sheepFormFromState(SHEEP_STATE.charge)).toBe(SHEEP_VISUAL_FORM.ram);
    expect(isWindupState(SHEEP_STATE.windup)).toBe(true);
    expect(isWindupState(SHEEP_STATE.charge)).toBe(false);
  });

  it('推断结果按实体记忆并只升不降，死亡后忘记', () => {
    const tracker = createSheepVisualTracker();
    expect(tracker.resolve(9, SHEEP_STATE.graze).form).toBe(SHEEP_VISUAL_FORM.grunt);
    expect(tracker.size).toBe(0);
    expect(tracker.resolve(9, SHEEP_STATE.ranged).form).toBe(SHEEP_VISUAL_FORM.elite);
    expect(tracker.resolve(9, SHEEP_STATE.chase).form).toBe(SHEEP_VISUAL_FORM.elite);
    expect(tracker.resolve(9, SHEEP_STATE.kingPhase2).form).toBe(SHEEP_VISUAL_FORM.king);
    expect(tracker.size).toBe(1);
    tracker.forget(9);
    expect(tracker.size).toBe(0);
    expect(tracker.resolve(9, SHEEP_STATE.graze).form).toBe(SHEEP_VISUAL_FORM.grunt);
  });

  it('windup 标记随状态逐帧返回', () => {
    const tracker = createSheepVisualTracker();
    expect(tracker.resolve(3, SHEEP_STATE.chase).windup).toBe(false);
    expect(tracker.resolve(3, SHEEP_STATE.windup).windup).toBe(true);
    expect(tracker.resolve(3, SHEEP_STATE.charge).windup).toBe(false);
    tracker.reset();
    expect(tracker.size).toBe(0);
  });
});
