# 客户端链条（C01–C15）收官清单

> 更新：2026-09-30 ｜ 第三轮实跑反馈（A21 移动闪烁、A22 开枪整链）已在 `2b218df` 落地 ｜ 本轮
> 之前的条目：A14 产品侧连接入口、A15 角度表随包、A16 运行期日志落盘、A17 真机复现 C14 门禁
> ｜ 标签 `MC01 … MC15`（`MC15` 见 A2，联合验收已通过）
> 本文每条都有实测出处。A 段是要你出手的，B 段是我能独立做完的。

## 0. 现状

**能跑的**：帧回路（输入 → 预测/和解 → 快照镜像 → 视图同步 → HUD → 特效 → 覆盖层 → 绘制）、音频 mixer、**运行期呈现层装配**（相机/灯光/竞技场/羊群/特效/屏幕流/调试面板）、版本行与**日志落盘**（A16 起真接线）、一条命令出发布包、资源引用守卫、**出包版可用 `server.txt`/`-acserver` 指定服务器**（A14）。
**已上屏**：HUD/准星/大厅/结算/波间/调试面板/局内聊天由 `Ac.UI/OverlayModel.cs`（布局模型）+ `Ac.Boot/OverlayRenderer.cs`（IMGUI 适配层）真画；**身份已通**：昵称键入 → 认领 pid → `GameLoop.LocalPlayerId`/`SnapshotView`/`EntityViews.SetLocalPlayer` → 相机/HUD 绑定；**材质来源已收口**：运行期着色器一律来自 `Assets/Resources/*.mat` 的资产引用（`Ac.View.ArenaMaterials`），不再靠 `Shader.Find` 按名字查（见 A13）；**角度表随包**（A15/ADR-017），出包 player 不再每帧抛异常；**开火反馈已通**（A22：本地武器镜像 `Sim/LocalWeapon.cs` + `GameLoop.PumpWeapon` 当帧判定、`Effects` 曳光闸门与射速间隔有人喂、命中点取权威 `HitX/Y/Z`、HUD 弹药走 `AmmoLedger`、视角吃后坐）。**还不能的**：武器视图网格（C09 无生成器）；IL2CPP 出包（本机只有 Mono 变体）。

| 事实 | 结果 |
|---|---|
| 场景 / 预制体 | 仍为 `0 / 0`——**这是设计选择**：`GameBootstrap` 用 `[RuntimeInitializeOnLoadMethod]` 纯代码自举（不手写 `.unity` YAML） |
| B1 后真正被构造的类 | `Camera`+`FpsCamera`、`LightingRig`、`ArenaMesh`（7 部件/6 材质/5 碰撞盒）、`Materials`、`SheepInstancePool`+`SheepVisuals`（经 `Culling`/`Batching`）、`Effects`、`ViewModel`、`Lobby`/`Results`/`Intermission`（按 `MatchStatePayload.Phase` 驱动）、`DebugPanel`（F3，走键位表）、准星调色板 |
| 帧分段 | `input/sync/predict/hud/fx/overlay/draw` 有生产打点；`audio` 仅在音频设备可用时接线。**打点规则**：只在该段真的做功时 `Mark` |
| 资产 guid | 本轮新增 **1 个** `.meta`（`Resources/trig-table.json`，TextAsset；guid 按 32 位十六进制手写）；`assets.guid_references_resolve` 仍是守门用例 |
| 自测 | `SELFTEST OK cases=210`（A21 前为 203：移动两条；A22 七条：武器镜像/射速/弹药闸/后坐注入/开火当帧/权威命中点/弹药账），零失败 |
| 发布包 | `BUILD OK ac-client-0.1.0+2b218df-win64.zip`（`backend=Mono`；sha256 `7ae1dda4…c6d3f4`，`latest.txt` 复核 `True`） |
| 帧预算（真机 player 口径） | P95 **0.9970** ms、P99 **1.7447** ms、0 B/帧、GC0=0、`drawCalls=51`、`triangles=34917`（限 20/33/0/0/120/180000，见 A17） |

---

## A. 需要你出手

**优先级一览（按"卡住什么"排）**

| 优先级 | 事项 | 一句话 | 卡住什么 |
|---|---|---|---|
| ⏳ | **A22-1 第三轮实跑复核** | 新包 `ac-client-0.1.0+2b218df-win64.zip`（sha256 `7ae1dda4…`）：解压后把 `server.txt` 一行写成 `43.143.120.65:8788`（或 `angry-chen.exe -acserver 43.143.120.65:8788`）→ 看四件事：① 走路不再闪烁/顿卡 ② 视角跟手 ③ 按下左键**当帧**见枪口火焰 + 曳光 ④ HUD 弹药当帧掉数、命中点在羊身上（不是空地/身体中段） | 只有你能上机；服务端**已探活**（下一条） |
| ✅ | ~~服务端在线预检（2026-09-30）~~ | 从本机探 `43.143.120.65:8787`：`/health` **200** `{"status":"ok","protocolVersion":1,"rooms":1,"connections":0,"uptimeSeconds":34357}`、`/metrics` `ac_server_version{version="0.1.0",protocol="1",tick_ms="50"} 1`、tick 绝对时刻误差 P95 `1.000ms` ⇒ 版本行与客户端 `proto=1` 匹配，**不需要重部署服务端** | 已闭环 |
| **P4** | **A22-2 服务端输入陈旧超时（可选，本轮未做）** | 客户端上行连续丢包时服务端会一直沿用最后一条命令（窗口上界约 1s）⇒ 只在"丢包严重"的链路上表现为"被拉回"。要修就在 `server/src/room/room.cpp` 的输入受理处加「≥6 tick（300ms）没有新 `clientTick` ⇒ 本 tick 的移动/按键清零、保留 Ready」，按 S 链出用例与证据后重部署 | 只在实跑确认还有"网络抖动被拉回"时才值得做（要你重部署服务端） |
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
| ✅ | ~~A13-1 Shader.Find 出包剥离（ADR-014 §后果-1 的正式修法）~~ | 材质改为对 `Assets/Resources/*.mat` 的资产引用（`Ac.View.ArenaMaterials`），全仓 `Shader.Find` 只剩两处兜底 | 已闭环（`assets.material_shader_reference` + `assets.material_shader_runtime` 守着） |
| ✅ | ~~A13-2 局内聊天 UI~~ | 键位表第 11 条 `chat`（一直是死绑定）接上：输入缓冲 + 左下角渲染 + 打字期间挂起意图 | 已闭环（4 条 `chat.*` 用例） |
| ✅ | ~~A13-3 产品侧连接参数入口~~ | 已裁决并落地（ADR-016）：`-acserver` → `AC_SERVER` → exe 同级 `server.txt` → `-server` → 默认；**默认端口 8787（HTTP 面）纠正为 8788（游戏面 UDP）** | 已闭环（`boot.server_config`，附变异打红） |
| ⏳ | **A13-4 真机复核** | **本轮已做一半**：真机（RTX 4060 Ti / 1920×1080）出包 + `frameP95/P99` 门禁**已独立复现 PASS**（0.9970/1.7447 ms，见 A17）、无头 195 例全绿（含 `InputCameraSuite` 的用例）；**仍未做**：IL2CPP 出包（本机只有 Mono 变体）、真人试玩验收 | 真人试玩 + IL2CPP 模块 |
| ✅ | ~~A15 角度表随包~~ | 出包 player 里没有仓库根 ⇒ 修前**每帧**抛 `TrigTableException`（预测整条废掉）；ADR-017 把表作为 `Resources/trig-table` 资产随包 + 镜像门禁 | 已闭环（194 例 + 真机 0 异常） |
| ✅ | ~~A16 运行期日志落盘~~ | `LogSink` 只有用例在用、出包版一个文件都不落；本批把引擎日志通道接上（`logs/client-<date>.log` + `crash-*.log`） | 已闭环（195 例 + 真机出文件） |

