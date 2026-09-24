# C05 验收记录（MC05）

- 计划：[`docs/plans-v2/client/C05-输入相机与本地移动预测.md`](../plans-v2/client/C05-输入相机与本地移动预测.md)
- 固定点：`1710548`（MC04）→ 本份提交
- 自检：`SELFTEST OK cases=24`（本份新增 5 条：`input.rate_30hz`、`input.backlog_merge`、`input.lock_flush`、`camera.pitch_clamp`、`sim.local_step`）

## 1. §6 的 5 条验证

| # | 命令 | 输出 | 结论 |
|---|---|---|---|
| 1 | `client/Logs/selftest.cmd` | `SELFTEST START cases=24` / `PASS input.rate_30hz` / `PASS input.backlog_merge` / `PASS input.lock_flush` / `PASS camera.pitch_clamp` / `PASS sim.local_step` / `SELFTEST OK cases=24`，exit=0 | ✅ |
| 2 | 用例 `input.rate_30hz` | 1ms × 1000 次 → 发出 29..31 条（首次发送出现在第 34ms，复算 29），`moveX=127`、`moveY=0`、`buttons=0`；单次 1ms 不发也不入队 | ✅ |
| 3 | 用例 `input.backlog_merge` | 5 次采样不取走 → `PendingCount=2`（= `MaxBacklog`）、`CommandsMerged=3`、取尽后最后一条的 `seq == Seq` | ✅（期望值见 §3.1） |
| 4 | `Select-String -Path client/Assets/Scripts/Sim/{LocalStep,Predictor}.cs -Pattern "Math\.(Sin|Cos|Tan|Exp|Log|Pow|Atan)"` | 无输出（连带跑 C02 §6.3 的全量禁超越函数扫描，`Sim`+`Net` 目录同样为空） | ✅ |
| 5 | `node tools/check-docs.mjs` / `node tools/check-assets.mjs` | `OK：v2 30 份计划（S/C 链）+ 10 份前置文档…` / `OK：仓库零外部素材，依赖白名单未被破坏。` | ✅ |

入口条件 §2：门 1 两行 `True`；门 2 两行 `True`；门 3 `MaxRetransmits` 命中 4 处；门 4 `inputsystem` 命中 0 处；门 5 `Test-Path $env:AC_UNITY` 为 `True`。

## 2. §7 的 8 条 DoD

| # | 判据 | 证据 | 结论 |
|---|---|---|---|
| 1 | §6 五条全过、输出逐字匹配 | §1 | ✅ |
| 2 | 上行 30Hz（±1/秒）；积压超 2 条只留最新 | `input.rate_30hz` + `input.backlog_merge` | ✅ |
| 3 | `LocalStep` 六步顺序与 §5.3 逐条一致 | `sim.local_step`（前进 0.225m、冲刺 0.315m、斜向 1/√2、谷仓推离、栅栏 39.35、非 50ms 拒绝） | ✅ |
| 4 | 单步位移 ≤ 0.315m | `sim.local_step` 中 `≤ 6.3 * 0.05` 的显式断言 | ✅ |
| 5 | 俯仰钳到 ±1.5707963267948966，越界不产生滚动 | `camera.pitch_clamp`：±10 万像素的鼠标增量后 `pitch == ±π/2` | ✅ |
| 6 | 失焦清理：下一条命令 `moveX`/`moveY`/`buttons` 全 0 | `input.lock_flush` | ✅ |
| 7 | `client/Assets/Scripts/Sim/` 内无超越函数 | §1 第 4 行 | ✅ |
| 8 | 仓库质量门全绿 | §1 第 5 行 | ✅ |

## 3. 计划修订与跨链

### 3.1 本份修订的计划条目
- §3/§4：用例文件 `input_camera_test.cs` → `InputCameraSuite.cs`（C03 起测试文件名统一 `*Suite.cs`），并新增第 5 条用例 `sim.local_step`——§7 第 3/4/7 行要求对 `LocalStep`/`Predictor` 逐条断言，四组输入/相机用例承载不了。
- §3 新增一行 `Net/CommandCodec.cs`【修改】、§5.3 与 §9：局部步进入口消费 `Ac.Sim.StepCommand`，采样侧输出 `Ac.Core.InputIntent`。原因是硬约束而非风格：`ContractSuite` 白名单里 `Ac.Core` 无任何程序集引用、`Ac.Sim` 只引用 `Ac.Core`，因此 `InputSampler` 碰不到 `CommandPayload`、`LocalStep` 也碰不到。三者的映射放在 `Ac.Net`（`IntentToPayload` / `ToStepCommand`），这是唯一能同时看到三层的程序集。
- §6 第 2 条：`InputSampler.Update()` → `Update(dtMs)`。§6 第 2 条要求"以 1ms 步进模拟 1000ms"，没有入参就无法在批处理里确定性推进，也不能让用例脱离 `Time.deltaTime`。
- §6 第 3 条：原写"构造 5 条未确认命令后再采样一次 → 队列长度 1、`commandsMerged` 增加 4"，与 §5.1 的"未确认超过 2 条即丢弃除最新外的全部"自相矛盾——上限既然是 2，队列永远到不了 5，也不可能一次丢 4 条。按 §5.1 的口径改写成可直接观测的期望：5 次采样后队列 2、`commandsMerged` 3（每丢一条旧意图记一次），且队尾始终是最新采样。
- §9：补记 `OnFocusChanged(bool)`、`CommandsMerged`、`PendingCount`、`DroppedSubsteps`。

