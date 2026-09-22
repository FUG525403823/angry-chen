# O02 服务器调度与 tick 抖动

> 里程碑：O2 ｜ 预估：1 会话 ｜ 上游移交物：HANDOFF-O01

## 0. 现状与不足（证据）

本步处理的是**性能与可观测性**问题：`ac_tick_jitter_ms_p95` 是压测门槛（阈值 ≤8ms），
但当前这个名字下的数**不是** tick 调度误差，而主要是操作系统定时器粒度；同时"跳 tick 后模拟时间永久落后"这一真问题没有任何指标能看见。

### 0.1 结论一：抖动样本测的是"定时器唤醒间隔"，与 tick 语义无关

| 事实 | 证据 |
|---|---|
| 房间轮询用 `setInterval(…, ROOM_LOOP_INTERVAL_MS)`，间隔常量 = 5ms | `packages/server/src/server.ts:11,93-96` |
| 抖动样本 = `monotonic − lastTickAtMs − SERVER_TICK_MS`，且只在一次 `updateRoom` 调用内的**首个** tick 采样（`firstInBurst = steps === 0`） | `packages/server/src/room.ts:422-427`、`packages/server/src/server.ts:73-75` |
| 采样环只有 128 个槽，`/metrics` 每次抓取都 `Array.from(...).sort()` | `packages/server/src/metrics.ts:2,110-116,127-130` |
| 本机实测定时器粒度 **15.56ms**（5ms 定时器被量化到 15.56ms） | `docs/evidence/bots-4p-5min.json` → `environment.timerGranularityMs = 15.56`、`tools/bots.mjs:731,1010` |
| 实测 P95 = 12.66–12.88ms（> 阈值 8ms） | `docs/evidence/bots-4p-5min.json`（`aggregate.tickJitterP95Ms = 12.66`、`serverMetrics.start.ac_tick_jitter_ms_p95 = 12.879`）、`docs/验收报告.md` §3.2 |

因为样本定义里没有"理想时刻"（`tickIndex × 50`）做参照，"粒度大但稳定"与"真的在漂"两种情形会给出同一个数。
`tools/bots.mjs:916-931` 甚至据此在报告里写了一句未经验证的断言："生产 Linux 上 5ms 循环可用，该值需在目标环境复测"——这是文档级猜测，不是测量。

### 0.2 结论二：没有"模拟时间 vs 真实时间"的漂移指标，而漂移是真实存在的

| 事实 | 证据 |
|---|---|
| 累积器按真实时间累积，每 50ms 走一个 tick：`accumulatorMs += elapsed`（:374）、`while (room.accumulatorMs >= SERVER_TICK_MS)`（:376） | `packages/server/src/room.ts:374-385` |
| 超过 `LIMITS.tickCatchUpLimit = 5` 时，**把多余的累积量直接丢掉**（`accumulatorMs -= skipped * SERVER_TICK_MS`） | `packages/server/src/room.ts:376-383`、`packages/shared/src/net/protocol.ts:126` |
| 只有 `tickSkips` 计数，没有"模拟时间 − 真实时间"偏差指标 | `packages/server/src/metrics.ts:5-10,63-68`（`ticks`/`tickSkips`/`tickJitter*`） |

一次 >250ms 的事件循环停顿会让模拟永久落后真实时间（每次停顿丢最多 250ms，之后不再补），
波次/波间/狂暴都按 `world.timeMs` 计时，所以"局内时间比真实时间慢"会稳定累积，但没有任何指标能发现。

### 0.3 结论三：所有房间在同一个循环里串行步进，无单房间预算与隔离

`for (const room of rooms.rooms.values()) updateRoom(roomDeps, room, nowMs)`（`packages/server/src/server.ts:73-75`）
没有每房间的工作量预算。一个房间的 tick 内工作包含 O(n²) 实体分离、射线判定、每会话快照编码与删除列表全扫
（见 O04/O05 的清单），任何一项抖动都会挤占其它房间的时间片；`ac_max_entities_observed` 与 `ac_tick_skips_total`
之外没有"每 tick 花了多少毫秒"的指标。