### A23. ✅ 移动画面"来回抖动"归零：重放被步数对齐的权威投影取代（A21 残留①③闭环）

**现象**（第三轮实跑 + 本机复测）：按住 W 移动时画面按 ~20Hz 周期性地"慢一帧、快一帧"地抖，
不是位置回退。A21 把根因登记为"快照可能落在本 tick 子步之前，模拟误差按相位在 `0.005/0.225m`
之间交替"，当时判为 ≤ 一个子步、由平滑器吸收、不在那轮修。

**根因（本轮实测定位）**：误差交替的源头是**重放的步数无法与权威的 tick 数对齐**，而不是
"快照落在子步之前"：

| 场景 | 实测（修复前，60fps 渲染步长 0.075m 为标准） | 机制 |
|---|---|---|
| RTT 5–10ms（无丢包） | 波纹 **18mm**（`0.057/0.093m` 步长交替） | ack（服务端最新收到命令）当区间就覆盖"预测正在用、刚入缓冲"的命令 ⇒ `AckUpTo` 把它们裁掉 ⇒ 重放比预测少一步 ⇒ 每个和解都假性整步 delta |
| RTT 30±15ms 抖动 | 原实现干净（0.8mm），replay-all 变体回归 29mm | 重放条数 = 到达区间的子步数（1~2 不定），权威固定 1 tick 推进 ⇒ 0/1 子步相位差随到达抖动进入误差 |
| RTT 5ms + 5–20% 丢包 | **41–49mm** | ack 停滞 ⇒ 缓冲跨区间累积 ⇒ 重放多步 ⇒ 状态持续超前权威 |
| RTT 30±15ms + 5% 丢包 | 原实现干净（1.1mm） | — |

**修法**：`GameLoop` ③ 在调 `Reconcile` 前把权威位置**按步数差外推**——权威 tick `T` 的位置是
服务端 `T−1` 步，客户端此刻已预测到 `_predictionSteps` 步，于是

```
extraSteps = _predictionSteps − (T−1)
authority += 本地速度 × extraSteps × 50ms   // 只动位置，不改 tick/ack
```

`Reconciler.Reconcile` 落地投影后的基线、清空缓冲，**不再** `AckUpTo` + 逐条重放（C06 §5(b)
的"落地权威姿态 → AckUpTo → 最旧到最新逐条重放"由此修正，重放被投影取代）。步数对齐与预测
网格同构：恒定移动（含丢包——服务端 `session->command` 持最近命令、丢包不缺步）下投影位置 ≡
本地预测位置，误差恒 ≈ 0.005m；输入真的变了（转向/变速）才出现误差，该纠正的纠正。
墙钟年龄（`now − serverTimeMs`）投影在到达抖动下仍残留 0.005~0.155m 相位差，步数对齐无残留。

**验证**（`JitterSim` 全矩阵：60fps、oneWay 0/5/10/20/30ms、抖动 0/15ms、丢包 0/5/20%）：

| 场景 | 修复前波纹 | 修复后波纹 |
|---|---|---|
| 无丢包（全 RTT/抖动组合） | 0.8–18mm | **≤ 0.9mm**（量化底） |
| 5% 丢包 | 41–46mm | **≤ 1.2mm** |
| 20% 丢包 | 49mm | **≤ 1.2mm** |
| 停止后 | — | 0 |

**用例**：`ReconcileSuite.reconcile.replay_order` 改为断言"落地投影基线、`Replayed==0`、缓冲
清空"；`BootSuite.boot.local_pose_from_prediction` 的"重放条数恒 2"改为"`Replayed` 恒 0"，
领先量断言改为"渲染贴预测轨迹（权威 ± 外推）"；`boot.no_camera_flicker` 的上界保留。自检
`cases=232` 与基线同口径（4 个既有 net/codec/hud 环境失败不变，零新增）；`movement-check.ps1`
16 项全绿。

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

### A13. ✅ v2 收尾三件（本轮：ADR-014 的正式修法 + 局内聊天 + 文档纠错）

**本轮只碰不需要开 Unity 界面的活**；真机复跑、`InputCameraSuite` 执行、`frameP95/P99` 门禁、IL2CPP 出包仍**未做**（见文末"仍未复核"）。

