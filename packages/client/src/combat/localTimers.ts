import { SERVER_TICK_MS } from '@ac/shared';

/** O07：本地倒计时（换弹 / 狂暴）——每帧推进 + 权威重同步。返回值对象被复用。 */
export interface LocalTimersView {
  reloadLeftMs: number;
  rageLeftMs: number;
}

export interface LocalTimers {
  /** 权威换弹剩余（`reloadLeft10Ms`，单位 0.01s）到达时对齐。 */
  resyncReload(reloadLeft10Ms: number, reloadMs: number): void;
  /** 权威狂暴剩余（`rageLeft100Ms`，单位 0.1s）到达时对齐。 */
  resyncRage(rageLeft100Ms: number): void;
  /** 每帧推进；下限 0。 */
  advance(dtMs: number): LocalTimersView;
}

/**
 * 对齐规则（O07 §5 冻结）：取「更紧急」的一方 `min(本地, 权威 + 单帧容差)`；
 * 本地已归零时直接采用权威值（新一轮换弹/狂暴），因此权威值不会让本地剩余变大，也不会有负值。
 */
export function createLocalTimers(): LocalTimers {
  const view: LocalTimersView = { reloadLeftMs: 0, rageLeftMs: 0 };

  function align(localMs: number, authorityMs: number): number {
    if (authorityMs <= 0) return 0;
    if (localMs <= 0) return authorityMs;
    const capped = authorityMs + SERVER_TICK_MS;
    return localMs < capped ? localMs : capped;
  }

  return {
    resyncReload(reloadLeft10Ms: number, reloadMs: number): void {
      const authority = Math.max(0, Math.min(reloadMs, reloadLeft10Ms * 10));
      view.reloadLeftMs = align(view.reloadLeftMs, authority);
    },
    resyncRage(rageLeft100Ms: number): void {
      view.rageLeftMs = align(view.rageLeftMs, Math.max(0, rageLeft100Ms * 100));
    },
    advance(dtMs: number): LocalTimersView {
      const step = Number.isFinite(dtMs) && dtMs > 0 ? dtMs : 0;
      view.reloadLeftMs = Math.max(0, view.reloadLeftMs - step);
      view.rageLeftMs = Math.max(0, view.rageLeftMs - step);
      return view;
    },
  };
}
