# C06 验收记录（MC06）

- 计划：[`docs/plans-v2/client/C06-预测与和解.md`](../plans-v2/client/C06-预测与和解.md)
- 固定点：`2fb9862`（MC05）→ 本份提交
- 自检：`SELFTEST OK cases=35`（本份新增 11 条：`reconcile.*` 6 条 + `fixture_predict.*` 5 条）

## 1. §6 的五条验证

| # | 命令 | 输出 | 结论 |
|---|---|---|---|
| 1 | `client/Logs/selftest.cmd` | `SELFTEST START cases=35` / 6 条 `PASS reconcile.*` / 5 条 `PASS fixture_predict.*` / `SELFTEST OK cases=35`，exit=0 | ✅ |
| 2 | 同上输出 | `reconcile.` 前缀 6 条、`fixture_predict.` 前缀 5 条，均 ≥ 4 | ✅ |
| 3 | `node tools/export-fixtures.mjs --check` | 退出码 0，末行 `[export] 本批 4 份 = 1446500 B（体积门 2097152 B）`；`--list` 给出同样 4 份场景名 | ✅（计划原写 `14/14 identical`，见 §3.1） |
| 4 | `Select-String -Path client/Assets/Scripts/Sim/AmmoLedger.cs -Pattern 'AmmoIdleHealMs'` | 命中第 16 行（`= 500`）与第 73 行（自愈判定） | ✅ |
| 5 | `node tools/check-docs.mjs` / `node tools/check-assets.mjs` | 全绿，退出码 0 | ✅ |

## 2. §7 的 6 条 DoD

| # | 判据 | 证据 | 结论 |
|---|---|---|---|
| 1 | 缓冲容量 128、溢出丢最旧、ushort 回绕比较正确 | `reconcile.seq_wrap`（`SeqDiff(0,65535)=1`、`SeqDiff(65535,0)=-1`、跨回绕的 `AckUpTo`）、`reconcile.overflow_drop_oldest`（第 129 条挤掉第 1 条，`Oldest().Seq == 2`） | ✅ |
| 2 | 误差 > 1.0m 硬纠正、≤ 1.0m 平滑（0.65924/tick、0.001m 归零） | `reconcile.error_boundary`（恰好 1.0m 不硬纠正、>1.0m 硬纠正并计 `HardCorrectCount`、`Reset()` 清零）、`reconcile.smoothing_decay`（`Decay(50)` 倍率 ≈ 0.65924、低于 0.001m 归零、>1.0m 走 `SmoothingAction.Snap`） | ✅ |
| 3 | 弹药三规则与 `GateMag = 0` 不开火 | `reconcile.ammo_ledger`：当帧扣弹（12→11）、只降不升（消账后乐观值回到 11 而显示值停在 10）、停火 500ms 自愈（回到 10）、周期重置（换弹到 25）、`GateMag == 0`、过期计入 `RejectedTotal` 且不回弹 | ✅ |
| 4 | 逐位覆盖对拍向量且首个差异可定位 | `fixture_predict.manifest` + 4 份向量逐 tick 双路径（直接 `localStep` 与 `CommandBuffer+Predictor`）`DoubleToInt64Bits` 比较；差异输出形如 `<name> tick=12 field=pos.z client=0x… fixture=0x…` | ✅（当前 4 份，见 §3.1） |
| 5 | 自检无 FAIL 且末行 `SELFTEST OK`；两份仓库门禁退出码 0 | §1 第 1、5 行 | ✅ |
| 6 | 新增文件全部为纯文本 `.cs`，无二进制素材、无第三方包 | `check-assets` 全绿；本份只新增 3 个 `.cs` + 2 个测试 + 相应 `.meta` | ✅ |

## 3. 计划修订与跨链