| # | 事项 | 落点 | 钉住它的用例 |
|---|---|---|---|
| 1 | **ADR-014 §后果-1 的正式修法**：运行期材质不再 `Shader.Find` 按名字查（出包被剥离 ⇒ player 里 `materialsReady=false`） | 新增 `Assets/Resources/ArenaUrpLit.mat` + `ArenaUrpLitInstanced.mat`（与 `FrameBenchUrpLit.mat` 同源、同一 URP Lit shader guid `933532a4…`）；新增 `Ac.View.ArenaMaterials`（`Resources.Load` 取资产引用，取不到才退按名字查，成功与否可观测）；`Materials.Bind` 与 `PresentationLayer.MakeInstancedMaterial` 都改走它 | `assets.material_shader_reference`（盘上：两资产存在、guid 合法且互不相同、都引用同一 URP Lit、实例化标志只开在实例化那张）、`assets.material_shader_runtime`（运行期：`Shader != null` 且 `ResolvedFromAsset == true`，不许退到按名字查） |
| 2 | **局内聊天 UI**：键位表第 11 条 `chat`（默认 `Return`，一直在表里但**全仓无消费方**）之前是死绑定 | `SettingsDefaults.ActionChat = 11`（与 `ActionReady=14` 同键是刻意的，相位互斥）；`SettingsPanel.ActionNames` 补第 15 条"准备"（此前 14 条 vs `ActionCount=15` 不对称）；`Chat` 补输入缓冲（`Focus`/`Apply`/`Feed`/`Submit`）；`GameLoop.ChatVisible` 按相位（`Hud.CombatUiVisible`，读 `_phase` 不读帧末才写的 `_sample.Phase`）；`ScreenFlow.Chat` + `OverlayModel.BuildChat`（左下角，底边抬起 96px，**不挪动任何既有绘制项**）；`GameLoopDriver` 接线 | `chat.input_buffer`、`chat.open_and_submit`、`chat.escape_closes`、`chat.overlay_lines` |
| 3 | **`InputSampler.Suspend/Resume`**：打字期间意图按 0，但 30Hz 上行照发零意图（不改 `Focused`，与失焦清理解耦）；`OnFocusChanged(true)` 在挂起期间不许把意图放回来 | `InputSampler.cs` | 由 `chat.*` 与既有 `input.lock_flush` 共同覆盖 |
| 4 | **文档纠错**：本文 §D 表此前仍写"C15 六步联合验收未做 / 无 `MC15`"，与本文 A2 的盖章自相矛盾；`client-v2-frame-acceptance.md` §3 的"未验收"结论同样只挂着一条取代注 | 本文 §D 的 C15 行、文首标签行；`client-v2-frame-acceptance.md` §3 | 无（文档） |

**时序坑（本轮实测踩到，已修）**：`chat` 键按下的那一帧，`Input.inputString` **已经带上了那个 `'\n'`**。若把"回车"当开关、又把 `'\n'` 喂进缓冲，同一次回车就变成"开 → 立刻发一条空消息 → 关"。修法是 `Chat._pendingOpen`：**只吃开户那一帧**的前导换行，之后的每一帧回车都是"发送"。反面教训同样实测过：把"吃前导换行"写成无条件的，回车就永远发不出去（第一版就是这么红在 `chat.open_and_submit` 上的）。

**状态：`SELFTEST OK cases=193`（本轮新增 6 条：`assets.material_shader_reference`、`assets.material_shader_runtime`、`chat.input_buffer`、`chat.open_and_submit`、`chat.escape_closes`、`chat.overlay_lines`；适配前基线 187 例，零回归）。**

### A14. ✅ 产品侧连接参数入口（ADR-016）—— 含一处**默认端口错到 HTTP 面**的纠错

ADR-014 §后果-2 把"产品侧连接参数入口"挂成"仍未做"：`-server` 在出包版里被 Unity 播放器自己吃掉，
产品侧只剩 `AC_SERVER` 环境变量一条通路。本轮把这条做完，并在做的时候挖出一个**默认值级**的产品缺陷。

| # | 事项 | 落点 | 钉住它的用例 |
|---|---|---|---|
| 1 | **默认端口 8787 → 8788**：8787 是 HTTP 诊断面（TCP），游戏面是 UDP **8788**（`server/README.md` §18.2）——客户端原先拿着 8787 去发 UDP `Hello`，`HelloAck` 永远回不来（出包版"连不上"的成因之一）。没被更早发现是因为：编辑器/批处理一律不连、六步联调每次显式传 `AC_JOINT_UDP=127.0.0.1:8788`、机器人用的是自己的 `--port` | `GameBootstrap.DefaultServerPort` | `boot.server_config`（`== 8788` 且 `!= 8787` 两条断言） |
| 2 | **产品侧入口**：解析顺序 `-acserver host:port` → `AC_SERVER` → **exe 同级 `server.txt`** → `-server` → 默认 `127.0.0.1:8788`。`-acserver` 不是任何播放器开关（`-server` 是），原样落到 `Environment.GetCommandLineArgs()` | `GameBootstrap.ChooseServer` / `FirstConfigLine` / `ServerSource` | 优先级四条 + "四条都空→默认" + "第一个非空解析失败即离线" |
| 3 | **`server.txt` 的读法**：exe 同级（出包版取 `Application.dataPath` 父目录）、空行与 `#` 注释跳过、**UTF-8 BOM 剥掉**、读坏了当没配 | 同上 | BOM / CRLF / 注释 / 两侧空白四种写法 |
| 4 | **可观测**：`ServerSource` 记录生效来源，真连前打一条 `Ac.Boot: 连接 host:port（来源 …）` | `GameBootstrap.ResolveServer` | —— |
| 5 | **分发模板**：`client/server.txt.example`（复制成 `server.txt` 放在 exe 同级，改一行即可）；`client/.gitignore` 忽略本地的 `server.txt` | 仓库文件 | `git check-ignore -v client/server.txt` 命中 |

**判别力证据（本轮）**：把第 106 行的 `DefaultServerPort == 8788` 临时改成 `== 8787` 后重跑自检 →
`FAIL boot.server_config expected=默认端口 = 游戏面 UDP 8788（server/README §18.2 的 AC_UDP_PORT） actual=8788`、
退出码 1；回滚后 `SELFTEST OK cases=193`（与 A13 基线同数，零回归）。**改动只碰客户端"连哪儿"**：
wire 格式、量化、握手时序与 `server/**` 一个字没动。

### A15. ✅ 出包阻断级：角度表没随包（出包 player 每帧抛 `TrigTableException`）—— ADR-017

**怎么发现的**：A14 做完后拿**真出包 player**（`client/Build/Windows64/ac-client-0.1.0+0d2bc6f-win64.zip`，
Mono 后端）跑了一次"改 `server.txt` 就换服务器"的实跑复核，第一屏日志除了正确的那条连接行，还跟着
**每帧一条**异常：

