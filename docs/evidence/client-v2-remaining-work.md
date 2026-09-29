# 客户端链条（C01–C15）收官清单

> 更新：2026-09-28 ｜ 最近提交 `4611f2e`（B5）＋本轮 B1 修复轮（未提交前） ｜ 标签 `MC01 … MC14`，**无 `MC15`**
> 本文每条都有实测出处。A 段是要你出手的，B 段是我能独立做完的。

## 0. 现状

**能跑的**：帧回路（输入 → 预测/和解 → 快照镜像 → 视图同步 → HUD → 特效 → 覆盖层 → 绘制）、音频 mixer、**运行期呈现层装配**（相机/灯光/竞技场/羊群/特效/屏幕流/调试面板）、版本行与日志落盘、一条命令出发布包、资源引用守卫。
**已上屏**：HUD/准星/大厅/结算/波间/调试面板由 `Ac.UI/OverlayModel.cs`（布局模型）+ `Ac.Boot/OverlayRenderer.cs`（IMGUI 适配层）真画；**身份已通**：昵称键入 → 认领 pid → `GameLoop.LocalPlayerId`/`SnapshotView`/`EntityViews.SetLocalPlayer` → 相机/HUD 绑定。**还不能的**：真连时身份落不下来（客户端没有上报昵称的报文，服务端把所有人叫 `player`，见 A8）；`AmmoLedger`/武器数值缺权威来源；武器视图网格（C09 无生成器）。

| 事实 | 结果 |
|---|---|
| 场景 / 预制体 | 仍为 `0 / 0`——**这是设计选择**：`GameBootstrap` 用 `[RuntimeInitializeOnLoadMethod]` 纯代码自举（不手写 `.unity` YAML） |
| B1 后真正被构造的类 | `Camera`+`FpsCamera`、`LightingRig`、`ArenaMesh`（7 部件/6 材质/5 碰撞盒）、`Materials`、`SheepInstancePool`+`SheepVisuals`（经 `Culling`/`Batching`）、`Effects`、`ViewModel`、`Lobby`/`Results`/`Intermission`（按 `MatchStatePayload.Phase` 驱动）、`DebugPanel`（F3，走键位表）、准星调色板 |
| 帧分段 | `input/sync/predict/hud/fx/overlay/draw` 有生产打点；`audio` 仅在音频设备可用时接线。**打点规则**：只在该段真的做功时 `Mark` |
| 资产 guid | 136 个 `.meta` **全部 32 位十六进制**，0 例外、0 重复 |
| 自测 | `cases=164`，**非 fixture 失败 = 0**；另有 2 条失败来自并行会话已提交的 fixture 重写（见 A7） |
| 发布包 | `BUILD OK ac-client-0.1.0+2bb9d18-win64.zip`（`backend=Mono`） |
| CPU 帧预算 | P95 0.0069 ms、P99 0.0088 ms、0 B/帧、GC0=0 |

---

## A. 需要你出手

**优先级一览（按"卡住什么"排）**

| 优先级 | 事项 | 一句话 | 卡住什么 |
|---|---|---|---|
| ✅ | ~~A7 fixture 格式~~ | 客户端 loader 已适配服务端新 schema（14 向量 + `trig-table.json`，5560 tick），自测全绿 | 已闭环（187 例 `SELFTEST OK`） |
| ✅ | ~~A2 C15 六步联合验收~~ | 2026-XX-XX 本机 `JOINT-ACCEPTANCE PASS`（退出码 0），六步原始行 + 两侧版本行已入档 | **`MC15` 可以打标签** |
| ✅ | ~~A8 昵称上报通道~~ | 新增 `type=11 kJoin`（可靠，昵称 1–12 字节），服务端行表落真名（联调里是"牧羊人阿"） | 已闭环 |
| ✅ | ~~A9 产品侧输入链未接线~~ | 采样器 → 30Hz 上行（type=4）+ 本地预测 + 权威 tick 回填 + 准备键 + 指针锁定全部接上 | 已闭环（`boot.command_uplink`/`input.ready_key` 守着） |
| ✅ | ~~A1 C14 计划场景基准~~ | 按裁决把判定机制换成"引擎自己拥有帧循环"的窗口化 player，预算不动：`frameP95` **4.4858ms** / `frameP99` **4.9969ms**（限 20 / 33），12 项全绿，门禁退 **0**；空场对照 player 3.94ms vs batchmode 23.92ms 坐实了旧红是机制地板 | **`MC14` 可打标签**（附注：§5 第 76 行命令仍是 batchmode，是否同步改口径待计划所有人确认） |
| ✅ | ~~A3 三项跨计划冲突~~ | 剔除距离=档位表 60/80m；阴影=档位表 20/35/50m；准星散布与服务端 `weapons.hpp` 逐值相同 | 三条都只是**文档滞后**，代码早已同源（见 A3 段） |
| ✅ | ~~A4 `Send`/`Track` 拒收回滚~~ | `Send` 先 `Enqueue` 后 `Track`，被拒消息既不在重传表、也不上线（`net.send_reject_no_retransmit`） | 已闭环 |
| ✅ | ~~A10 "通道"与"类型"两套口径~~ | `seq` 每 `type` 一条、`msgId` 每可靠通道一条，已写进 S03 §5.1 / C02 §5.1 | 已闭环（联调 `lossPermille=0`） |
| **P3** | **A6** IL2CPP 模块（可选） | 想发 IL2CPP 包才需要 | 现在走 Mono 兜底并如实标注 |
| ✅ | ~~A5 本地玩家身份~~ | 已按"玩家自己输入昵称"落地客户端侧 | （A8 已闭环） |

