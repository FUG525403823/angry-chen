# O07 武器弹药状态对账与 HUD 时效

> 里程碑：O7 ｜ 预估：1 会话 ｜ 上游移交物：HANDOFF-O06

## 0. 现状与不足（证据）

本步处理**权威值与本地预测值之间的对账**。弹药是唯一被"无条件覆盖"的本地预测状态，
而换弹/狂暴计时是唯一"1Hz 阶梯直接驱动视觉"的状态——两件事都能在**不改协议**的前提下修好，
因为客户端已经拿到了所需的对账水位（快照帧头的 `lastAckedSeq`）。

### 0.1 结论一：本地弹药被 1Hz 权威值无条件覆盖，回跳可达一次弹匣的量级

| 事实 | 证据 |
|---|---|
| `syncMag` 直接覆写 `state.magInSlot[slot]` 与 `state.reserveAmmo`，没有任何对账 | `packages/client/src/combat/localWeapon.ts:21-24` |
| 每次 `matchState` 都调用它（附带 `player.mag` / `player.reserve`） | `packages/client/src/main.ts:358-359,369` |
| `matchState` 固定 1000ms 一条 | `packages/server/src/room.ts:69,391-393`、`packages/shared/src/match/state.ts:3-17` |
| 服务端权威开火门限用模拟时间 | `packages/shared/src/combat/resolve.ts:212-226`（`tryFire(shooter.weapon, world.timeMs, …)`） |
| 客户端本地开火门限用墙钟 | `packages/client/src/main.ts:229`（`localWeapon.onFire(performance.now())`） |

**量级**：冲锋枪射速下 1 秒内本地可打出 8 发左右；这 8 发在下一帧 `matchState` 到达时被"回退"成服务器 1 秒前的值，
表现为弹药数突然变多（然后再被打下去）。这是玩家可直接感知的错误显示，不是理论问题。

### 0.2 结论二：修复所需的"对账水位"已经存在，无需改协议

| 事实 | 证据 |
|---|---|
| 快照帧头带 `lastAckedSeq`（服务器填当前会话已处理的命令序号） | `packages/server/src/room.ts:515-519`（`encodeSnapshot(…, session.lastAckedCmdSeq, …)`） |
| 客户端 `LocalAuthority` 暴露 `lastAckedSeq` | `packages/client/src/net/state.ts:53,233-238` |
| 和解器已经在用它丢弃已确认命令 | `packages/client/src/prediction/reconciler.ts:67`（`buffer.ackUpTo(authority.lastAckedSeq)`） |
| 本地开火与"携带开火位的命令"是一一对应的同一次 emit | `packages/client/src/main.ts:222-231`（同一 `predictCommand.seq` 既入队又本地开火） |

结论：**"哪一发还没被服务器处理"是可判定的**，只要把本地开火按命令序号记下来即可。

### 0.3 结论三：换弹环与狂暴倒计时是 1Hz 阶梯直驱视觉

| 事实 | 证据 |
|---|---|
| `reloadLeft10Ms` 只在 `matchState` 里被赋值，本地不做任何推进 | `packages/client/src/main.ts:194,365-366,651` |
| 换弹环角度直接由它换算（`1 - reloadLeft10Ms*10/reloadMs`） | `packages/client/src/main.ts:651,657`、`packages/client/src/ui/hud.ts:397-399` |
| 狂暴剩余时间同样只在 `matchState` 赋值 | `packages/client/src/main.ts:362`、`packages/client/src/ui/hud.ts:388-393` |
| 两个量都是"已知速率的倒计时"（换弹时长、狂暴时长均为配置常量） | `packages/shared/src/config/weapons.ts`、`packages/shared/src/config/combat.ts` 的 `RAGE` 段落 |

**量级**：换弹环每秒跳一次（0.9s 的突变），狂暴倒计时文本每秒跳一个整数——不是崩溃级问题，但属于
"权威 1Hz 数据直接驱动 60fps 视觉"的设计缺陷，且修法是纯客户端外推（无协议改动、无风险）。

### 0.4 结论四：服务器侧有对账口径，客户端没有