```
Ac.Boot: 连接 43.143.120.65:8788（来源 server.txt）
TrigTableException: 角度表缺失：C:\Users\…\Temp\docs\evidence\fixtures\trig-table.json
  at Ac.Sim.TrigTable.LoadFromFile → get_Shared → LocalStep.StepLocalPlayer → Predictor.StepWith → GameLoop.Frame
```

15 秒的 `Player.log` 涨到 **81 KB**。含义：**出包版的本地预测链路整条是废的**（每帧抛异常、没有预测、
玩家本地移动不可能对）。编辑器、批处理自检、六步联调都看不见 —— 三者都在仓库里跑，`RepoPaths` 能往上找到仓库根。

**根因**：C02 §5.6 只说"两侧共用 `docs/evidence/fixtures/trig-table.json`"，并把打包留给 C15；
C15 交付了出包链路但**这条留白没补**，`RepoPaths` 是"从工程根往上找仓库目录"的定位器，而 player 里没有仓库根。

**裁决与落地（ADR-017）**：把表作为 Unity `TextAsset` 随包（`Assets/Resources/trig-table.json`，
资产名 `trig-table`），Boot 层装配时喂进 `Ac.Sim.TrigTable`（`Ac.Sim` 自己不引 UnityEngine）；
镜像由 `tools/export-trig-table.mjs` 一次写两份、`--check` 双侧逐字节比；客户端新增用例
`quantize.trig_table_packaged` 在运行期再比一次并断言装配真能把表装上。三道门任意一道都能抓到"只改一侧"。

**真机复核（同一天、同一个包路径）**：重新出包后同一个场景 `Player.log` **0 条 `Exception`**、整份 **1.1 KB**
（修前 81 KB）；`client/Assets/Resources/trig-table.json` 与 `docs/evidence/fixtures/trig-table.json` 逐字节相同。

### A16. ✅ 运行期日志落盘从未接线（运维手册承诺的文件一个都不落）

`LogSink`（C15 §5：`logs/client-<yyyyMMdd>.log`、8 MiB 轮转保留 5 份、`crash-<yyyyMMdd-HHmmss>.log`）
**全仓只有用例引用**，生产路径没有构造者 ⇒ 出包版跑完，`%USERPROFILE%\AppData\LocalLow\AngryChen\angry-chen\logs\`
根本不存在（A14/A15 的两次真机跑都证实了这一点）。本批接线：

| 环节 | 落点 |
|---|---|
| 装配 | `GameBootstrap.Start()` 第一件事调 `ClientLog.Attach(Application.persistentDataPath)`（幂等） |
| 通道 | 订阅 `Application.logMessageReceived`（**只订阅不拦截**，Unity 自己的 `Player.log` 照旧） |
| 级别 | `Log/Warning/Error/Assert/Exception` → 小写 `log/warning/error/assert/exception` |
| 崩溃 | `Exception`/`Assert` 另写 `crash-*.log`（首行版本行、≤200 行），**同一段异常只写一份**（上限 16 份）——出问题的那一帧会每帧抛，不设限会把日志目录刷爆 |
| 失败姿态 | 落盘不可用只记一条 warning，客户端照常能玩 |

**真机证据**：重新出包后跑 15 秒，`logs/client-20260929.log` 出现（192 行，含 player 自己的
`连接 …（来源 server.txt）` 与那条身份 warning，每行都带 `version":"ac-client 0.1.0+0d2bc6f proto=1"`），
无 `crash-*.log`（因为已无异常）。用例 `boot.client_log_wiring` 钉住：根目录、`msg`/`level`/`version` 三个字段、
crash 首行是版本行、同段异常只写一份、`Detach` 之后不再落盘。

### A17. ✅ 真机复核：C14 帧门禁独立复现 PASS（并修掉门禁脚本的一处死路）

本轮在一台**有图形设备**的机器（RTX 4060 Ti / 1920×1080 / quality tier 2）上重新出包并复跑
`pwsh -File client/tools/frame-bench.ps1 -Runs 3 -Frames 600`：

```
FRAME-BENCH PASS p95=0.996999999999844ms alloc=0B mechanism=player emptyP95=0.664699999999812ms editorP95=32.503999999999ms editorVerdict=FAIL
```

`frameP95` **0.9970 ms**（限 20）、`frameP99` **1.7447 ms**（限 33）、0 B/帧、`drawCalls=51`（限 120）、
`triangles=34917`（限 180000）、`particles=256`（限 256）、`materials=8`（限 24）、8 段 `stageP95` 全在
（最大 `draw` 0.0548 ms）、`missingStages=[]`、空场地板 0.6647 ms < 20 ms ⇒ 机制不变式成立。

**顺带修掉的工具死路**：ADR-014 引入的"同轮空场地板"对照跑**按设计**不装配呈现层，于是图形四项与
`engineFrames` 都是"未测得"（`-1` / `0`），而 `frame-bench.ps1` 的 fail-closed 检查把它们一律判成
"这台机器没有图形设备" ⇒ **`-Mechanism player` 这条路恒退出 2、永远给不出判定**（这解释了本轮第一次复跑
为什么是 `ENV` 退出）。修法是给这一次跑加 `-FloorProbe`：只放宽"图形四项"与"引擎帧计数"两条
（ADR-014 裁决 4 只要求它回答"地板是否低于预算"），**仍要求真图形设备与 `P95 < 预算`**；
被判定的三轮一项都不放宽。明细见 `docs/evidence/client-v2-frame-acceptance.md` §4。

### A22. ✅ 第三轮实跑反馈（续）：「开枪」整条反馈链四处断线（枪口火焰迟一秒 / 没有曳光 / 命中点不对 / 弹药等一秒）

A21 修完移动之后，用户要的下一件事是"像 CS 一样正常开枪"。查下来开火这一路**不是慢，是断**：
本地没有任何"这一枪响了"的判定，特效层与 HUD 各自靠一条不可靠的间接推断活着，而三处关键接线
在生产里**根本没有调用者**（全仓只有用例在调）。同样全部落在已冻结契约内（C06 §5(c)、C09 §5/§6、
S09 命中点、C01 §5 事件域）⇒ 不新增 ADR、不动协议／量化／握手。