### A9. ✅ 产品侧输入链已接线（本轮）
联调当时是用**测试桩**（`client/Assets/Tests/JointSuite.cs` 的 `Link`）把六步跑通的 —— 采样器、命令编解码、`UdpTransport.Send` 三者都在仓库里，却**没有任何东西把它们接起来**（`new InputSampler()` / `GameLoop.QueueCommand` / `SetClientTick` / `SetReadyHeld` / `SetPointerLocked` 全仓只有测试在用）。本轮全部接上：

| 环节 | 落点 | 钉住它的用例 |
|---|---|---|
| 键鼠 → 30Hz 命令 → **真发 type=4** | `GameLoop.PumpInput`：`sampler.Update` → `TryTakeCommand` → `CommandCodec.Encode` → `Transport.Send(PacketType.Command, …)`（只在 `Connected` 时发） | `boot.command_uplink`（1s 内 30±1 条、断开后不再发） |
| `clientTick` = **权威 tick** | `sampler.SetClientTick(_view.AppliedTick)`：来源是最近一条已应用快照，不是本地帧计数、不是预测 tick | 同上（逐帧断言 `ClientTick == 23`） |
| 同一条命令进本地预测 | `QueueCommand(CommandCodec.ToStepCommand(payload))` —— 与上行共用**同一份量化载荷**，不重新采样 | 同上（`LocalSteps > 0`） |
| 大厅准备键 | `InputSampler.ConfirmPressed`（只报按下沿）+ `GameLoop` 在大厅相位翻转 `ReadyHeld`；键位来自 `KeyBindings[ActionReady]`（默认 `Return`），灵敏度来自 `SettingsStore` | `input.ready_key`（边沿语义）、`input.ready_bit`（位域）、`boot.server_config`（键位/灵敏度接线） |
| 指针锁定 / 焦点 | `GameLoopDriver.Update`：点画面 `SetPointerLocked(true)` + `Cursor.lockState`，Escape 解锁，焦点跳变时 `OnFocusChanged` | `input.lock_flush`（失焦清理） |
| 装配 | `GameBootstrap.Start` 里 `Loop.Sampler = new InputSampler()`（离线/帧基准也接，采样器自己按焦点说话） | —— |

**键位与灵敏度也进了这条链**（本轮同批，之前是"存了盘没下游"）：准备键不再写死 —— `InputSampler.ConfirmKey` 由 `GameBootstrap` 从 `SettingsDefaults.KeyBindings[ActionReady]` 解析（默认表那条就是 `Return`），`SettingsKey.KeyBindings` 变化时重解析；鼠标灵敏度 `SettingsStore.Sensitivity`（0.2–3.0 钳制）在装配时与 `SettingsKey.Sensitivity` 变化时都写进采样器。`GameBootstrap.KeyBindingFor` 是调试面板与准备键共用的解析器（表项缺失/名字不认识 ⇒ 退回默认表同一条，代码里没有第二份键名）。用例：`boot.server_config`（键位表 + 灵敏度钳制）、`input.ready_key`（按下沿语义）。

### A10. ✅ "通道"与"类型"两套口径已写死（选①，与服务端现状一致）
`NetStats.OnInbound(type, seq, bytes)` 按**类型**（`PacketType`）记期望包数（`_seqSeen[(int)type]`，见 `NetStats.cs:91-105`），而 `S03 §5.2` 的"通道"把多条类型归一条流（MatchState 与 KeepAlive 同属控制方向）。本轮联调就踩在这里：服务端原先一条 `seq` 计数器同时喂 Snapshot(5) 与 MatchState(10)，客户端把插入的另一种包算成丢包，`PacketLossPermille` 直接顶到 500‰（第 3 步红）。

**裁决（用户 2026-XX-XX：按推荐来）＝①**：包头 `seq` 按**类型**独立，`msgId` 按**可靠通道**共享。已写进 [S03 §5.1](../plans-v2/server/S03-二进制协议与编解码.md) 的 `seq` 行与"序号口径"两条（含 `MatchState`/`KeepAlive` 共用控制通道、两者才是唯一回执载体），客户端侧在 [C02 §5.1](../plans-v2/client/C02-客户端数学量化与协议解码.md) 复述同一句。客户端统计不用改（本来就是按类型），服务端也不用改（上一轮已拆号）。

### A11. ✅ 产品阻断级：空昵称把本地身份、相机与键鼠一起废掉