### 3.2 有意保留的判断题
- `FpsCamera`/`ViewModelAnchor` 只交付可断言的参数与状态，引擎接线（点击锁定、`Escape` 解锁、`Application.focusChanged` 订阅、`Camera` 组件赋值）留给场景装配计划：批处理 `-nographics` 下没有可信的鼠标/焦点事件，把事件源做进本份只会得到无法验证的代码。§5.6 的三条语义本身已实现并被用例覆盖（`OnFocusChanged(false)` → 清位 + flush 零意图 + 停止采样；`RequestPointerLock`/`ReleasePointerLock` 在非播放态只改状态、不碰 `Cursor`）。
- `InputSampler` 自带一份角度量化（`QuantizeRadians`）：`Ac.Core` 引用不到 `Ac.Sim`，只能在采样侧按同规则算一遍，值与 `Quantize.QuantizeAngle` 同域（用例经 `Quantize.DequantizeAngle` 反读校验）。
- `Predictor` 的 `SetAuthoritative` 只写基线，不做和解重放（计划 §1 明写留给后续计划）。
- `Escape` 解锁、点击锁定、`Application.focusChanged` 的**引擎事件订阅**同样归场景装配：本份交付的 `FpsCamera.RequestPointerLock/ReleasePointerLock`、`InputSampler.OnFocusChanged`、`InputSampler.SetPointerLocked` 就是那三个事件的落点，语义已逐条实现，缺的只是"谁来调"。
- 指针锁定对视角的约束在采样器里落地（引擎路径仅在 `PointerLocked` 时读鼠标增量），但 `FpsCamera` 与 `InputSampler` 的联动仍由场景装配接线：批处理下无法验证 `Cursor` 行为。

### 3.3 跨链事项
- `CommandCodec.ToStepCommand`/`IntentToPayload` 是 C05 引入的 Net↔Core/Sim 缝，C06 的和解重放与 C12 的发送路径都应复用它，不要各写一份字段拷贝。
- 失焦 flush 的零意图命令只有接上 C03 的 `CommandChannel` 才真正"防止失焦后自动前进"；本份只保证它被生成并入队。
- 上行 30Hz 与 §5.4 的 50ms 子步进是两套节拍：命令 33.333ms 一条、预测 50ms 一步，C06 对账时不要按同一时钟对齐。

## 4. 双轴审查（本轮 · 固定点 `1710548`）

两个子代理并行跑规格轴与规范轴（各自读规格/标准与全部新增文件，按快照 hash 记录版本）。完整报告在 `client/Logs/review-c05-spec.md` 与 `client/Logs/review-c05-standards.md`（gitignore，不入提交）。

### 4.1 规格轴
| 发现 | 处置 |
|---|---|
| A1（必须修）§5.6 第 3 条：失焦 flush 的「零意图」只对注入路径成立——真实播放态 `BuildIntent` 直接读引擎 `Input`，被清空的注入表根本不参与 | ✅ 已修：`BuildIntent` 入口先查 `_intentCleared`，失焦那条命令改由 `ZeroIntent()` 直接构造、完全不读引擎输入。批处理下无法制造真实的焦点/按键事件，所以用例只能覆盖注入路径；修法是结构性的（单一入口守标志），不是补一条分支 |
| B1 §5.1 采样源行冻结了 `Input.GetAxisRaw("Horizontal"/"Vertical")`，实现改用了 §5.2 键位表的 `GetKey` | ✅ 已修：引擎路径改回 `GetAxisRaw("Vertical"/"Horizontal")` + `AxisValue`（保留了连续轴换算余地），注入路径（用例/回放）继续用 `SetKey` 键位表 |
| B2 `Escape` 解锁指针零实现 | ⏸ 场景装配项，记入 §3.2（本份交付 `ReleasePointerLock()` 这个落点） |
| B3 鼠标增量未受指针锁定约束 | ✅ 已修：新增 `SetPointerLocked`，引擎路径仅在锁定态读鼠标增量（§5.6 第 1 条） |
| B4 `DroppedSubsteps` 计数口径 | ⏸ 保留：按累加器余量整除计（丢的是子步，不是帧），已在 §3.2 的口径说明里写明 |
| B5 §8 回滚清单文件名未同步 | ✅ 已改为 `InputCameraSuite.cs` |
| B6 记录写「30 条」而按首次发送时点复算是 29 | ✅ 已按实测口径改成 29..31（首次发送出现在第 34ms） |