## 1. 目标

本步骤结束后，抖动与漂移**可分辨、可判定**：

1. `ac_tick_schedule_error_ms_p95` = 实际 tick 时刻与理想时刻（`房间首 tick 时刻 + n × 50ms`）之差，衡量调度质量；
2. `ac_sim_drift_ms` = 模拟时间与真实时间之差，必须**有界**（±1 tick 内），不得随运行时间累积；
3. `ac_tick_work_ms_p95/p99` = 单次 tick 的工作耗时，用来把"调度问题"与"计算问题"分开；
4. 房间轮询改为绝对时刻自校正调度，且单房间每次调用有工作量预算，不再独占事件循环。

判据：4 个无头机器人 5 分钟压测中，`ac_tick_schedule_error_ms_p95` ≤ 8ms；
若证据文件记录的定时器粒度 > 8ms（本机 Windows = 15.56ms），则以**替代判据**判定并通过：
「运行前 1/3 与后 1/3 区间的 schedule error p95 差值 ≤ 2ms 且 `ac_sim_drift_ms` 的绝对值全程 ≤ 50ms」。
替代判据必须写进证据文件并在报告 note 中注明依据（见 §4 任务 6）。

## 2. 入口条件

**入口条件**：HANDOFF-O01

| # | 必须已成立的事实 | 验证命令 | 期望 |
|---|---|---|---|
| 1 | 上一步交付可用 | `ls docs/优化/O01-*.md` 且 `pnpm check` | 文件存在、退出码 0 |
| 2 | 姿态校验不再污染延迟相关指标 | `curl -s localhost:8787/metrics \| grep ac_hard_correct_total` | 数分钟内增量 ≤5/分钟/人 |
| 3 | 现状抖动可复现 | `node tools/bots.mjs --players 4 --minutes 2 --out /tmp/bots-jitter-before.json` | 报告 `S5.2-4` 为 `fail`，`tickJitterP95Ms` > 8 |
| 4 | 定时器粒度可测 | `node tools/bots.mjs --help 2>&1 \| head -1` 后运行任意压测 | 报告 `environment.timerGranularityMs` 有值 |
| 5 | 跳 tick 无漂移指标 | `curl -s localhost:8787/metrics \| grep -c sim_drift` | 输出 `0`（证明指标尚不存在） |

## 3. 交付物

| 文件 | 状态 | 职责 |
|---|---|---|
| `packages/server/src/server.ts` | 修改 | 绝对时刻自校正调度替换 `setInterval(5ms)`；房间循环预算 |
| `packages/server/src/room.ts` | 修改 | `tickIdealAtMs` 记账；`runTick` 报 schedule error 与 work ms；累积器按预算让出 |
| `packages/server/src/metrics.ts` | 修改 | 新增 `tickScheduleError*`、`tickWork*`、`simDriftMs`；四条新 Prometheus 指标 |
| `packages/shared/src/net/protocol.ts` | 修改 | `LIMITS.roomTickBudgetMs = 8` |
| `packages/server/src/server.test.ts` | 修改 | 新增：用假 `now()` 驱动 1000 次 `step()`，断言模拟时间与真实时间偏差 ≤1 tick（调度不引入漂移） |
| `packages/server/src/metrics.test.ts` | 修改 | 新指标的分位数/漂移计算单元测试 |
| `tools/bots.mjs` | 修改 | `S5.2-4` 改读 `ac_tick_schedule_error_ms_p95`；替代判据与依据写进 note；删除未验证的 Linux 断言 |
| `tools/report.mjs` | 修改 | 报告渲染新增 schedule error / sim drift / tick work 三行 |
| `docs/evidence/bots-schedule.json` | 新建 | 修复后压测证据（含前 1/3 与后 1/3 两段 p95） |
| `docs/验收报告.md` | 修改 | 回填 §3.2 的 `S5.2-4` 与 §3.3 的 tick 相关结论 |

## 4. 任务清单