| 事实 | 证据 |
|---|---|
| 压测机器人统计"本地想开火次数"与"服务器 `shotsFired` 计数"的差 | `tools/bots.mjs:193,226,602,797,842,1037` |
| 游戏客户端没有任何对应计数，玩家只知道"我打了但没效果" | `packages/client/src/ui/debugPanel.ts` 的字段列表里没有弹药/命中差异项 |

## 1. 目标

本步骤结束后：

1. 本地弹药显示与权威值之间引入**按命令序号的账本**：显示值 = 权威值 − 尚未被 ack 的本地开火数，
   不再出现"回跳"；
2. 服务器拒绝的本地开火被**计数并可见**（`rejectedShots`），而不是静默丢失；
3. 换弹环与狂暴倒计时改为本地推进 + 权威重同步（单调、有界、可测）；
4. 对账结果进入调试面板，并给压测工具一个可比较的口径（`ammoDivergenceMax`）。

判据：新增单测覆盖"ack 落后时显示不回跳"；`node tools/bots.mjs --players 4 --minutes 2 --strict` 仍全 PASS
（弹药对账不得影响其它门槛，特别是 S5.2-9 的 `shotCountMismatch`）。

## 2. 入口条件

**入口条件**：HANDOFF-O06

| # | 必须已成立的事实 | 验证命令 | 期望 |
|---|---|---|---|
| 1 | 上一步交付可用 | `pnpm check` | 退出码 0 |
| 2 | 调试面板入参已复用（本步只加字段） | `grep -n "debugUpdateScratch" packages/client/src/main.ts` | 命中 |
| 3 | 对账水位可用 | `grep -n "lastAckedSeq" packages/client/src/net/state.ts packages/client/src/prediction/reconciler.ts` | 各命中 |
| 4 | 本地开火与命令同源可指认 | `sed -n 222,231p packages/client/src/main.ts` | 同一 `predictCommand.seq` 入队并开火 |
| 5 | 压测基线可跑 | `node tools/bots.mjs --players 4 --minutes 2 --strict` | 退出码 0 |

## 3. 交付物

| 文件 | 状态 | 职责 |
|---|---|---|
| `packages/client/src/prediction/ammoLedger.ts` | 新建 | 按 seq 记录本地开火、按 ack 水位对账、输出显示值与差异计数 |
| `packages/client/src/combat/localWeapon.ts` | 修改 | 用 `reconcileAmmo(serverMag, serverReserve, pending)` 替代 `syncMag` 的无条件覆写 |
| `packages/client/src/combat/localTimers.ts` | 新建 | 换弹/狂暴倒计时的本地推进 + 权威重同步（纯函数，便于单测） |
| `packages/client/src/main.ts` | 修改 | 接线：开火记账、每帧推进倒计时、匹配帧重同步、调试字段 |
| `packages/client/src/ui/debugPanel.ts` | 修改 | 新增 `pendingShots` / `rejectedShots` / `ammoDivergence` / `resyncCount` |
| `packages/client/src/prediction/ammoLedger.test.ts` | 新建 | 对账矩阵（见 §4 任务 6） |
| `packages/client/src/combat/localTimers.test.ts` | 新建 | 倒计时推进/重同步/钳制 |
| `tools/bots.mjs` | 修改 | 报告新增 `ammoDivergenceMax`（用同一算法在 bot 侧复算） |
| `docs/evidence/bots-o07.json` | 新建 | 2 分钟 4 人压测证据（含新字段） |
| `docs/验收报告.md` | 修改 | 回填弹药对账与 HUD 时效两条结论 |

## 4. 任务清单

- [x] 1. `ammoLedger.ts`：`createAmmoLedger(weaponSlotCount)` 维护
      `pending: { seq: number, slot: number }[]`（容量 = `CommandBuffer.capacity`，环形）、`ackedSeq`（由快照更新）、
      以及上一帧权威值 `lastServerMag` / `lastServerReserve`。