| # | 冻结契约 | 根因（实测） | 修法 |
|---|---|---|---|
| F1 | 开火反馈必须当帧（C06 §5(c)、C09 §5） | 客户端**没有本地开火判定**：`PresentationLayer.UpdateWeaponView` 只能用"弹匣变小"推断开火（`_lastMagForKick`），而权威弹匣在 **1Hz** 的 MatchState 里 ⇒ 枪口火焰/坐力最多迟 1 秒，且任何一次权威纠正都会误触发 | 新增 `Sim/LocalWeapon.cs`（逐条复刻 `server/src/combat/weapon.cpp` 的 `resetWeaponState/updateWeapon/tryFire/tryStartReload/switchSlot`）+ `GameLoop.PumpWeapon()`：每帧按采样器的**当前**按钮位驱动、开火当帧置 `LocalShotFired` |
| F2 | 曳光闸门由弹药账给（C09 §5(c)/§6） | `Effects.SetAmmoGate` / `SetFireIntervalMs` **没有任何生产调用者**（全仓只有 `EffectsSuite` 在用）⇒ `AmmoGate` 恒 `0` ⇒ `Effects.SpawnTracer` 每一发都走 `RejectedShots` 分支 ⇒ **曳光一条都不会出现**（枪口火焰来自坐力曲线 ⇒ 实跑症状就是"看得到闪、看不到弹道"） | `PresentationLayer.TickFx` 每帧喂闸门与间隔；闸门取本地镜像**开火前**的弹匣（否则"最后一发"会被自己"打完归零"闸掉），间隔取 `WeaponTable.FireIntervalMs`（与服务端同一条公式） |
| F3 | 命中反馈落在权威命中点（S09 `HitX/Y/Z`） | `GameLoop.ApplyEvent` 把 `EventEntry` 转 `HudEvent` 时**丢掉了 `SubjectId` 与 `HitX/Y/Z`** ⇒ 弹着特效只能挂目标实体中心（爆头反馈落在身体中段），曳光终点也只能按视线外推 30m | `HudEvent` 增 `SubjectId/HitX/HitY/HitZ`；`OnEventApplied` 优先用权威命中点（三分量全 0 才退回实体中心）；`Tracer.RetargetNewest()` + `Effects.RetargetNewestTracer()` 把最近一段曳光改到该点 |
| F4 | 弹药显示 = 权威 − 未确认开火（C06 §5(c)） | `AmmoLedger` 同样没有任何生产调用者，`FillSample` 直接把 1Hz 的 `MatchState.mag` 铺上 HUD ⇒ 打完一发要等最多 1 秒才掉数 | `FillSample` 改走 `AmmoLedger.Reconcile(...)`；快照路径补 `NoteServerAck(authority.LastAckedSeq)`，重连 `Reset()`；`LocalWeapon.SyncAuthority` 只把本地镜像**拉低**（1Hz 的旧值不许把打掉的子弹还回来） |
| F5 | 后坐 0.35°/±0.2°、衰减 7.5/s（C09 §5） | 后坐只做在**视图模型**上（`WeaponAnim`），视角不吃后坐 ⇒ 服务端射线方向与玩家看到的后坐无关（跟 CS 不一致） | `InputSampler.ApplyAimPunch()`：开火当帧顶视角（下一帧上行即带上，服务端射线跟着后坐走），按 7.5°/s **线性**回正，且只回正"后坐抬起来的那部分" |
| F6 | 用例缺口 | 帧回路 → 开火 → 特效／弹药这条缝没有集成用例 | 新增 7 条：`weapon.mirror_values`、`weapon.local_fire_interval`、`weapon.ammo_gate`、`camera.recoil_punch`、`presentation.fire_same_frame`、`presentation.hit_uses_authoritative_point`、`presentation.ammo_optimistic_then_authority` |

**客户端 ↔ 服务端镜像值登记**（跨语言冻结值只有一份来源：`server/src/config/weapons.hpp`；
客户端的落点是 `Sim/WeaponTable.cs`，改任何一项都必须两端同改，见 ADR-010 §7）

| 服务端符号 | 值 | 客户端符号 |
|---|---|---|
| `kWeapons[].mag` | 12 / 30 / 6 | `WeaponTable.MagSize` |
| `kWeapons[].rpm` | 300 / 600 / 70 | `WeaponTable.Rpm` |
| `kWeapons[].pellets` | 1 / 1 / 8 | `WeaponTable.Pellets` |
| `kWeapons[].reloadMs` | 1400 / 2000 / 2600 | `WeaponTable.ReloadMs` |
| `kWeapons[].spreadDeg` | 0.8 / 0.6 / 4.0 | `WeaponTable.BaseSpreadDeg` |
| `kSpreadGrowthPerShotDeg` / `kSpreadMaxDeg` | 0.1 / 0.25 | `WeaponTable.SpreadGrowthPerShotDeg` / `.SpreadMaxDeg` |
| `kSpreadDecayDelayMs` / `kSpreadDecayPerSecondDeg` | 350 / 6.0 | `WeaponTable.SpreadDecayDelayMs` / `.SpreadDecayPerSecondDeg` |
| `kRageFireRateMultiplier` | 1.25 | `WeaponTable` **未镜像**（本地按 1.0 计时，见残留①） |
| `kReserveAmmoInitial` | 120 | `WeaponTable.ReserveAmmoInitial` |
| `kRecoilPitchPerShotDeg` / `kRecoilYawJitterDeg` | 0.35 / 0.2 | `WeaponTable.RecoilPitchPerShotDeg` / `.RecoilYawJitterDeg`（C09 §5 同值） |
| `kAimPitchLimitRad` | 1.5533 | `WeaponTable.AimPitchLimitRad`（**射击方向**的夹取；采样器的瞄准夹取是 π/2，两者差 1°） |
| `kShotMaxDistanceM` | 160 | `WeaponTable.ShotMaxDistanceM` |
| `kWeapons[].damage` / `falloff*` / `headshotMultiplier` / `isAuto` | — | 刻意不镜像（纯服务端结算；`isAuto` 服务端也不用，只靠"按住 = 持续开火 + 射速间隔"把关） |