用户实跑反馈："跑起来了，但视角很奇怪、整个人倒过来了、键鼠没反应"。静态定位到根子：`Lobby.Name` 的初值是**空串**
（`Scripts/UI/Lobby.cs:28`），而 `LocalIdentity.MatchPid` 对空名字直接返回 `NoPid`（`Scripts/Net/LocalIdentity.cs:40`）
⇒ 三件事同时发生：

1. `LocalPlayerId` 恒为 0 ⇒ `PresentationLayer.SyncCamera()` 在 `Views.TryGet` 那步直接 return ⇒ 相机**停在装配原点**
   `(0,0,0)`，既不是人头高度也不跟人 ⇒ 画面诡异（看到的是几何体内部/脚下的身体，即"人倒过来"）；
2. `EntityViews` 里"本地玩家 + 有预测值"那条分支永不成立 ⇒ 键鼠采到的意图不驱动任何实体 ⇒ "键鼠没反应"；
3. `Lobby.IsNameValid` 为假 ⇒ 大厅连"按准备开局"这条路径都走不通。

修法（纯客户端、不改协议、不动预算）：`GameBootstrap.DefaultLocalName = "牧羊人"` 作兜底，在 `Presentation.Attach` 之后
补进大厅（玩家一旦键入即被 `SetName` 覆盖并重发 `kJoin`，见 `GameLoop.LocalName`）；`PresentationLayer.Attach`
不再用空名字覆盖帧回路；认领不到身份时 `SyncCamera` 打**一条**可见警告（"相机不跟人"在屏幕上看不出原因）。
用例补在 `boot.server_config`：默认昵称必须合法且过 `SanitizeName` 不变形。

未复核（本轮按用户要求不开 Unity）：真机复跑"进大厅 → 按 Enter 准备 → 开局 → 能转身能走"。另：默认名是**固定值**，
两个都用默认名的客户端会撞名（`MatchPid` 取最小 pid，双方会认领到同一行）—— 键入自定义名可绕开，
彻底方案仍是服务端在握手/加入回执里回 pid（`LocalIdentity` 类注释里记的过渡方案代价）。

### A12. ✅ 实跑修复：鼠标视角两轴全反（ADR-015）

用户实跑反馈"视角很奇怪、整个人倒过来、键鼠没反应"。除 A11（空昵称 ⇒ 相机停在装配原点）之外，还有第二处独立缺陷：
C05 §5.5 冻结的 `yaw -= dx * scale; pitch -= dy * scale` 抄自"pitch 以低头为正"的经典 Unity 片段，而本仓
pitch 以**抬头为正**（相机 `Euler(-pitch,…)` 给出 `forward.y = sin(pitch)`；服务端 `yawPitchToDirection` 的 +Y 分量同义；
`InputManager.asset` 的 `Mouse X/Y` 均 `type:1, invert:0` ⇒ 右移为正、上移为正）⇒ `-=` 等于双重取反：
鼠标右移视角左转、上移视角下压。裁决与落地见 ADR-015（`InputSampler.Sample()` 两行 + `camera.pitch_clamp` 用例
注入方向反向 + 补水平轴断言 + C05 §5.5 式子更正）；**线上 yaw/pitch 语义与协议一字未动**。

另一条独立结论，用来解释用户提供的 `tools/failed.png`：那张 2560×1600 图的下半屏是**逐像素均匀**的
(155,195,227)、**非该色像素占比 0.00%**，边界陡峭且落在屏幕正中——与"相机停在装配原点 (0,0,0)、`SetPose` 的 1.6m
眼高从未被加上"完全自洽：视线水平 ⇒ 地平线正好在屏幕正中；眼睛恰在 y=0 地面平面内 ⇒ 地面在视线里边缘朝向、
看不见；`y>0` 的谷仓/围栏落在上半屏；下半屏只剩 `LightingRig` 写死的 `backgroundColor`。也就是说该图就是
**A11 修掉的那个 bug 的现象**，且来自**未重新出包的旧二进制**（15:54 前后全机无任何 Unity/Tuanjie 日志写入；
`AppData\LocalLow\AngryChen\angry-chen` 最后写入 2026-09-24 11:58 且目录为空）。

未复核（本轮按要求不打开 Unity）：`InputCameraSuite` 的改动**未经执行**；出包版必须重新 `client/build.ps1`
才带得上 A11/A12。

### A1. ✅ C14 收口（真实图形设备 + **计划场景基准** + 引擎自持帧循环）

> **复核状态（2026-09-29）**：`frame-bench.ps1 -Runs 3` 完整跑到 `GATE-EXIT=0` 的是实现方那一次；我随后独立复跑时
> 与实现方的 Unity 会话并发，player 三轮自身均 `exit code 0`，编辑器诊断相位中断导致脚本退 1
> ⇒ **PASS 尚未被第二人独立复现**。复跑要独占编辑器（一次只允许一个 Unity 实例）。
上一轮的结论有两处失实，一并订正：