- [ ] 1. `metrics.ts`：新增 `tickScheduleErrorSamples/Sum/Max/Ring/Cursor`、`tickWorkSamples/Sum/Max/Ring/Cursor`、`simDriftSamples/MaxAbs`；新增 `recordTickScheduleError(metrics, errorMs)`、`recordTickWork(metrics, workMs)`、`recordSimDrift(metrics, driftMs)`（可复用现有 `recordTickJitter` 的环实现，抽成 `recordRing(ring, cursor, sample)`）。
- [ ] 2. `metrics.ts`：新增 `tickScheduleErrorP95/P50`、`tickWorkP95/P99`、`simDriftMsMaxAbs`；`renderPrometheus` 输出
      `ac_tick_schedule_error_ms_p95`、`ac_tick_work_ms_p95`、`ac_tick_work_ms_p99`、`ac_sim_drift_ms`（gauge，取最近样本的绝对值最大值）。
- [ ] 3. `room.ts`：
  - `Room` 增加 `firstTickAtMs: number`（首 tick 的 monotonic 时刻）与 `tickIndex: number`；
  - `runTick` 内：`const ideal = room.firstTickAtMs + room.tickIndex * SERVER_TICK_MS`，
    `recordTickScheduleError(deps.metrics, monotonic - ideal)`；
  - 用 `deps.monotonicNow()` 取首尾差值 `recordTickWork(deps.metrics, workMs)`（只统计 tick 主体，不含快照发送以外的分支？— 统一计入整段 `runTick`，口径写死）；
  - `deps.metrics.simDriftMs` 更新：`recordSimDrift(deps.metrics, room.world.timeMs - （首次 tick 的真实时间 + 累积真实经过时间))`，实现方式：`Room` 记 `wallStartMs` 与 `simStartMs`，漂移 = `(world.timeMs − simStartMs) − (nowMs − wallStartMs)`；
  - 保留现有 `recordTickJitter`（改名 `recordTickInterval`）作为对照指标 `ac_tick_interval_error_ms_p95`，便于对比新旧口径。
- [ ] 4. `room.ts` `updateRoom` 的 tick 循环加预算：进入 `while` 前取 `budgetStartMs = deps.monotonicNow()`，
      每步后若 `now - budgetStartMs > LIMITS.roomTickBudgetMs && steps > 0` 则 `break`（**不丢累积量**，剩余留给下一次调用），
      计数 `ac_room_budget_exceeded_total`。语义约束：不得跳 tick、不得改步长、不得改 tick 顺序；只把同一串 tick 分摊到多次调用。
- [ ] 5. `server.ts`：`setInterval(5ms)` → 自校正调度：
  - `let nextDeadlineMs = monotonicNow();`
  - `function pump(): void { const now = monotonicNow(); while (now >= nextDeadlineMs) nextDeadlineMs += 1; if (now - (nextDeadlineMs - 1) > 100) nextDeadlineMs = now; step(now); loop = setTimeout(pump, 1); }`
  - 说明写进代码注释：**模拟正确性来自房间累积器**，调度只决定"何时把时间片喂给房间"；`setTimeout(1)` 的粒度由 OS 决定（Windows ≈15.56ms），自校正保证误差不累积。
- [ ] 6. `tools/bots.mjs`：
  - `S5.2-4` 的 `measured` 改为 `metricsEnd.get('ac_tick_schedule_error_ms_p95')`；
  - 采样时在 1/3 与 2/3 时刻各记一次 `/metrics`，据此计算 `scheduleErrorP95Early` / `scheduleErrorP95Late`；
  - 判定：`p95 ≤ 8` 直接 pass；否则若 `timerGranularityMs > 8` 且 `|late − early| ≤ 2` 且 `|ac_sim_drift_ms| ≤ 50`，判 `pass（替代判据）`，note 里写清依据与实测粒度；
  - 删除原 note 中"生产 Linux 上 5ms 循环可用"的断言，替换为"本机定时器粒度实测 X ms；自校正调度下误差不累积（前 1/3 与后 1/3 差 Y ms）"。