### 4.2 规范轴
| 发现 | 处置 |
|---|---|
| F7（必须修）失焦零意图的破口（与 A1 同一处） | ✅ 已修（见 4.1 A1） |
| F1/F2/F3 量化与回绕、按钮位域、π/2 各写两三遍 | ⏸ 部分：`Ac.Core` 按白名单引用不到 `Ac.Sim`，共享原语必须先落进 `Ac.Core`，而本份计划里没有这个文件——属于下一份的收敛项（已记入交付说明）。本份用交叉断言把一致性钉死：用例把 `InputSampler` 的 sprint 位直接喂给 `Sim.StepCommand`（Core 位域 == Sim 位域）、用 `Quantize.DequantizeAngle` 反读采样器的 π/2、断言 `FpsCamera.PitchLimitRad == InputSampler.PitchLimitRad` |
| F4 `MaxPending = 8` 的环形守卫不可达 | ✅ 已修：待发队列改成 `MaxBacklog` 长度的 FIFO，删掉环形索引与不可达守卫 |
| F5 `flush`/`dtMs` 死参数、`Enqueue` 恒真返回 | ✅ 已修：`Sample()`/`Enqueue(intent)` 去掉无用参数，`Update` 的返回值语义改为「本帧是否到了上行间隔」 |
| F6 无调用方的公开 API（`IsDown`/`ClearIntent`/`ClientTick`/`SetClientTick`/`YawRad`/`PitchRad`/`Focused`/`SetPointerLocked`） | ✅ 部分：删掉 `IsDown` 与 `ClearIntent`；`YawRad`/`PitchRad`/`Focused`/`PointerLocked` 在用例中断言；`ClientTick`/`SetClientTick` 是 §5.1 的线上字段，喂入方是 C06 的快照层，保留并记入跨链事项 |
| F8 `InputIntent.YawUnits/PitchUnits` 与冻结字段表 `yaw`/`pitch` 不同名 | ✅ 已改名 `Yaw`/`Pitch`（映射处只剩一套名字） |
| F9 `ViewModelAnchor` 死属性、近远裁零引用、类应 static | ✅ 已修：改 `static class`、删 `LocalX/Y/Z`，近远裁与 FOV 在用例里逐条断言 |
| F10 `Predictor.Steps` 只写不读、`SetConfig` 无调用方 | ✅ 已在用例中调用 `SetConfig` 并断言 `Steps` |
| F11 50ms 时基三写（`Predictor.SubstepMs`/`StepDtMs`，且两处用法不一致） | ✅ 已修：`SubstepMs = (int)LocalStep.StepDtMs`，`StepWith`/`Advance` 同源 |
| F12 盒内推离的期望值由被测 `config` 自构（恒真） | ✅ 已修：期望值写成 `4.4` 字面量，另加一条谷仓盒冻结值断言（±4 / y 0..5） |
| J1–J14 判断题（职责过多、`MoveConfig`/`MoveState` 十个 double、手写向量数学不复用 `Vec3`、同形结构体拷贝与 `global::` 写法、用例类可见性、长用例、`At`/`Command` 命名、`MouseRadPerPixel` 零断言、yaw 重量化往返等） | ⏸ 全部保留，理由见报告。其中 J3/J13 与 C04 判例一致：与 `server/src/sim/movement.cpp` 逐字同序优先于复用既有类型，ADR-010 明确禁止「顺手优化」运算顺序 |

### 4.3 审查快照
规范轴报告基于 `InputSampler.cs` MD5 `1477AC3D`（14:20:50）那一版给出，审查期间该文件被并发改动过；本轮所有修复都已在其后落地，报告中的 F1–F8 行号按修订前版本阅读。

## 5. 复跑命令

```powershell
& cmd.exe /c (Join-Path (Get-Location) 'client/Logs/selftest.cmd'); "exit=$LASTEXITCODE"
Select-String -Path client/Logs/selftest.log -Pattern '^SELFTEST |^PASS (input|camera|sim)\.|^FAIL ' | ForEach-Object { $_.LineNumber.ToString() + ": " + $_.Line.Trim() }
Select-String -Path client/Assets/Scripts/Sim/LocalStep.cs, client/Assets/Scripts/Sim/Predictor.cs -Pattern "Math\.(Sin|Cos|Tan|Exp|Log|Pow|Atan)"
node tools/check-docs.mjs; node tools/check-assets.mjs
```