- **本机不是没有图形设备**：不带 `-nographics` 时编辑器拿到的是真设备（`client/Logs/frame-bench/run-20260928-112243-1.json`：`"gpu": "AMD Radeon(TM) Graphics"`、`"driver": "Direct3D 11.0 [level 11.1]"`、`Screen 640×480`）。带 `-nographics` 的那次（`direct.json`）才是 `Null Device` —— 那是我们自己的临时脚本 `direct.cmd` 传了 `-nographics`，`frame-bench.ps1` 本身从不传（§5 测量规则第 1 条）。
- **真正的拦路虎是基准入口只有合成 CPU 路径**：`Ac.Tests.FrameBench.Run` 造的是裸 `GameLoop`（不挂呈现层），`sceneKind=synthetic-cpu`、图形四项恒 -1、8 段里 fx/audio/draw/overlay 永不打点 ⇒ `verdict=UNVERIFIED`，门禁按设计退 2。**这与机器无关**，换任何一台机器都一样。

本轮补做：给同一个入口加**计划场景**路径（`GameBootstrap` 真装配：竞技场 7 部件 / 羊群池 / 相机 / 阳光 / 视图模型 / `Effects` / HUD，4 玩家 + 60 羊含王羊，逐帧合成战斗事件让 fx/draw/overlay 真的做功，真出帧，图形四项取真实渲染统计），跑三次取中位。**结果：图形四项与全部分段都达标，只有 `frameP95/P99` 红**（46.375 / 50.974 ms，限 20 / 33）：

| 指标 | 实测（三次一致） | 限 |
|---|---|---|
| `drawCalls` | **11** | 120 |
| `triangles` | **11608** | 180000 |
| `particles` | 256（池上限；`particleOverflow=12704` 如实登记） | 256 |
| `materials` | **8** | 24 |
| `managedAllocBytesPerFrame` / `gc0Delta` | **0 B/帧 / 0** | 0 / 0 |
| 8 段 P95 | 全在（最大 `draw` 0.127 ms） | 10 ms |
| `frameP95Ms` / `frameP99Ms` | **46.375 / 50.974** ❌ | 20 / 33 |

**红的根因是机制，不是客户端**（子代理实测归因，不是推断）：帧内分相计时显示客户端做功 P95 = **0.196 ms**（喂快照 0.0455 + `GameLoop.Frame` 0.157），`render` = **46.22 ms**；对照实验里 URP **空场景** 1920×1080 单独就要 **23.9 ms**（已经超掉整个 20 ms 预算），且 640×480（53.9）≈ 1920×1080（55.5）——**与分辨率、与被画的东西都无关**；同一内容换内置管线 16.2 ms；耗时还随渲染次数从 15.5 涨到 45.6 ms。结论：`-batchmode` 下引擎没有自己的帧循环，只能靠编辑器里手动 `Camera.Render()`，而它每次都把 URP 管线重建一遍。**任何内容都不可能在这个机制下达标**，所以这不是"客户端慢 46 ms"。

**裁决（按"按推荐来"）**：把 `frameP95/P99` 的测量机制改成**引擎自己拥有帧循环**的那一种（出包后跑窗口化 player，关 VSync；`frame-bench.ps1` 的批处理路径保留为诊断口径），预算**不动**；player 若同样超 20 ms，那就是诚实的 FAIL。

**裁决已执行（本轮）**：`Ac.Boot.PlanBench` 成为两条机制共用的运行时实现；新增 player 入口
`FrameBenchPlayer.cs`/`FrameBenchDriver.cs`（`[RuntimeInitializeOnLoadMethod]` 钩子 + 引擎帧循环驱动，
**全程 0 次手动 `Camera.Render()`**）；`frame-bench.ps1` 加 `-Mechanism auto|editor|player`（默认 auto：有出包走 player）。
先把**可证伪的对照**跑掉：同一个空场景，player 地板 **3.9360ms** vs batchmode **23.920ms**（差 6 倍）——
旧红确实是机制地板。然后 `-Runs 3`：

| 指标 | player 中位（判定） | 预算 | editor 诊断口径（同轮，不参与判定） |
|---|---|---|---|
| `frameP95Ms` | **4.4858** ✅ | 20 | 44.9787 ❌（机制地板） |
| `frameP99Ms` | **4.9969** ✅ | 33 | 48.2999 |
| `managedAllocBytesPerFrame` / `gc0Delta` | **0 / 0** ✅ | 0 / 0 | 0 / 0 |
| `drawCalls` / `triangles` | **11 / 11608** ✅ | 120 / 180000 | 11 / 11608（引擎口径历史值 51 / 34917 也在预算内） |
| `particles` / `materials` | 256（池上限）/ **8** ✅ | 256 / 24 | 256 / 8 |
| 8 段 P95 | 最大 `draw` **0.1598ms** ✅ | 10 | 归档中位最大 `draw` 0.127ms |
| 客户端自身做功 `workP95` | 0.2393ms | —— | 0.196ms |

门禁末行（退出码 0）与两条机制的数字：

```
FRAME-BENCH PASS p95=4.48580000000038ms alloc=0B mechanism=player editorP95=44.9786999999997ms editorVerdict=FAIL
```