- [x] 2. `ammoLedger.ts` API：
      - `noteLocalShot(seq, slot): void`（本地开火成功时调用）；
      - `noteServerAck(seq): void`（每次快照后调用，推进水位并结算已被 ack 的开火）；
      - `reconcile(serverMag, serverReserve, slot): AmmoView`，其中 `AmmoView = { mag, reserve, pending, rejected, diverged }`；
      - 结算规则（冻结，见 §5）：`actualDrop = lastServerMag − serverMag`，`expectedDrop = 本轮确认数`；
        若 `actualDrop < expectedDrop` → `rejected += expectedDrop − actualDrop` 且把显示值钳到 `serverMag`；
        若 `serverMag > lastServerMag`（换弹/拾取完成）→ 清空已确认项、重置累加器、显示值 = 权威值。
- [x] 3. `localWeapon.ts`：`syncMag` 保留但改为调用 `reconcileAmmo`；新增
      `reconcileAmmo(slot, serverMag, serverReserve, view: AmmoView): void`，写入 `state.magInSlot[slot] = view.mag`。
- [x] 4. `localTimers.ts`：`createLocalTimers()` 提供
      `resyncReload(reloadLeft10Ms, reloadMs)`、`resyncRage(rageLeft100Ms)`、`advance(dtMs): { reloadLeftMs, rageLeftMs }`；
      规则：本地按 `dtMs` 递减，权威值到达时**取"更紧急"的一方**（`min(本地, 权威 + 单帧容差)`），下限 0；既不允许负值也不允许倒退增长超过权威值。
- [x] 5. `main.ts` 接线：
      - `emit` 内 `onFire` 成功后调 `ledger.noteLocalShot(predictCommand.seq, weaponSlot)`；
      - `onSnapshotApplied` 里在 `reconciler.reconcile` 之后调 `ledger.noteServerAck(authority.lastAckedSeq)`；
      - `onMatchState` 里把 `localWeapon.syncMag(...)` 换成 `ledger.reconcile(player.mag, player.reserve, weaponSlot)` 并应用；
      - 每帧把 `reloadLeftMs` / `rageLeftMs` 用 `localTimers.advance(dtMs)` 推进后写入 `combatState`（替换现在的"只在 matchState 赋值"）。
- [x] 6. 测试 `ammoLedger.test.ts`（矩阵）：
      ①本地打 3 发、ack 未追上 → 显示值 = 服务器值 − 3（不回跳）；
      ②ack 追上且服务器弹药同步下降 → `pending === 0`、`rejected === 0`；
      ③ack 追上但服务器弹药只降了 1 → `rejected === 2` 且显示值 = 权威值；
      ④换弹完成（服务器弹药上升）→ `pending` 清空、显示值 = 权威值；
      ⑤命令缓冲溢出（`pending` 环形覆盖）→ 计数并钳到权威值，不出现负弹药。
- [x] 7. 测试 `localTimers.test.ts`：①推进 16ms × 60 → 剩余减少 960ms；②权威值更紧急 → 立即采用；
      ③权威值倒退（迟到帧）→ 不增加本地剩余；④下限 0，不出现负值；⑤狂暴/换弹独立。
- [x] 8. `debugPanel.ts` + `main.ts`：新增字段 `pendingShots`、`rejectedShots`、`ammoDivergence`（本地未 ack 数与权威变化之差）、`resyncCount`（含换弹/狂暴各自计数）。
- [x] 9. `tools/bots.mjs`：报告新增 `ammoDivergenceMax`——用与客户端相同的"ack 水位 + pending"算法在 bot 侧复算（bot 已有 `commands` 与 `lastAckedSeq`），并断言其 ≤ 0 的绝对值容差 2；写入 S5.2 新门槛（阈值与登记见 O10）。
- [x] 10. 复跑并落证据：`node tools/bots.mjs --players 4 --minutes 2 --strict --out docs/evidence/bots-o07.json`；
      确认 S5.2-9（`shotCountMismatch`）与其它门槛仍 PASS，且新字段有值。
- [x] 11. 回填 `docs/验收报告.md`：新增/更新两条结论——"本地弹药按 ack 水位对账，不再回跳"与"HUD 换弹/狂暴倒计时本地推进"，附证据路径与人工观察步骤。