**实跑前预检（2026-09-30，从本机探生产服务端）**：`43.143.120.65:8787` 可达；`/health` **200**
`{"status":"ok","protocolVersion":1,"rooms":1,"connections":0,"players":0,"uptimeSeconds":34357}`
（uptime ≈ 9.5h = 上一轮部署仍在跑）；`/metrics` 首行 `ac_server_version{version="0.1.0",protocol="1",
tick_ms="50"} 1` ⇒ 与客户端 `ac-client 0.1.0+2b218df proto=1` 的 `proto`/`MAJOR.MINOR` 一致；
`ac_tick_schedule_error_ms_p95 1.000`（20Hz 的 5%）。**结论：本轮只需换客户端，服务端不用动**（Phase C
的输入陈旧超时见优先级表 A22-2，是可选后续）。

**验证（变异打红：逐条把修法改回缺陷，看用例是否真的红）**

| 变异 | 实测（红的用例与数值） |
|---|---|
| `WeaponTable.Pellets[2]` 8→7 | `weapon.mirror_values`：`expected=8 actual=7` |
| `LocalWeapon.TryFire` 去掉射速闸（`nowMs < nextFireAllowedAtMs` 一行） | `weapon.local_fire_interval`：2 秒打 `13` 发（修后 `10`）；集成侧连带 `presentation.fire_same_frame`：第二发落在第 `2` 帧（修后 9–11）；`presentation.ammo_optimistic_then_authority`：`expected=11 actual=10`（每帧都在开火） |
| `LocalWeapon.TryFire` 弹匣闸 `<=0` → `<0` | `weapon.ammo_gate`：`expected=12 actual=13` |
| `GameLoop.LocalFireRateMultiplier` 1.0→2.0 | `presentation.fire_same_frame`：第二发落在第 `6` 帧（修后 9–11） |
| `ApplyEvent` 不转发 `HitX/Y/Z` | `presentation.hit_uses_authoritative_point`：`expected=1 actual=0`（`HitRetargets` 恒 0） |
| `FillSample` 直接铺 `players[i].Mag`（绕过弹药账） | `presentation.ammo_optimistic_then_authority`：`expected=11 actual=12` |
| **实现期真实缺陷**：`DecayRecoil` 把 `7.5°/s` 当 `7.5 rad/s` 用 | `camera.recoil_punch`：`expected=回正在进行但没做完 actual=0`（一帧就把后坐还完 = 视角完全不抬）——这条是**先红后修**（不是事后补的变异），换算写成 `RecoilDecayPerSecond * DegToRad * dtMs / 1000` |

修后全绿：`SELFTEST OK cases=210`（A21 时为 203，七条新用例）。

**已知残留（登记，不在本轮修）**：① 本地开火镜像按 **1.0** 倍率计时、**不镜像 rage 的 1.25×** ⇒ 狂暴窗口里
本地射速比服务端慢 1/5，多出来的那几发由权威弹匣拉回（不产生"幻影开火"，只是本地少放几发特效）；
② 回执到达前曳光终点按视线外推 30m（`Tracer.FallbackEnd`），`PlayerHit` 一到就改到权威命中点（打 3m
外的目标时那条长条存在 1–3 帧）；③ 本地**不做命中判定**（射线/回溯全在服务端 S08/S09）⇒ "打没打中"
完全由 `PlayerHit` 决定，曳光/弹着点在 RTT 内不是权威；④ 开火按**帧**驱动而不是 50ms 子步：射速由
`nextFireAllowedAtMs` 把关（帧率不改变射速，只改变"按下到出膛"的粒度），而上行仍是 30Hz 采样 ⇒
服务端那一格可能比本地晚最多一条命令（弹药账与命中回执各自把这点吸收掉）。

### A21. ✅ 第三轮实跑反馈：移动「疯狂闪烁 / 来回闪烁」+ 视角迟滞（预测—和解链路四处回归）

用户复述的症状是"移动起来巨卡无比，移动一下就会疯狂闪烁，来回闪烁"。四处问题全在客户端，且都是
**对已经冻结的契约的回归**（C05 §5.4/§5.5/§8、C06 §5(b)/§8、C04 §5.5）⇒ 不新增 ADR、不动协议／
量化／握手；修法只是把实现改回契约描述的行为。

| # | 冻结契约 | 根因（实测） | 修法 |
|---|---|---|---|
| R1 | 本地玩家渲染用预测姿态（C05 §5.4） | `EntityView.HasPrediction / PredictedX/Y/Z / PredictedYawRad / PredictedPitchRad` 在生产代码里**没有任何写入者**（全仓只有 `Tests/InterpolationSuite.cs` 在用），`EntityViews.SyncFrame` 因此恒走"按最新帧吸附"那一支 ⇒ 本地玩家只能按 20Hz 快照跳格 | `GameLoop.PublishLocalPrediction()`：每帧把 `Predictor.RenderPosition()`（50ms 子步 + 渲染外推）写进视图，并在没有本地实体时清标志 |
| R2 | 相机 yaw/pitch 直接取自本机命令姿态（C05 §5.5） | `SyncCamera` 读 `local.YawRad/PitchRad`，而那份值来自权威快照 ⇒ 视角被 RTT + 50ms 拖住（"鼠标迟滞"） | 同一处写入点：预测 yaw/pitch 取**采样器当前值**（与上行命令同一份）；`SyncCamera` 无需改动 |
| R3 | 重放只在收到权威快照时发生（C06 §8）；缓冲每条 = 一个 50ms 子步（C06 §5(b)） | `Frame` 每帧都调一次 `Reconcile`，且每帧只 `Push` 一条命令（按帧率 ≈ 2× 于 20Hz 步进）⇒ 重放条数系统性多一倍（实测本场景每 tick 2 帧：重放 `4` 步而不是 `2` 步） | 和解改为"**已应用快照的 tick 变过**才做一次"（判据取 `SnapshotView.AppliedTick`，不用"本帧收到过包"——装配根/帧基准直接喂快照的那条路同样要和）；命令缓冲改为每个子步进入队一条（`Advance` 返回的步数即条数） |
| R4 | 给平滑器的是「和解前 − 重放后」（C06 §5(b) + C04 §5.5） | `Frame` 传的是 `predictor.State - result.Offset`（= 2×重放后 − 和解前），每帧往偏移里灌进约一个帧位移量级；量级越过 `SnapThresholdM = 1.0` 就 `Reset()` 归零 ⇒ 渲染位置在「权威+大偏移」与「权威+0」之间来回跳 = **用户看到的来回闪烁** | 新增 `EntityViews.ApplyPoseDelta(view, dx, dy, dz)`：把**姿态位移量**（`ReconcileResult.Offset*`）交给 `ErrorSmoother.Apply`（在旧偏移基础上累加），而不是拼一个"位置"出来 |
| R5 | 用例缺口 | GameLoop → 视图／和解／相机这条缝**一条集成用例都没有**（前两轮的真实缺陷都从这条缝漏出去） | 新增 `boot.local_pose_from_prediction`、`boot.no_camera_flicker`（含"确实和解过"的判据 `Reconciles`／`LastReplayed`，避免平滑断言在没和解时真空通过） |