- [ ] 7. `tools/report.mjs`：Markdown 报告新增三行（schedule error p95、sim drift max、tick work p95/p99）。
- [ ] 8. 测试：`server.test.ts` 用假 `monotonicNow`（可手动推进的时间源）驱动 `step()` 1000 次，断言
      `|world.timeMs − 真实经过时间| ≤ SERVER_TICK_MS`；`metrics.test.ts` 断言分位数与 `simDriftMsMaxAbs` 计算正确（含跨环覆盖）。
- [ ] 9. 复跑并落证据：`node tools/bots.mjs --players 4 --minutes 5 --strict --out docs/evidence/bots-schedule.json`，
      再跑 `node tools/soak.mjs --minutes 5 --sample 10 --out docs/evidence/soak-5min.md --json docs/evidence/soak-5min.json` 确认 CPU/RSS 无回归。
- [ ] 10. 回填 `docs/验收报告.md` §3.2（`S5.2-4` 结论 + 判据依据）与 §6（若替代判据生效，写明"生产环境复测"仍是未决项及其命令）。

## 5. 冻结契约

```ts
// packages/shared/src/net/protocol.ts → LIMITS 新增
roomTickBudgetMs: 8,   // 单次 updateRoom 调用内步进 tick 的工作量预算（毫秒），超出则本房间让出事件循环

// packages/server/src/metrics.ts 新增（Prometheus 名 → 类型）
ac_tick_schedule_error_ms_p95  // gauge：|实际 tick 时刻 − (首 tick 时刻 + n×50ms)| 的 p95
ac_tick_work_ms_p95            // gauge：单次 runTick 工作耗时 p95
ac_tick_work_ms_p99            // gauge：同上 p99
ac_sim_drift_ms                // gauge：|模拟时间增量 − 真实时间增量| 的近期最大值
ac_room_budget_exceeded_total  // counter：房间因工作量预算让出事件循环的次数
ac_tick_interval_error_ms_p95  // gauge：旧口径（与上一次实际 tick 的差）保留作对照
```

| 冻结项 | 值 / 定义 |
|---|---|
| 调度方式 | 绝对时刻自校正：`nextDeadlineMs += 1`（ms 网格），仅在 `now ≥ nextDeadlineMs` 时 `step(now)`；单次 `pump` 内不用 `while` 追赶超过 100ms |
| 抖动口径 | `tickScheduleError = monotonicAtTickStart − (firstTickAtMs + tickIndex × 50)`；`tickIndex` 从 0 起，逐 tick 自增，房间清空重建时重置 |
| 漂移口径 | `simDrift = (world.timeMs − simStartMs) − (nowMs − wallStartMs)`；`wallStartMs`/`simStartMs` 在房间首个 tick 时取 |
| 预算语义 | 让出 ≠ 丢 tick：累积量不扣除，下一次调用继续；`tickSkips` 仍只由 `tickCatchUpLimit` 触发 |
| 协议 | 不涉及 |

## 6. 验证

| # | 命令 | 期望 | 失败意味着 |
|---|---|---|---|
| 1 | `pnpm --filter @ac/server test` | 新用例通过 | 调度/预算改动引入了模拟时间漂移 |
| 2 | `node tools/bots.mjs --players 4 --minutes 5 --strict --out docs/evidence/bots-schedule.json` | `S5.2-4` `pass`（直接判据或替代判据），退出码 0 | 若是直接判据失败且粒度 ≤8ms → 计算工作量（转 O04）；若是漂移超限 → 累积器被预算改坏 |
| 3 | `curl -s localhost:8787/metrics \| grep -E "tick_schedule_error\|tick_work\|sim_drift\|room_budget"` | 五条指标齐全 | 指标未接线 |
| 4 | `node tools/soak.mjs --minutes 5 --sample 10 --out /tmp/soak.md --json /tmp/soak.json` | RSS 增长斜率 <1MB/分钟、房间回落到 0、无未捕获异常 | 调度重写引入定时器泄漏（`setTimeout` 未清理） |
| 5 | `pnpm check` | 退出码 0 | 类型/风格/文档/覆盖率任一门失败 |
| 6 | 人工复核：`grep -n "nextDeadlineMs" packages/server/src/server.ts` | 存在自校正步进与 100ms 追赶上限 | 退化成"每次 setTimeout(1) 立即 step"，把调度误差换成了 CPU 空转 |