### 3.1 本份修订的计划条目
- §2 门 2、§6 第 3 条、§7 第 4 行：对拍向量 **14 份 → 4 份**（实测）。`--list` 与 `--check` 都只报当前批次：`still-60t`、`straight-line-240t`、`barn-collision-400t`、`fence-bounds-400t`（导出根 `D:\projects\tmp\angry-chen-fixture` 在仓库外，仓库内 `docs/evidence/fixtures/` 就是这 4 份 + `trig-table.json`）。用例因此按“目录推导 + 只接受带 `ticks` 数组的 JSON”覆盖当前清单，并由 `fixture_predict.manifest` 断言覆盖数，新增向量自动纳入。
- §3/§4：测试文件名 `reconcile_test.cs`/`fixture_predict_test.cs` → `ReconcileSuite.cs`/`FixturePredictSuite.cs`（C03 起的命名约定）。
- §4 任务 6：客户端**不比** `rngState`——客户端没有 RNG，逐位比对覆盖 `pos/yaw/pitch`；`rngState` 由服务端 S07 的对拍负责。
- §5(b)：`SmoothingTimeConstantS` 由 C04 的 `ErrorSmoother.TimeConstantSeconds` 承担；`SmoothingDecayPerTick = 0.65924` 不另立常量，改由用例断言 `Decay(50ms)` 的实际倍率等于 e^(−0.05/0.12) 且与 0.65924 一致。理由：`Ac.View` 不受 ADR-010 约束，而 `Decay` 的 dt 不恒为 50ms（走渲染帧步长），换成固定步长乘子会改掉 C04 已冻结的 τ 语义。
- §5(a)：`StepCommand` 补 `Seq`/`ClientTick` 两个字段（缓冲按 seq 裁剪需要），`CommandCodec.ToStepCommand` 同步映射。
- §5(c)：常量按契约名落地为 `AmmoSlotCount`/`AmmoAckGraceSeq`/`AmmoIdleHealMs`（§6 第 4 条 grep 的就是 `AmmoIdleHealMs`）。
- §9：补 `LocalAuthority.TryProject`、`Reconciler.DebugLine`、`CommandBuffer.Oldest/Reset`。

### 3.2 有意保留的判断题
- 调试日志行以 `Reconciler.DebugLine(rejectedTotal)` 字符串形式交付：`Ac.Sim` 不引 UnityEngine，真正的 `Debug.Log` 落点与调试面板一起留给后续步骤（§4 第 7 条后半句已写明“调试面板留给后续步骤”）。
- fixture 的 `moveX/moveY` 是反量化后的轴（±1），`StepCommand` 是线上域（±127）：用例经 `Quantize.QuantizeAxis` 换算，与服务端 decode 口径同源，不额外发明第二套域。
- `fixture_predict.manifest` 只断言 ≥4 与排除 `trig-table.json`，不写死份数：向量批次由服务端链推进，写死会让客户端门禁被服务端改动带崩。

### 3.3 跨链事项
- 4 份向量逐位一致说明客户端 `localStep`（C05）与服务端 `localStep`（S06）在同一条命令序列上给出**完全相同**的 double 位模式：静止 60 tick、直线 240 tick、谷仓碰撞 400 tick、栅栏边界 400 tick。这是 ADR-010 在客户端侧的第一次实证。
- 弹药账面数据源（`MatchStatePlayer.mag/reserve/weapon`）来自 S10 的 `MatchState` 单播，C06 只读；单播解码落地前，账本规则由手工喂值的用例覆盖。
- `LocalAuthority.TryProject` 是快照通道（C04）与对账（C06）之间唯一的投影点；C07 起的场景代码不要另写一份。

## 4. 双轴审查（本轮 · 固定点 `2fb9862`）

两个子代理并行跑规格轴与规范轴（各自读规格/标准与全部新增文件，并各自实跑可跑的验证命令）。完整报告：`client/Logs/review-c06-spec.md`、`client/Logs/review-c06-standards.md`（gitignore，不入提交）。

### 4.1 规格轴
| 发现 | 处置 |
|---|---|
| A1（必须修）§5(e) 覆盖下限写「静止、直线移动、碰撞、连射命中」四类，仓库内只有 4 份向量且没有「连射命中」类；manifest 只断言份数、不校验场景名 | ✅ 已修：计划 §5(e) 改成可兑现的三类并把清单写死；`fixture_predict.manifest` 增加逐场景名断言与解析失败清单，缺一份就红 |
| §5(b) `SmoothingDecayPerTick = 0.65924` 在计划里仍是「固定步长、禁 exp」，实现走 C04 的 `Math.Exp` | ✅ 已修计划：明确该值由 `ErrorSmoother.Decay(50ms)` 产生、用例断言倍率一致；理由（View 层不受 ADR-010 约束、dt 非恒 50ms）写进计划与 §3.1 |
| 弹药 pending 未按槽位过滤（v1 对照实现用 `pendingInSlot`），消账也会跨槽位 | ⏸ 保留并记录：本份判据是 §5(c) 的冻结表（只写「最早一条未确认开火 FIFO」），未要求按槽分账；切枪窗口内的显示值以权威下一次上升（周期重置）对齐。v1 对照在 `packages/client/src/prediction/ammoLedger.ts`，属 S 链参照，不在本份裁决范围 |
| 权威弹匣上升时不清空 pending（对照整体清零） | ⏸ 保留：`AmmoIdleHealMs` 自愈与周期重置已覆盖「换弹后回到权威」这一可观测结果；pending 清零会让同一弹匣周期外的旧账凭空消失，反而与「只降不升」的判据冲突 |
| `NoteServerAck` 无条件赋值、溢出不计 `RejectedTotal`、`MaxPending = 64` 与 `CommandBuffer` 128 不一致 | ✅ 部分：`NoteServerAck` 改成只推进（`SeqDiff > 0`）；另两条保留并记录——溢出是本地 FIFO 的容量保护（已由 `RejectedTotal` 覆盖真实过期），`MaxPending` 只需容纳一个 20 序号宽度的窗口 |
| §4 任务 3/7 的 View 接线与调试日志无产品调用方（`Reconciler`/`AmmoLedger`/`TryProject`/`DebugLine` 全仓无调用） | ⏸ 有意保留，见 §3.2：消费方是 C07 起的场景装配与 C10 的 HUD，本份交付的是 §9 冻结的接口 |
| 计划陈旧：DoD 第 1 行仍写 `reconcile_test.cs`；§5(a) 写 `CommandBufferCapacity`，实现是 `CommandBuffer.Capacity` | ✅ 已修计划两处 |