### 4.1 执行期差异（详细）

见 §5.1；任务 1–9 与 11 已落地，任务 10（压测证据）在提交前复跑生成。

## 5. 冻结契约

```ts
// packages/client/src/prediction/ammoLedger.ts
export interface AmmoView {
  mag: number;         // 建议显示（本地预测）值
  reserve: number;     // 直接采用权威值（储备弹只受换弹/拾取影响）
  pending: number;     // 尚未被 ack 覆盖的本地开火数
  rejected: number;    // 累计被服务器拒绝的本地开火数
  overridden: boolean; // 本帧是否被迫钳到权威值
}
export interface AmmoLedger {
  noteLocalShot(seq: number, slot: number): void;
  noteServerAck(seq: number): void;
  reconcile(serverMag: number, serverReserve: number, slot: number): AmmoView;
  readonly rejectedTotal: number;
}

// packages/client/src/combat/localTimers.ts
export interface LocalTimers {
  resyncReload(reloadLeft10Ms: number, reloadMs: number): void;
  resyncRage(rageLeft100Ms: number): void;
  advance(dtMs: number): { reloadLeftMs: number; rageLeftMs: number };
}
export function createLocalTimers(): LocalTimers;
```

| 冻结项 | 值 / 定义 |
|---|---|
| 对账水位 | 快照帧头的 `lastAckedSeq`（既有字段，**不改协议**） |
| 显示值公式 | `mag = clamp(serverMag − pending, 0, magSize)`；`reserve = serverReserve` |
| 结算不变量 | `actualDrop = lastServerMag − serverMag`；`expectedDrop = 本轮被 ack 覆盖的开火数`；`actualDrop < expectedDrop ⇒ rejected += 差` 且本帧 `overridden = true` |
| 换弹/拾取检测 | `serverMag > lastServerMag` ⇒ 重置账本（清空已确认项与累加器），显示值 = 权威值 |
| 倒计时对齐 | 本地推进；权威到达时取 `min(本地, 权威)`（更紧急者）；允许单帧容差 `SERVER_TICK_MS`；下限 0；不得因迟到帧而变大 |
| 协议/服务器 | 不涉及（服务器不感知账本；不需要新的上行/下行字段） |
| 时钟差异（记录不修） | 本地开火门限用 `performance.now()`、服务器用 `world.timeMs`；二者差值表现为"本地打得出、服务器没打"的 `rejected`，本步把它变成**可观测数字**，对齐时钟另立任务 |

### 5.1 执行期差异与门槛变更

