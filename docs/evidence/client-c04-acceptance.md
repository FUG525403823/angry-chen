# 客户端 C04 验收记录（HANDOFF-C04）

- 计划：[C04-视图层快照应用与插值](../plans-v2/client/C04-视图层快照应用与插值.md)（快照字段与段序对齐 [S03](../plans-v2/server/S03-二进制协议与编解码.md) §5.3，载荷类型见 [C02](../plans-v2/client/C02-客户端数学量化与协议解码.md) §5.2）
- 里程碑：MC04（tag `MC04`）｜固定点：MC03（`6c3e929`）
- 代码面：新增 `Sim/SnapshotView.cs`、`View/{Interpolation,EntityViews,ErrorSmoother}.cs`、`Tests/InterpolationSuite.cs`；改动 `Net/SnapshotCodec.cs`（新增 `TryToFrame` 适配器）、`Tests/SuiteRegistry.cs`（登记 4 组用例）
- 本轮结论：`client/Logs/selftest.cmd` → `SELFTEST OK cases=19`（含 4 个 `view.*` 用例）

## 1. §6 五条验证（实测）

### 1.1 §6.1 分组自检
```text
SELFTEST START cases=19
PASS view.tick_monotonic
PASS view.interp_100ms
PASS view.jitter_50ms
PASS view.hard_correct_1m
SELFTEST OK cases=19
```

### 1.2 §6.2 tick 单调
喂入 tick 序列 `20, 19, 21, 21, 23`（全部 `baselineTick = 0` 的全量帧）→ 只应用 3 帧（`AppliedFrames == 3`、`AppliedTick == 23`），`BaselineMismatch == 0`、`InvalidSnapshots == 0`。另有四条边界断言：`baselineTick > tick` 与 `baselineTick > appliedTick` 各记一次 `baselineMismatch` 且不改 `AppliedTick`；`count > 128` 记 `invalidSnapshots`；解码失败由 `NoteDecodeFailure()` 记 `invalidSnapshots`（两者都不动已应用状态）。

### 1.3 §6.3 插值
两帧 `serverTimeMs` 相差 50ms（1000/1050），把渲染时间摆到中点 1025 → `InterpolationAlpha = 0.5`、端到端 `EntityViews` 里位置 `1.0m → 3.0m` 插值结果为 `2.0m`（断言误差 ≤ 1e-9）。同一用例还断言 `FindBracket` 的四种落点（区间内、早于最旧、晚于最新、只有一帧）与角度最短弧（350°→10° 得 +20°，反向得 −20°，跨 0° 的中点落在 0°）。

### 1.4 §6.4 视图层只读
```text
负向（禁镜像突变入口）：Get-ChildItem client/Assets/Scripts/View -Recurse -Include *.cs |
  Select-String -Pattern '\.(ApplyFrame|SetLocalPlayer|NoteDecodeFailure)\('        -> 无输出
正向（只读面清单）：同上目录 Select-String
  -Pattern 'view\.(ForEachVisible|TryGetLocalAuthority|TryGetFrame|TryGetEntityInFrame|CopyServerTimes)|SnapshotView\.'
  -> EntityViews.cs:38  SnapshotView.MaxEntities
     EntityViews.cs:45  SnapshotView.HistoryFrames
     EntityViews.cs:49  SnapshotView.EntityVisitor
     EntityViews.cs:116 view.CopyServerTimes(_times)
     EntityViews.cs:128 view.TryGetFrame(olderAge, out older)
     EntityViews.cs:129 view.TryGetFrame(newerAge, out newer)
     EntityViews.cs:138 view.ForEachVisible(_visitor)
     EntityViews.cs:166 view.TryGetEntityInFrame(olderAge, record.Id, out previous)
     EntityViews.cs:210 view.TryGetLocalAuthority(out localAuthority)
```
计划原有的 `SnapshotView\\.` 单条扫描只能命中静态访问，抓不到 `view.ForEachVisible(...)` 这类实例调用，已改成正负两向。视图层用到的只读成员正好五个（`ForEachVisible`/`CopyServerTimes`/`TryGetFrame`/`TryGetEntityInFrame`/`TryGetLocalAuthority`）加常量与委托类型；镜像的突变入口（`ApplyFrame`/`SetLocalPlayer`/`NoteDecodeFailure`）在 View 目录里一次都没出现。