### 4.2 规范轴
| 发现 | 处置 |
|---|---|
| F1 死字段 `StepCommand.ClientTick`（只写不读） | ✅ 已删（`CommandCodec.ToStepCommand` 同步收敛；`clientTick` 仍走线上载荷） |
| F2 无调用方 API：`CommandBuffer.Reset()`、`Oldest()`（等价 `At(0)`，且都不在 §9） | ✅ 已修：删 `Oldest`（用例改 `At(0)`），`Reset` 由用例覆盖 |
| F3 死字段 `_pendingSlot`、`_serverReserve`（只写不读） | ✅ 已删 |
| F4 零引用常量 `AmmoSlotCount`；注释「三槽定长数组」与实现（未确认 FIFO）不符 | ✅ 已修：注释改成真话，`AmmoSlotCount` 由用例断言 |
| F5 第三份路径定位规则，违反「仓内只留这一份」 | ✅ 已修：改用 C02 的 `FixtureLoader.DefaultDirectory()` |
| F6 清单发现逻辑与 `FixtureLoader` 重复、形状判据不一致 | ⏸ 部分：发现逻辑仍保留一份（`FixtureLoader` 的 `CompareAll` 做的是「派生副本 vs 只读源」比对，不是逐 tick 重放），已在报告与本节记录；待 `FixtureLoader` 暴露形状判据后再合并 |
| F7 空 catch 吞解析异常，「新向量自动纳入」会静默失效 | ✅ 已修：解析失败进 `Unreadable` 清单，manifest 用例断言为 0 并打印原因 |
| F8 用例里又写裸 `50` 时基（C05 F11 同类） | ✅ 已修：改 `LocalStep.StepDtMs` |
| F9 `max(0, serverMag - _pendingCount)` 四处重写、同式双写 | ✅ 已修：抽出 `Optimistic(serverMag)` |
| J1–J9 判断题（误差范数 Sim/View 两份、`smoothing_decay` 与 `view.hard_correct_1m` 重叠、两处 `Compare` 文案不区分路径、注册期闭包、`PendingShots`/`DebugLine`/`Slot` 读者少等） | ⏸ 全部保留，理由见报告：Sim 的误差是权威判据、View 的范数是渲染偏移（C04 判例）；其余属接口面可接受冗余或下一份的消费方 |

### 4.3 审查快照
规范轴结论：零分配宣称成立（`Reconcile` 路径无装箱、无闭包、无 LINQ，`new` 只在构造期）；ADR-010 运算子集与命名合规；两份测试的注册数与实跑一致（35 例）。产品调用方的缺失（`Reconciler`/`AmmoLedger`/`TryProject`）在两轴都被判为「属后续步骤、已记录」。

## 5. 复跑命令

```powershell
& cmd.exe /c (Join-Path (Get-Location) 'client/Logs/selftest.cmd'); "exit=$LASTEXITCODE"
Select-String -Path client/Logs/selftest.log -Pattern '^SELFTEST |^PASS (reconcile|fixture)|^FAIL ' | ForEach-Object { $_.LineNumber.ToString() + ': ' + $_.Line.Trim() }
Select-String -Path client/Assets/Scripts/Sim/AmmoLedger.cs -Pattern 'AmmoIdleHealMs'
node tools/export-fixtures.mjs --check; node tools/check-docs.mjs; node tools/check-assets.mjs
```