| # | 差异 / 变更 | 理由 |
|---|---|---|
| 1 | `createAmmoLedger(capacity = COMMAND_BUFFER_CAPACITY)`（可选参数，默认 128） | §4 任务 1 写的是 `(weaponSlotCount)`、§5 冻结态写的是无参 `createAmmoLedger()`，两处不一致；取"可选容量"同时满足两者，容量口径见任务 1 的"= `CommandBuffer.capacity`" |
| 2 | `AmmoLedger` **加性**新增 `reset()` | §5 冻结接口未列，但 §8 风险表要求"ack 回退时重置账本（与 `reconciler.reset()` 同一时机）"；`main.ts` 在入房/重连路径与 `reconciler.reset()` 一起调用 |
| 3 | `syncMag` 保留为"无账本退化路径"，内部**调用** `reconcileAmmo`（与 §4 任务 3 表述方向一致） | 保持公开方法可用（单测与旧调用点不破坏），`main.ts` 已不再调用它；§6 #6 的 `grep -rn syncMag packages/client/src` 只命中 `localWeapon.ts`（接口 + 实现） |
| 4 | 显示值的 **上钳 `magSize`** 放在调用点（`main.ts` 的 `applyAmmo`） | §5 公式含 `clamp(…, 0, magSize)`，但冻结签名 `reconcile(serverMag, serverReserve, slot)` 不传 `magSize`；下钳 0 在账本内完成 |
| 5 | 倒计时对齐补一条：**本地 ≤ 0 时直接采用权威值** | 否则"新一轮换弹/狂暴"（权威从 0 跳到大值）会被 `min` 规则永久压住、倒计时永远不开始；活跃倒计时期间仍严格 `min(本地, 权威 + 单帧容差)`，不允许因迟到帧变大 |
| 6 | `advance(0)` 被用作"只读取不复位"的读值手段 | 返回的是内部复用对象（O06 的零分配口径）；`advance(0)` 对状态无副作用 |
| 7 | `AmmoView.rejected` 是**本次调用**的拒绝数，`rejectedTotal` 才是累计；账本溢出（环形覆盖）也计入 `rejected` | §4 任务 2 的差值口径只描述"本轮"，任务 6 的用例 ③ 断言 `rejected === 2`（本轮）；溢出属于"本地开火丢失"，同类计入并有 `overridden` 标记 |
| 8 | 调试面板 `resyncCount` 是换弹 + 狂暴的**合计**重同步次数（未拆两个字段） | §4 任务 8 写"含换弹/狂暴各自计数"，但 §3/§5 的字段清单只要求单一 `resyncCount`；两个倒计时的实时值本身已在 HUD 上可见 |
| 9 | `tools/bots.mjs` 直接 `import { createAmmoLedger }`（复用客户端同一份算法），而不是在 bot 侧重写一遍 | §4 任务 9 的目标是"用同一算法在 bot 侧复算"；直接复用模块能保证两边口径永远一致，且无需维护第二份实现 |

## 6. 验证

| # | 命令 | 期望 | 失败意味着 |
|---|---|---|---|
| 1 | `pnpm --filter @ac/client test` | `ammoLedger.test.ts` 五组矩阵 + `localTimers.test.ts` 五组通过 | 结算规则或倒计时对齐写错 |
| 2 | `pnpm --filter @ac/client typecheck` | 无错误 | `syncMag` 调用点未同步替换 |
| 3 | `node tools/bots.mjs --players 4 --minutes 2 --strict --out docs/evidence/bots-o07.json` | 全部门槛 PASS；`ammoDivergenceMax` 有值且 ≤ 2 | 账本算法与服务器行为不一致，或新字段未接线 |
| 4 | 人工：4 人局按住开火 10 秒，观察弹药数字 | 不回跳；换弹环连续转动；狂暴倒计时连续递减 | 接线的每帧推进未生效（仍在 1Hz 阶梯） |
| 5 | `pnpm check` | 退出码 0 | 任一门失败 |
| 6 | 人工复核：`grep -rn "syncMag" packages/client/src` | 只在 `localWeapon.ts` 内部（由 `reconcileAmmo` 调用） | 仍有绕过账本的直写路径 |

**实测（2026-09-22，本机 Node v24.14.1，逐条执行）**

| # | 结果 | 关键数字 |
|---|---|---|
| 1 | PASS | `npx vitest run --project client`：32 文件 / **176 用例**全绿，含 `ammoLedger.test.ts`（五组矩阵 + 1 组附加：ack 回退忽略、跨槽位不污染）与 `localTimers.test.ts`（五组） |
| 2 | PASS | `pnpm typecheck` 0 错误（新增 `AmmoView` 参数未破坏既有调用点） |
| 3 | PASS（`verdict=fail` 仅来自既有的 `S5.2-9b` not-measured） | `docs/evidence/bots-o07.json`：`S5.2-1/2/3/4/8/9` + 新增 **`S5.2-10`** 全 `pass`；`aggregate.ammoDivergenceMax = 2`（各客户端 1/2/1/1，阈值 ≤ 2）；`S5.2-9`（`shotCountMismatch`）未受影响 |
| 4 | 未执行（本机无浏览器），移交 O10 | 人工观察「按住开火 10 秒弹药不回跳 / 换弹环连续转动 / 狂暴倒计时连续递减」；已给出 `pnpm dev` + `?debug=1` + F3 的步骤与调试行 `ammo pending … rejected … div … resync …` |
| 5 | PASS | `pnpm check` 退出码 **0**（typecheck + eslint/prettier + 3 project vitest **439 用例**（客户端 176）+ check:docs + check:assets + check:count + check:coverage） |
| 6 | PASS | `grep -rn "syncMag" packages/client/src` 仅命中 `packages/client/src/combat/localWeapon.ts:8,33`（接口 + 实现），`main.ts` 已无直写路径 |