## 7. DoD（验收标准）

- [ ] `pnpm check` 全绿。
- [ ] `ac_tick_schedule_error_ms_p95 ≤ 8ms`；若使用替代判据，则 `|late − early| ≤ 2ms` 且 `|ac_sim_drift_ms| ≤ 50ms`，且判据依据写进证据文件与报告 note。
- [ ] `ac_tick_work_ms_p95/p99` 与 `ac_sim_drift_ms` 在 `/metrics` 可见并被 `tools/report.mjs` 渲染。
- [ ] `server.test.ts` 的"1000 次 step 后模拟时间与真实时间偏差 ≤1 tick"断言通过。
- [ ] `tools/bots.mjs` 中不再存在"生产 Linux 上 5ms 循环可用"这类未验证断言。
- [ ] `docs/evidence/bots-schedule.json` 与回填后的 `docs/验收报告.md` 存在。
- [ ] 未新增运行时依赖；未改 `SERVER_TICK_MS = 50`；未改 tick 顺序与模拟步长。

## 8. 风险与回滚

| 风险 | 触发信号 | 对策 |
|---|---|---|
| 自校正调度在"追赶风暴"下空转烧 CPU | soak 的 `cpuAvg` 从 2% 跳到 >10%，或 `ac_ticks_total` 增速 >20/s | `pump` 内 `while` 追赶上限 100ms + `nextDeadlineMs = max(nextDeadlineMs, now)`；`soak` 门槛纳入本步验证 |
| 预算让出改变了输入延迟手感 | `rttP95Ms` 上升或 `commandsDropped` 激增 | 预算默认 8ms 足够 4 房间 20Hz；若触发则只对本房间生效并记录 `ac_room_budget_exceeded_total`；必要时把预算调成"每房间独立预算" |
| 替代判据被当成"放宽门槛" | 报告中出现 `pass（替代判据）` 但没写依据 | §4 任务 6 强制把 `timerGranularityMs`、前后段 p95、`sim_drift` 三个数写进 note；缺一个即视为验证失败 |
| 漂移指标口径写错（把 pause/空房算成漂移） | 空房或宽限期后 `ac_sim_drift_ms` 长期偏大 | `wallStartMs`/`simStartMs` 在**首个 tick**取，房间清空时重置；`metrics.test.ts` 覆盖空房场景 |
| 回滚目标 | — | 回滚 = `git revert` 本次提交（`server.ts`、`room.ts`、`metrics.ts`、`protocol.ts`、`tools/*.mjs`），调度退化为 `setInterval(5ms)`；不含协议与持久化变更 |

## 9. 移交物

**移交物 ID**：HANDOFF-O02

给下一步（O03 出站帧所有权与背压）的稳定接口与已验证能力：

**稳定接口**
- `LIMITS.roomTickBudgetMs = 8`
- 指标：`ac_tick_schedule_error_ms_p95`、`ac_tick_work_ms_p95/p99`、`ac_sim_drift_ms`、`ac_room_budget_exceeded_total`、`ac_tick_interval_error_ms_p95`
- `recordTickScheduleError(metrics, ms)`、`recordTickWork(metrics, ms)`、`recordSimDrift(metrics, ms)`、`recordTickInterval(metrics, ms)`
- `server.ts` 的自校正调度（`nextDeadlineMs`）+ `updateRoom` 的预算让出语义（让出 ≠ 丢 tick）

**已验证能力**
- tick 调度误差与模拟漂移是两个可分别判定的指标；4 机器人 5 分钟场景下 `S5.2-4` 可判定为 PASS（含替代判据的依据留痕）
- 单个房间的计算抖动不再独占事件循环（有预算与计数）
- `/metrics` 与压测报告都能看到"调度 / 漂移 / 工作量"三件事，便于把后续 O04/O05 的收益量化

**未决项（移交后续）**
- 若替代判据生效，则"生产 Linux（低粒度定时器）上直接判据是否 ≤8ms"仍是未验证项，命令与预期已写入 `docs/验收报告.md` §6