**验证（变异打红：逐条把修法改回缺陷，看用例是否真的红）**

| 变异 | 实测（红的用例与数值） |
|---|---|
| R4：`ApplyPoseDelta` 换回 `ApplyCorrection(local, State − Offset)` | `boot.no_camera_flicker`：单帧位移 `0.6683` > C05 §8 上界 `0.315`，轨迹里出现 `9.312 → 9.254` 的**回退**（＝来回闪烁在用例里复现）；`boot.local_pose_from_prediction`：领先量 `0.70..1.28`（修后 `0.27..0.49`） |
| R3：命令缓冲改回每帧一条 | `boot.local_pose_from_prediction`：重放条数恒 `4`（修后恒 `2`） |
| R3：和解改回每帧一次 | 两条用例同时红：`Reconciles = 80`（修后 `40`＝一 tick 一次） |
| R1/R2：去掉 `PublishLocalPrediction()` | `boot.local_pose_from_prediction`：`68` 帧没有预测值（修后 `0`），领先量退回 ≈0 |

修后全绿：`SELFTEST OK cases=203`（A20 时为 201，两条新用例）。

**已知残留（登记，不在本轮修）**：~~① 快照可能落在本 tick 子步**之前**，于是模拟误差按相位在
`0.005/0.225m` 之间交替（≤ 一个子步）——平滑器把它吸收成渲染偏移，**渲染位置保持连续、无回退**，
用例按 C05 §8 的"一个子步"上界钉住~~（**已在 A23 闭环**：根源是重放步数与权威 tick 无法对齐，
改为步数对齐的权威投影后误差恒 ≈0.005m）；② `Predictor.Advance(int dtMs)` 会把 16.67ms 截断成 16ms，
客户端时钟比权威慢约 4%（稳态留下一个小偏置）——改签名属 C05 冻结面，另立后续项；~~③ 30Hz 采样 /
20Hz tick 时 ack 可能多追一条 push，重放少一步（≤0.315m），同样由平滑器吸收~~（**已在 A23 闭环**：
重放与 ack 裁剪都被投影取代，见 A23）。

### A20. ✅ 第二轮实跑反馈（移动一顿一顿 / 灵敏度 / 有枪 / 羊）+ 美术细节

| 反馈 | 根因（实测） | 修法 |
|---|---|---|
| **移动一顿一顿** | 客户端只能报"最近一条已应用快照的 tick"（20Hz 快照 / 50ms 一格），命令按 30Hz 上行、下一格才到；16ms RTT 实测按 W 8 秒 `ac_dropped_frames_total +103`（**占上行 43%**），连按键一起丢 ⇒ 预测被和解反复拽回 | S11 §5 的 tick 校验改为窗口 `[serverTick - 2, serverTick]`（100ms，ADR-018），未来 tick 仍拒收；**修后同一条实测：`frames_in +307`、`dropped_frames +0`** |
| **鼠标灵敏度巨低** | `MouseRadPerPixel = 0.001` + 默认灵敏度 1.0 | 0.001 → 0.0035、默认 1.2、上限 3.0 → 5.0 |
| **手里没有枪** | C09 的 ViewModel 只有锚点、没有网格 | 新增 `Ac.View.WeaponMesh`（三套程序化网格，ADR-003 零素材）+ 装配：待机摆动 / 行走摆动 / 开火后坐力 / 枪口火焰 + **枪口动态光 + 抛壳**；锚点 `OffsetZ≈0` 会把枪投影到屏幕外（实测完全看不见）⇒ 前推 0.42m、横向 ×0.6、放大 1.55×、侧转 13° |
| **羊"根本不像羊"** | 羊毛团是 8 面体（出包=一堆白方块），头/腿是悬空小方块 | 羊毛团改 UV 球（蛋形躯干），加深色头 + 嘴 + 双耳 + 眼 + 尾，四条腿连到地面（两截 + 蹄），羊王加皇冠三角；按羊形分化（撞角羊更壮角更粗、冲刺羊更高更瘦、羊王全面放大），并在 §5(a) 的 512 顶点/512 三角形预算内 |

同轮修掉的实跑缺陷：**服务端重启后客户端永久卡死**（新世界 tick 从 0 起，客户端旧高 tick 让快照过不了单调过滤、命令全判 `kFutureTick`）→ 按重连计数复位镜像（`SnapshotView.ResetForNewSession`）；**大厅镜头跟着上一局残留的"自己尸体"**（画面是一堵墙、有一次整幅上下翻转）→ 非对局相位一律用转播机位；**面板 `snapshotRateHz` 虚高** → 改成"最近 1 秒窗口"；**对局外不举枪**。

验证：客户端 `SELFTEST OK cases=201`；服务端 `ac_tests 514/514` + ctest 通过并已重装重启（备份 + sha256 一致）；装包实跑对局画面（羊/枪/HUD/遥测）逐张截图确认。

**仍未做**：仓库内 1Hz 遥测行与 `client/tools/playtest.ps1`（把"能玩"固化成机器判据）——本轮仍靠临时诊断工具；末尾几次大厅截图的采集工具返回了同一帧（窗口未重绘），所以"转播机位"的最终观感未经截图确认，仅自检覆盖。
### A19. ✅ 真机试玩查出的三个阻断级缺陷（"完全玩不了"的根因）

装包对着部署服务器（`43.143.120.65:8788`）实跑复现，三个缺陷串成一条链：