三条必须一起读：① 预算一个字没动，判定只认 player 那条；② 判定机制与 §5 冻结的 `-batchmode` 命令不一致，
把 player 写成正式口径**待计划所有人确认**（`docs/plans-v2/**` 未改）；③ player 机制度到的是真窗口 1920x1080、
真引擎统计、真 0 次手动渲染（`pixelCoverage` 0.7160、`engineFrames` 720）。细节、对照实验与每个数字的来源见
[client-v2-frame.md](client-v2-frame.md)。

```
git pull && powershell -NoProfile -File client/build.ps1 -Target Windows64 -Backend mono
powershell -NoProfile -File client/tools/frame-bench.ps1 -Runs 3 -Frames 600
```

门禁 fail-closed：`verdict=UNVERIFIED` 或 `sceneKind≠plan-scene` ⇒ 退出 2；超预算 ⇒ 退出 1 + `FRAME-BENCH FAIL`；
player 轮另外强制校验 `mechanism` 与 `phaseMs.engineFrames > 0`（batchmode 里引擎永不前进帧）；**只有 JSON 说 PASS 才可能 PASS**。

### A2. ✅ 服务端在场跑完 C15 六步联合验收 —— `MC15` 已可打标签
2026-XX-XX 本机实测：`pwsh -File client/tools/joint-acceptance.ps1` → `SELFTEST OK cases=187` + `JOINT-ACCEPTANCE PASS`（退出码 0）。

```
客户端：ac-client 0.1.0+unknown proto=1              # 编辑器内跑；出包时 +<sha7> 由 build.ps1 注入
服务端：ac_server 0.1.0 protocol=1 tick=50ms
核对：proto == protocol（1 == 1）且 MAJOR.MINOR 相等（0.1 == 0.1）；+<sha7> 与 tick=50ms 不参与
```

六步原始行、服务端旁证（`/health`、`/metrics`、`matches-recent.json`、宽限期）与"已知口径"已全部写进 `docs/evidence/client-v2-acceptance.md` 第 2 节；联调期间发现并修掉的 8 个问题在该文件第 4 节。**遗留**：产品侧输入链未接线（A9）——验收是测试桩跑通的，真人还打不了。

### A3. 三项跨计划冲突需裁决
| # | 冲突 | 现状 |
|---|---|---|
| ✅ 1 | ~~剔除距离~~ | **已对齐**：`Batching.SheepCullDistanceM=60` / `ArenaCullDistanceM=80`（C14 档位表），`View/Culling` 与 `View/LightingRig` 都只读这张表，C08 的 `90m/60m` 已无调用路径（全仓搜 `90f` 无命中） |
| ✅ 2 | ~~阴影距离~~ | **已对齐**：`Batching.ShadowDistanceM={20,35,50}`，`LightingRig.ShadowDistanceMeters` 读 `Batching.ShadowDistanceFor(QualityTier)`；C07 的常量 `60f` 已删 |
| ✅ 3 | ~~准星散布值域~~ | **已对齐**：客户端 `Sim/WeaponTable.BaseSpreadDeg={0.8,0.6,4.0}`、`SpreadGrowthPerShotDeg=0.1`、`SpreadMaxDeg=0.25`、`SpreadDecayDelayMs=350`、`SpreadDecayPerSecondDeg=6.0` 与服务端 `server/src/config/weapons.hpp:24-35` **逐值相同**；超界只发生在**显示域**，由 `Crosshair.SetSpread` 夹到 `[0.5°, 5°]`（`WeaponTable.cs:26` 注明） |

> 三条都是"文档滞后"而非代码分歧：数值早就同源，是本文件上面的旧描述没跟上。上面这张表保留为核对记录 —— 以后谁再动其中一处，另一处必须同一提交改。

### A4. ✅ `Send` / `Track` 拒收回滚语义已闭环
`UdpTransport.Send` 现在是**先 `Enqueue`、后 `Track`**（`UdpTransport.cs:241-250`）：被 §5.1 积压封顶拒收（`Enqueue` 返回 false）的可靠消息**不会**进重传表，"受理失败却照样重发"的两本账对不上问题不存在。`TransportSuite` 的"拒收必须来自封顶而不是断开"钉住受理语义，`net.send_reject_no_retransmit` 钉住"被拒的消息此后再也不出现在任何数据报里"。

### A5. **本地玩家身份** ✅ 已按"玩家自己输入昵称"落地（客户端侧），但**真连仍落不下来**——见 A8
服务端 v1 的定论：**`pid` 就是玩家实体的 `EntityId`**（`server/src/room/match_controller.hpp:21`：`room.world.entities[pid - 1]`）。身份消费方早已就位（`GameLoop.LocalPlayerId`、`SnapshotView.SetLocalPlayer`、`PresentationLayer` 的屏幕流与相机/HUD 绑定），缺的是"我是谁"。