### 1.5 §6.5 仓库质量门
```text
node tools/check-docs.mjs   -> OK：v2 30 份计划（S/C 链） + 10 份前置文档，线性链与链接校验通过。
node tools/check-assets.mjs -> OK：仓库零外部素材，依赖白名单未被破坏。
```
附带门禁（C02 §6.3 的禁超越函数扫描）在新增 `Sim/SnapshotView.cs` 之后仍为空；C04 §2 的四条入口条件全部成立（`codec=True`、`view_asmdef=True`、`net.stats_p99` 命中、`Quantize.AngleUnits` 命中）。

## 2. §7 DoD 逐项

| # | 验收标准 | 结论与证据 |
|---|---|---|
| 1 | §6 的 5 条验证全部通过，输出逐字匹配 | ✅ §1.1–§1.5 |
| 2 | 关键帧环容量 6、插值延迟初值 100ms、动态范围 100..250ms | ✅ `SnapshotView.HistoryFrames = 6`（用例断言喂 6 帧后 `FrameCount == 6` 且第 7 帧不可访问）；`Interpolation.DelayMs = 100`、`DynamicDelayMs`：中位数 20→100、50→100、80→160、200→250 |
| 3 | tick 非单调与重复 tick 都不改变已应用状态 | ✅ §1.2（`21` 重复帧与 `19` 旧帧都被丢弃） |
| 4 | 20Hz 快照率下渲染姿态连续：连续帧位置差 ≤ 0.5m | ✅ `view.jitter_50ms` 用 8 帧 50ms 间隔、到达时刻抖动 −25..+15ms 的序列，逐帧断言 `|ΔRenderX| ≤ 0.5`（实测每帧约 0.1m，2 m/s 移动） |
| 5 | 硬纠正阈值 1.0m 与归零阈值（<0.001m 归零、吸附 0ms）被断言 | ✅ `view.hard_correct_1m`：`0.0005m` 忽略且归零；`0.5m` 进入平滑、一个时间常数（0.12s）后衰减到 1/e；`1.0001m` 立即吸附（偏移 0、`SnapCount == 1`）；衰减到阈值以下归零 |
| 6 | `ErrorSmoother` 只影响渲染变换，偏移从不写回 `SnapshotView` | ✅ 用例在纠正+衰减+再次 `SyncFrame` 之后回读镜像帧第 0 条记录的 `XCm`（仍等于 `Quantize.QuantizePosition(5.0)`）、`AppliedTick` 不变；§1.4 的负向扫描在源码层再兜一道 |
| 7 | 视图层每帧零托管分配 | ✅ 稳态路径只写预分配数组：`SnapshotView` 的 6 槽环 + 1025 槽镜像表、`EntityViews` 的池/活动表/`_seen`/`_stamp` 与缓存委托、`RenderClock` 的 60 槽到达采样与插排缓冲；解码（`SnapshotCodec.Decode`）的分配属 Net 层 |
| 8 | `check-docs`/`check-assets` 全绿 | ✅ §1.5 |

## 3. 施工偏差与跨链待办

### 3.1 计划本轮的补充
- §2 入口条件第 3 条的路径订正为 `client/Assets/Tests/TransportSuite.cs`（C03 已把测试文件名从 `transport_test.cs` 改掉）。
- §3 交付物：测试文件名订正为 `InterpolationSuite.cs`；新增一行「改动 `Net/SnapshotCodec.cs`」，记录 `TryToFrame` 这条 Net→Sim 的缝。
- §5.2：事件块**不进**镜像——`EventEntry` 定义在 `Ac.Net`，而 `Ac.Sim` 按 `ContractSuite` 白名单只能引用 `Ac.Core`，所以事件留在 `SnapshotPayload.Events` 里由 UI 层消费；镜像只覆盖实体与时间字段。
- §5.3：写死渲染时间估计的口径（到达瞬间目标 = 该帧 `serverTimeMs`，帧间外推交给 `Advance`）。
- §5.4：回收判据写成「只读镜像的最新可见集合」（`ForEachVisible`）。
- §5.5：`ErrorSmoother` 增加 `Apply(...) → SmoothingAction` 作为阈值入口，并写明 View 层用 `Math.Exp`（ADR-010 只约束权威模拟与客户端预测）。
- §6.4 与 §7：见 §1.4 与 DoD 第 7 行的注记。
- §5.1：`StatsWindowMs` 注明落在 C03 的 `NetStats.WindowMs`，视图层不重复定义。
- §9：`RenderClock` 的 `DelayMs` 改名 `RenderDelayMs`（避免与外层常量 `Interpolation.DelayMs` 同名歧义）。