| # | 缺陷 | 证据 | 修法 |
|---|---|---|---|
| 1 | **下行快照一条都没进镜像**：`GameLoop.OnPacket` 每包把复用帧重置成 `default(SnapshotFrame)`（数组 null），`TryToFrame` 要求非空 ⇒ 全被判失败丢弃 | 对局面板 `inboundBytesPerSec 19240`（快照在收）却 `serverTick=0`、`entityCount=0`、`snapshotRateHz=n/a`；世界里没有实体 ⇒ 相机无本地实体可跟（画面是几何体内壁，"视角整个倒转"）、HUD `HP 0` | 复用帧数组在构造期分配一次，`OnPacket` 只复用不重建（用例 `boot.snapshot_reaches_mirror`，变异打红） |
| 2 | **HUD 口径错位 + 无分辨率缩放**：`OverlayItem.X` 对 Center 是中心线、对 Right 是右边缘，渲染侧却按左边缘起矩形 | 变异输出逐字复现用户症状：`align=Center text=ANGRY CHEN rect=640,28,1920,82`（1280 宽的视口里被推到屏外）；波次横幅同样越界、右对齐弹药行整条出屏；字号固定像素、1440p 下 16~32px "看不清" | `OverlayRenderer.ItemRect` 按对齐口径换算；`Hud.ScaleFor(视口高)` 统一缩放字号/条宽/行高并重建样式（用例 `render.overlay_in_bounds`：三档分辨率 × 四相位，每条绘制项必须在视口内） |
| 3 | **断线后永不重连 + 失焦即停帧**：`SessionStateMachine.Tick` 没有 `Disconnected` 分支；`runInBackground: 0` | 实跑抓到僵尸客户端：服务端 `players 0`、10 秒 `ac_frames_in_total` 零增长，客户端面板 `0 B/s`、缓存着旧花名册、按键全无反应、界面无任何提示 | `Disconnected` 按 1s→2s→4s→8s 退避重连（新 nonce；成功后 `FlushJoin` 自动重发昵称并重新认领身份）、`runInBackground: 1`、断线时大厅/HUD 显示"与服务器断线，正在重连…"（用例 `net.auto_reconnect`） |

**大厅 ready 的死因是 #1 的下游**：命令的 `clientTick` 取自最近一条**已应用**快照 ⇒ 恒 0，而服务端
`validateClientTick` 是严格相等且在生产实例上已累积到 4.8 万（跑过 75 局）⇒ 每一条大厅命令被整条丢掉
（Ready 位一起丢）。A/B 实证：生产实例按回车无反应；新起实例（tick=0）按回车立刻进对局。用例
`boot.command_tick_from_snapshot` 钉住"大厅命令带活 tick + Ready 位"。

顺带接上两处实跑可见的空缺：**弹药/怒气 HUD**（取自本地玩家那行 MatchState 的 mag/reserve/rage，
此前 `sample` 没人填、HUD 恒 `0 / 0`；`WeaponTable` 补与服务端 `kWeapons[].mag` 同值的弹匣容量表）与
**进对局自动锁定指针**（以前必须先点一下画面，玩家第一反应是"视角无法调整"；ESC 解锁后不会被自动锁回）。

**修后装包复跑**（同一台机器、同一台服务器）：对局面板 `serverTick 46051 / entityCount 62 / ping 16ms /
frameTimeP95 0.1ms`，`波次 1` 居中、`HP 90`、`怒气 0`、`12 / 120` 弹药全部在屏内且可读；
WASD/鼠标/开火都有可观测响应。`SELFTEST OK cases=201`。

**仍未做**（如实登记）：手里没有武器视图模型（C09 至今没有生成器，"空手射击"）；调试面板的
`snapshotRateHz` 用生命周期计数除短窗口，数值偏大（表现为 100~700/s，实际 20Hz），是面板口径 bug；
仓库内的遥测行与 `client/tools/playtest.ps1`（把"能玩"固化成机器判据）尚未落地 —— 本轮用的是临时诊断工具。
### A18. ✅ 真机连上公网部署（`43.143.120.65`）—— 产品侧入口 + 身份绑定 + 诊断纠错

服务端部署在 `43.143.120.65`（详见 `docs/evidence/server-v2-acceptance.md` §14），部署方放行安全组
`8000-9000` TCP+UDP 后，本轮用**真出包客户端**（`ac-client-0.1.0+778c15d-win64.zip`、`backend=Mono`）
从公网直连，`server.txt` 只写一行 `43.143.120.65:8788`：

```
Ac.Boot: 连接 43.143.120.65:8788（来源 server.txt）
Ac.Boot: 本地身份已绑定 pid=3（大厅昵称="牧羊人"）
```

含义：`Hello → HelloAck → kJoin(昵称) → MatchState 行表 → 按名认领 pid → 相机/HUD 绑定` 整条链路
在**公网真机**上打通（服务端同刻 `/health` 报 `connections:1 players:1 graceActive:1`）。整份 `Player.log`
**0 条异常**。

**顺带修掉的诊断缺陷**：「本地身份未绑定」那条警告原来在**第一帧**就发（`LocalPlayerId` 起跑线必然为 0），
而身份要到 1 Hz 的 MatchState 回来才可能绑上 ⇒ 每次正常启动都喊一次狼来了，实跑排障只能靠猜。
本批改为：① 宽限期 `NoIdentityWarnSeconds = 5.0` 秒（纯函数 `ShouldWarnNoIdentity` 可复算，用例
`presentation.identity_warn_grace`）；② 认领成功时补一条配对日志 `本地身份已绑定 pid=…`。
基线用例数 195 → **196**（`SELFTEST OK cases=196`）。

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
| C15 | 发布链路实测出包；版本行/日志已交付；**六步联合验收已通过**（A2：`SELFTEST OK cases=187` + `JOINT-ACCEPTANCE PASS`，退出码 0） | 已验收 | 未跑双轴审查（唯一未做项） | `MC15` |
| 补齐 | B1 呈现层装配、B2 guid 统一、B3 URP 引用、B4 预算单源、B5 JSON 去重、B7 准星调色板、B8 击杀受害者、B9 基准口径、B10 门禁词表、B11 自测入口、B12 打包 guard | — | B1 已跑两轴并修复；其余为审查/审计发现 | — |

提交链：`ac9b9ec` → `099d1f6` → `a2bbe2b` → `42e0260` → `88663c9` → `2bb9d18` → `71c123b` → `982092a` → `22b452a` → `1b36859`（B1）→ `4611f2e`（B5）→ 本轮 B1 修复轮。