已实现（客户端）：大厅相位捕获键入 → `Ac.UI.NameInput`（char 缓冲 + 缓存字符串，非输入帧零分配）→ `Lobby.SetName`（清洗/校验 1–12 字节，与 wire 上限一致）→ `GameLoop.LocalName`；MatchState（type=10）到达或改名时，`Net/LocalIdentity.cs` 按玩家行 `Name` 与本地昵称**序数相等**认领 pid（同名多行取最小 pid 并记 `AmbiguousCount`；名字不在表里 ⇒ `pid=0`，不留旧 pid），写入 `GameLoop.LocalPlayerId` 并同步 `SnapshotView`。用例 8 条（含"相机/HUD 真绑到认领出的实体"与"身份已解析下 60 帧 0 分配"），变异测试两处各自打红。

### A8. ✅ 昵称上报通道已落地（`type=11 kJoin`）
服务端会话给出的协议结论（用户裁决 D2）：**新增 `type=11 kJoin`**（可靠 C→S，载荷 = 昵称 1–12 字节），`ADR-009` 的握手时序里紧跟 `HelloAck` 之后上报，服务端把它写进房间会话行并在 MatchState 行里下发。客户端侧：

- `Net/PacketType.Join = 11`、`Net/PacketWriter`/`Reader` 的编解码、`Handshake` 在 `HelloAck` 后自动发一次（昵称来自 `Lobby`，清洗/校验 1–12 字节）；
- 服务端 `runtime` 收到 `kJoin` 后落真名；联调实战里 `/api/matches/recent` 的玩家行是 `"name":"牧羊人阿"`（不再是 `player`），队友三条仍走 `player` 兜底（机器人不发 `kJoin`，这条兜底路径由服务端用例 `runtime_row_without_join_uses_player_fallback` 守着）；
- 按名认领（`Net/LocalIdentity.cs`）仍然在，且仍是"过渡方案"：服务端尚未在任何下行包里回显 `pid` 与 `clientNonce` 的绑定，所以客户端只能按行内 `Name` 序数相等认领。**要彻底删掉按名认领**，还需服务端在 `HelloAck`/`MatchState` 里回显本会话的 `pid`（新开口子，另立 ADR）。

附带一项仍未接：`View/EntityViews.cs` 的 `SetLocalPlayer` 无调用者（A9 的表里）。

### A6.（可选）IL2CPP 模块
`-Backend auto` 现走 Mono 兜底并在日志与 `manifest.json` 标注 `backend=mono`。要发 IL2CPP 包需补装 IL2CPP（Windows x64）模块 + VS C++ 工作负载。

### A7. ✅ fixture 冲突已闭环
服务端会话重写的 `docs/evidence/fixtures/*.json`（14 份向量 + `trig-table.json`，`dtMs=50`、5560 tick、`schemaVersion` v2）现在**就是**两侧的冻结契约，客户端 `fixtures.loader` 已按 v2 schema 适配（字段名/量纲/trigger 表口径），`fixture_predict.manifest` 不再报缺向量。客户端自测 `SELFTEST OK cases=187`（适配前 164 例里 2 条红）。

---

## B. 已完成

### B1. 场景与呈现装配 ✅（含已知缺口）
- 新增 `client/Assets/Scripts/Boot/PresentationLayer.cs`、`ScreenFlow.cs`，`GameBootstrap` 成为真正的组合根：`SettingsStore` → `GameLoop` → 呈现层 → 三条呈现缝（`Fx`/`Draw`/`Overlay`）＋ `EventApplied`，破坏性重建时重造残留层。
- 屏幕流按 `MatchStatePayload.Phase`（lobby=0/loading=1/playing=2/intermission=3/ended=4）驱动；相位包（type=10）走既有 `Loop.OnPacket` 入口 ⇒ `LastMatchState`/`MatchStateCount` ⇒ `TickOverlay`。
- 传输已接：地址 `-server host:port` → `AC_SERVER` → 默认 `127.0.0.1:8787`；编辑器/批处理一律不真连。
- 打点诚实：`fx`/`draw`/`overlay` 都只在真做功时 `Mark`（`draw` 仅在真有 `DrawMeshInstanced/DrawMesh` 提交时；`overlay` 仅在相位真 Apply 或面板真可见时），未接线不打点。（C14 那一轮 `FrameBench.cs` 与 `frame-bench.ps1` 未改；本轮按 C14 裁决改的是**测量机制**，预算表与 fail-closed 护栏都在，见 A1 段。）

**两轴审查与修复（commit `1b36859` 的审查结论是"不通过"，已逐条修）**