### 3.2 解释性偏离（原计划没写、但受既有契约约束必须选一种）
- `SnapshotView` 落在 `Ac.Sim`（计划即如此），因此自持一份 `FrameEntity`/`SnapshotFrame` 镜像；与 `Ac.Net` 的 `EntityRecord`/`SnapshotPayload` 之间的转换由 `SnapshotCodec.TryToFrame` 负责（Net 可以引用 Sim，反之不行）。
- `SnapshotView.ApplyFrame` 接收已解码的帧（`SnapshotFrame`）而不是原始字节：字节解码是 `SnapshotCodec` 的职责，重复实现一遍解码会分裂字段表的唯一来源。
- 关键帧环留在 `SnapshotView`（§3 如此规定），渲染侧通过只读访问器读取，而不是在 `EntityViews` 里再复制一份历史——历史只有一份真相。
- 本地玩家：有预测值时用预测值渲染（`HasPrediction`+`Predicted*`，本份不产生预测值，留给 C05/C06），同时把镜像里的权威姿态记到 `LastAuthorityX/Y/Z` 作为和解基线。

### 3.3 跨链与后续计划待办
1. 事件块（`SnapshotPayload.Events`）目前无人消费，C10（HUD）需要从 Net 层取用；C04 只保证它不参与插值。
2. `lastAckedSeq` 已随帧镜像保留，但本份不使用；对账与和解重放是 C06 的范围。
3. 服务端差分基线的语义（S03 §5.3：`baselineTick = 0` 为全量、否则为基线 tick）若与客户端不一致，会直接体现在 `SnapshotView.BaselineMismatch` 计数上；联调时应先看这个计数。
4. 快照率与插值延迟的关系：延迟动态上限 250ms 对应 6 帧环在 20Hz 下刚好覆盖（6×50ms = 300ms）；若服务端快照率降到 10Hz，6 帧只覆盖 600ms 仍够用，但 250ms 上限与 2×中位数(200ms) 会顶到上限，届时按 §8 的风险对策提升快照率而不是放宽抖动吸收。

## 4. 双轴审查（本轮 · 固定点 `6c3e929`）

两个子代理并行跑 Spec 轴与 Standards 轴（各自读规格/标准与全部新增文件，并各自实跑自检），报告与处置如下。

### 4.1 Spec 轴
| 发现 | 处置 |
|---|---|
| 事件块未进镜像，违反 §3「含末段事件块」与 §5.2「随帧保留供 HUD 消费」 | 🟡 **改规格**：`EventEntry` 定义在 `Ac.Net`，而 `Ac.Sim` 按 `ContractSuite` 白名单只能引用 `Ac.Core`，镜像无法自持事件类型；§5.2 已改成「事件留在 `SnapshotPayload.Events`，由 UI 层消费，不进 `SnapshotView`」，消费点记入 C10（§3.3-1） |
| §9 冻结的 `GetLocalAuthority(out LocalAuthority)` 与实现不符（`LocalAuthority` 类型从未定义） | ✅ 已修计划 §9：改为 `TryGetLocalAuthority(out FrameEntity)` |
| §6.4 的只读扫描抓不到实例调用（`view.ForEachVisible` 等），§7「逐字匹配」不达成 | ✅ 已修：改成负向+正向两条命令，实测负向无输出、正向只命中五个只读成员（§1.4） |
| `invalidSnapshots` 没有生产接线，只能手工调用 | ✅ 部分：`ApplyFrame` 自身对越界帧计数并保留已应用状态；解码失败上报需要 C05 的喂包循环（`SnapshotCodec.Decode` 失败 → `TryToFrame` 返回 false → `NoteDecodeFailure()`），本份由用例直接驱动 |
| §5.3「hpRatio/state/flags = 最新帧的值，不做插值」与 §5.4「本地玩家无预测值 → 用最新帧姿态」被实现成走插值区间 | ✅ 已修：只读遍历回调 `MarkVisible` 把最新帧的 kind/flags/hpRatio/state 直接写进视图对象，并对无预测值的本地玩家按最新帧姿态吸附；插值区间只服务于远端实体位姿 |
| §5.2「旧帧不更新任何状态」被 `InvalidSnapshots` 递增破坏（校验顺序） | ✅ 已修：`ApplyFrame` 先判 tick 单调与基线，再做数组/计数校验，旧帧连计数都不动 |
| 用例文件名与 §3 要求的 `interpolation_test.cs` 不符 | ✅ 计划已订正为 `InterpolationSuite.cs`（C03 起测试文件统一 `*Suite.cs`） |