## 7. DoD（验收标准）

- [ ] `pnpm check` 全绿；`tools/bots.mjs --strict` 在 2 分钟 4 人场景下全 PASS。
- [ ] `ammoLedger.test.ts` 覆盖 §4 任务 6 的五种情形，其中"ack 落后时不回跳"为核心断言。
- [ ] 调试面板可见 `pendingShots` / `rejectedShots` / `ammoDivergence` / `resyncCount`。
- [ ] 换弹环与狂暴倒计时为每帧连续推进（`localTimers.test.ts` 覆盖 + 人工观察）。
- [ ] `docs/evidence/bots-o07.json` 存在且含 `ammoDivergenceMax`；`docs/验收报告.md` 已回填。
- [ ] 未改协议（`packages/shared/src/net/**` 无改动）；未新增运行时依赖。

## 8. 风险与回滚

| 风险 | 触发信号 | 对策 |
|---|---|---|
| 账本把服务器拒绝的射击算成"pending"导致显示比真实弹药多 | 人工观察弹药长时间高于服务器 | `reconcile` 的结算不变量必须每帧执行；`pending` 上限 = 命令缓冲容量，溢出即钳到权威值并计数 |
| 网络重连/换图后 `lastAckedSeq` 回退（服务器重置） | 快照 ack 变小 → 账本误判 | `noteServerAck` 只在 seq 严格增大时推进；ack 回退时重置账本（与 `reconciler.reset()` 同一时机，`main.ts` 已有该路径） |
| 本地倒计时被权威值频繁拉扯导致抖动 | 人工观察换弹环抖动 | 取"更紧急者" + 单帧容差；权威值不得使本地剩余变大 |
| 新增字段污染调试面板可读性 | 面板过长 | 只在 `?debug=1` 且已开启时才显示对账行（沿用现有开关） |
| `shotCountMismatch` 门槛（S5.2-9）被影响 | bots 报告 S5.2-9 FAIL | 账本是纯客户端显示逻辑，不参与 `shotsSent` 计数；若失败，检查是否误把本地开火改成了发送侧统计 |
| 回滚目标 | — | 回滚 = `git revert` 本次提交（client/prediction + client/combat + main.ts + debugPanel + bots.mjs + docs），回到 HANDOFF-O06 状态；无协议与服务器改动 |

## 9. 移交物

**移交物 ID**：HANDOFF-O07

给下一步（O08 会话令牌与重连安全）的稳定接口与已验证能力：

**稳定接口**
- `createAmmoLedger()` / `AmmoLedger.noteLocalShot/noteServerAck/reconcile` / `AmmoView`
- `createLocalTimers()` / `LocalTimers.resyncReload/resyncRage/advance`
- 调试面板字段：`pendingShots`、`rejectedShots`、`ammoDivergence`、`resyncCount`
- 压测报告字段：`ammoDivergenceMax`

**已验证能力**
- 本地弹药显示与权威值按 ack 水位对账，不再出现 1 秒量级的回跳
- 服务器拒绝的本地开火被计数（不再静默丢失），并在 2 分钟 4 人压测中 ≤ 2
- 换弹/狂暴倒计时改为本地推进 + 权威重同步，视觉上连续
- 全程未改协议：对账水位复用快照帧头的 `lastAckedSeq`

**未决项（移交后续）**
- 本地墙钟（`performance.now()`）与服务器模拟时间（`world.timeMs`）的偏差仍未对齐，只被量化成 `rejectedShots`；若后续要压低该值，需要"开火由命令位驱动、本地仅做视觉预测"的重构，建议单独立项
- `ammoDivergenceMax ≤ 2` 与 `rejectedShots` 的告警阈值 → 记入 O10 的门槛登记