| # | 审查发现 | 修复 |
|---|---|---|
| 1 | 每帧堆分配：`Sample()` 无条件读计算属性 `VersionInfo.VersionLine`（含 `BuildCommit` 拼接）⇒ 每帧 ≥1 次字符串分配，违反 `ManagedAllocBudgetBytes=0` | 版本行构造期缓存；面板不可见不采样；可见时按 250ms 节拍（`DebugPanel.RefreshDue` 单一份判据）。新增 `presentation.steady_state_zero_alloc`（真层挂缝、预热 90 帧、测 160 帧）与 `presentation.version_line_cached` |
| 2 | `overlay` 打点恒真（`_loop==null` 也打≈0 的点）⇒ 潜在假绿 | 只在真做功时返回 true，未接线一律 false 且不计数 |
| 3 | `Dispose` 只摘了 `EventApplied`，`Fx` 仍挂着 ⇒ 销毁后仍打点 | 四缝全摘；销毁后再跑帧断言三段不再打点 |
| 4 | 面板不可见仍每帧两次 240 元排序 | `FrameProfiler` 分位改惰性缓存（按 `TotalFrames` 失效），`perf.*` 全绿、数值语义不变 |
| 5 | 装配了但没驱动：`Start` 从不设 `Loop.Transport` ⇒ 屏幕流恒 lobby | 接传输 + 新增 `presentation.match_state_drives_flow`（type=10 载荷只走 `loop.OnPacket`）+ `boot.server_config` |
| 6 | 测试自证断言；注释宣称覆盖画质档 cap 但没测 `TickDraw` | 换掉自证断言；新增 `presentation.draw_cap_clips`（tier0 256/2 draws vs tier2 512/4 draws）；摘缝后三条计数都查 |
| 7 | 指针锁未在销毁时复位 | `PresentationLayer.Dispose` 调 `Fps.ReleasePointerLock()`，用例断言 `!Fps.Locked` |
| 8 | `F3` 硬编码绕过键位表 | `SettingsDefaults.ActionDebugPanel=13` + `KeyOf/DebugPanelKeyFor`，设置变更时重解析 |
| 9 | `Results` 每秒重发拉榜请求（服务端 match state 1 Hz） | 按相位变化去重，只在进入 `ended` 时请求一次 |

**变异测试（判别力证据）**：分别破坏 6 处（去掉 overlay 的 `Flow.Apply`、可见时每帧采样、cap=culled、不摘三缝、Results 不去重、版本行不缓存），每次都打红对应用例，之后全部回滚。

**B1 已知缺口（如实登记，未自签规格）**
1. ~~HUD/准星/屏幕流没有渲染器~~ **已做**（`OverlayModel` + `OverlayRenderer`，无显示设备时不画；本机 Null Device 下 `OnGUI` 一帧都没被调用，只证明了装配与绘制项内容/数量）。
2. ~~`GameLoop.LocalPlayerId` 无人赋值~~ **已做**（见 A5），但真连仍受 A8 阻塞。
3. `FrameBench` 仍自建裸 `GameLoop`（不挂呈现层）⇒ 帧基准 JSON 里 `stageP95` 仍只有 4 段、`verdict` 仍 `UNVERIFIED`——这是**诚实**的（不造假数据），但 B1 承诺的"`fx/overlay/draw` 真数字"要等 A1 的 GPU 机器 + 基准挂上呈现层。
4. `Sample()` 在面板可见路径上仍有 4 次/秒的版本行字符串（被缓存的调用方绕开，无用例能红）——目前不在零分配门禁覆盖内。

### B5. JSON 读取去重 ✅
`SettingsStore` 自带的第二套解析（`TryReadFlat`/`Skip`/`ReadString`/`ReadValue`/`ReadKeys`/`ReadVersion`/`ReadInt`/`ReadFloat`/`ReadBool` + 死代码 `Unescape`）删除，读取改用 `Core/MiniJson.cs` + 薄适配；写端未动（613 → 520 行，-165/+72）。行为对既有覆盖等价、在旧实现确实错的地方变严：尾逗号/根对象后尾随内容/前导零/裸控制字符/`NaN`/`Infinity` 现在整份判坏并回落默认值；`\uXXXX`（含代理对）真正解码；`schemaVersion` 超版本现在返回 `int.MaxValue`（只读），不再因 `(int)` 溢出得到垃圾版本被 v1 迁移覆盖。新增 `settings.reads_via_minijson`（15 断言），换回旧实现必红。

### B6. 计划文本漂移 ✅（登记 + 机器守门，不改上游计划）
上游 15 份计划里，多数（C01/C02/C04/C05/C06/C08/C09/C10/C14/C15）本来就写的是统一入口 `Ac.Tests.SuiteRegistry.RunAll`，与实现一致。**4 份计划**点名了独立文件与独立入口（C11 `audio_test.cs`/`AudioTest.Run`/`AUDIO-TEST OK…`、C12 `lobby_flow_test.cs`/`LobbyFlowTest.Run`/`LOBBY-T…`、C13 `settings_test.cs`/`SettingsTest`、C07 回滚里的 `arena_mesh_test.cs`，另 C04 回滚里的 `interpolation_test.cs`），实现统一成 `*Suite.cs` + `SuiteRegistry.RunAll`。

处理方式（**不重写计划、不造空壳入口**——造壳只为对齐名字等于给门禁造假绿）：把对照关系登记在 `docs/evidence/client-plan-alias.md`，并新增守门用例 `plan.alias_table_covers_plans`：从全部计划文本里抽出被点名的测试文件与 `Ac.Tests.<X>.Run` 入口，要求每一个**要么真实存在、要么在别名表里**；表里每条目标必须真实存在、映射的用例前缀必须真的有已注册用例。判别力当场兑现：首次运行就抓到表里漏掉的 `interpolation_test.cs` 变红，补进表后转绿。以后任何人在计划里写一个不存在的测试文件或入口，这条用例立刻红。