### 4.2 Standards 轴
| 发现 | 处置 |
|---|---|
| §6.4 负向扫描命中 `view.Smoother.Reset()`，字面判据被自己绊倒 | ✅ 已修：负向只保留三个镜像突变入口；`Reset` 不列入（`ErrorSmoother.Reset()` 是视图层自有对象的清理），计划 §6.4 同步注明 |
| 死字段 `SnapshotView._timeBuffer` | ✅ 已删 |
| 无调用方的公开 API：与属性重复的 `GetServerTimeMs()`、`TryGetEntity/TryGetEntityInFrame`、`Interpolation.Clamp/Clamp01`、`ResetEstimateThresholdMs` | ✅ 已收敛：删掉重复的 `ServerTimeMs` 属性（保留 §9 冻结的 `GetServerTimeMs()`，用例里断言 6000）；`Clamp/Clamp01` 收成 private；`ResetEstimateThresholdMs` 是 §5.3 冻结常量，保留公开；`TryGetEntityInFrame` 转为视图层接缝（见下条）、`TryGetEntity` 收成 private |
| 零引用常量 `Interpolation.StatsWindowMs`、`Interpolation.HistoryFrames` | ✅ 已删（`StatsWindowMs` 落在 C03 的 `NetStats.WindowMs`；`HistoryFrames` 只保留 `SnapshotView` 一份） |
| Duplicated Code：交换删除循环两份、token 戳记表两份、`clock == null` 两份 | ✅ 戳记表整块删除，改由 `view.TryGetEntityInFrame` 提供——顺带修掉「`_stamp` 长 128 却按 `id-1` 索引，id > 128 必定越界」这个真 bug；`clock == null` 抽成局部量；两份交换删除循环保留（一份管镜像可见表、一份管视图池，合并要跨层抽象，判为判断题不修） |
| Feature Envy：`EntityViews.TryFindOlder` 自建环帧扫描 | ✅ 已修：删除自建扫描，改用 `SnapshotView.TryGetEntityInFrame`，同时让这个公开只读 API 有了调用方 |
| Mysterious Name：`view1`、`RenderClock.DelayMs` 与外层 `DelayMs` 同名 | ✅ 已修：`view1` → `entity`；`RenderClock.DelayMs` → `RenderDelayMs`（计划 §9 同步） |
| Primitive Obsession / Data Clumps：手写 sqrt、`Pose` 五个 double、未复用 `Vec3` | ⏸ 有意保留：`Vec3` 是 Ac.Sim 的量化整数向量（权威模拟用），渲染侧偏移与插值走 double 米制，混用会把量化误差带进渲染；`Pose` 是方法内临时聚合，没有第二个使用点 |


## 5. 复跑命令
```powershell
& cmd.exe /c (Join-Path (Get-Location) 'client/Logs/selftest.cmd')        # SELFTEST OK cases=19
node tools/check-docs.mjs ; node tools/check-assets.mjs
Get-ChildItem client/Assets/Scripts/View -Recurse -Include *.cs | Select-String -Pattern '\.(ApplyFrame|SetLocalPlayer|NoteDecodeFailure)\('
Get-ChildItem client/Assets/Scripts/Sim, client/Assets/Scripts/Net -Recurse -Include *.cs | Select-String -Pattern '(Math|MathF)\.(Sin|Cos|Tan|Sinh|Cosh|Tanh|Asin|Acos|Atan|Atan2|Exp|ExpM1|Log|Log2|Log10|Log1p|Pow|Cbrt|Hypot)'
```