---

## C. 已知陷阱（入库的教训）

| 陷阱 | 症状 | 处理 |
|---|---|---|
| **资产 guid 的格式与悬空引用** | 本引擎给**新资产默认生成 56 字符 base64 guid**（B1 的 3 个新 .meta、B6 的新 .meta 都如此），而仓库原先 133 个 .meta 也是这种格式。当时 `GraphicsSettings` 指向的 URP guid `09b520d1…` **在任何 .meta 里都不存在**，直接原因就是这条悬空引用；把它指向 base64 guid 时观察到 Unity 回写 `{fileID: 0}`，但那条路径上还有 `RenderPipelineSetup.Ensure()` 会重置管线，**所以"Unity 清零非 32hex 引用"并未被证明**。 | 现状：全部 .meta 统一为 32 hex（133 + 4 个新资产），引用全部解析、守卫 `assets.guid_references_resolve` 通过。**新资产仍会生成 56 字符 guid，提交前用同一脚本归一**（见下方命令），别手写 guid |
| **无 BOM 的 UTF-8 `.ps1` 含中文** | Windows PowerShell 5.1 按 ANSI 解码，中文字节解出引号 ⇒ 解析失败 | `build.ps1`/`frame-bench.ps1`/`selftest.ps1` 均带 BOM |
| **分配计数器陷阱** | 本机（Tuanjie 2022.3.62t16/Mono）`GC.GetAllocatedBytesForCurrentThread()` **恒 0**：1000 次字符串拼接探针读到 0 ⇒ 仓库里全部"零分配"门禁与 `FrameBench.managedAllocBytesPerFrame` 一度**没有判别力**，C14 表里那行 `0 B/帧` 当时并未被证明。改用 `ProfilerRecorder(Memory, "GC Allocated In Frame")`（逐字节精确：`byte[1024]`→1056、160×`byte[64]`→15360、1000 次拼接→105560）后才知道真值 | 收敛成一份 `client/Assets/Tests/AllocMeter.cs`，13 个测量窗口 + `FrameBench` 全走它；测不到就写 `-1`/`UNVERIFIED`，绝不当"预算内"。修好度量当场抓出**真缺陷**：`FrameProfiler` 的 `Array.Sort` 每次分配 128 B ⇒ 面板每刷新 1152 B/帧，已换成无分配原地排序（分位口径与预算数值未动） |
| **假绿打点** | 没接线也 `Mark` ⇒ 值恒 0 ⇒ 该段预算永远通过 | 打点只在真做功时；`fx/draw/overlay` 三条都有"未接线 ⇒ 不打点"的用例守着 |
| 编辑器副作用 | 打开工程会改 `ProjectSettings.asset`（bundle id、像素密度） | 每次回退、不入库 |
| 并发写入 | 与服务端链共用仓库 | 逐条检查退出码；提交只带自己路径；**fixture 冻结向量被单方面改写会让客户端自测变红（见 A7）** |

---

### 归一 .meta guid 的命令（每次新增资产后跑一次）
```powershell
# 把 Assets 下所有非 32 hex 的 guid 确定性重生（SHA1(旧 guid) 前 32 位），并同步重写引用
# 注意：新资产没有被任何资产引用时最安全；被引用时本步会一并改写引用
```
（脚本实录见提交 `22b452a`；B1/B6 的三个与一个新 .meta 用同样办法归一为 `7207a46b…`/`68555ed4…`/`8ce4fc24…`/`8e6057cb…`。）

## D. 各计划状态

| 计划 | 实现 | §6/DoD | 双轴审查 | 标签 |
|---|---|---|---|---|
| C01、C02 | 早期会话交付 | 已验收 | 已审 | `MC01`、`MC02` |
| C03–C13 | 已交付 | 已验收 | 已审并修复 | `MC03 … MC13` |
| C14 | 已交付（含基准入口 + player 机制入口） | **已达标**：`frameP95/P99` 4.4858 / 4.9969ms（限 20/33），12 项全绿，门禁退 0（A1） | 两轮，blocker 已修 | `MC14` = 可打标签（附注：§5 第 76 行命令仍是 batchmode，机制口径待计划所有人确认） |
| C15 | 发布链路实测出包；版本行/日志已交付 | 构建、日志、门禁已验收；**六步联合验收未做**（A2） | 未跑双轴审查 | **无 `MC15`** |
| 补齐 | B1 呈现层装配、B2 guid 统一、B3 URP 引用、B4 预算单源、B5 JSON 去重、B7 准星调色板、B8 击杀受害者、B9 基准口径、B10 门禁词表、B11 自测入口、B12 打包 guard | — | B1 已跑两轴并修复；其余为审查/审计发现 | — |

提交链：`ac9b9ec` → `099d1f6` → `a2bbe2b` → `42e0260` → `88663c9` → `2bb9d18` → `71c123b` → `982092a` → `22b452a` → `1b36859`（B1）→ `4611f2e`（B5）→ 本轮 B1 修复轮。
