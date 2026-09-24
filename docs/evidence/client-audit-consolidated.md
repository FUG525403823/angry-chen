# 客户端全量审计合并记录（C07–C14 之后）

> **收尾状态（第 13 轮）**：用例 113 → **126** 全绿；H2 已用服务端原文驳回；H1/H3/H7/A8/M1/M2/M3/M5/M6/M7/M8/M9 已修并各自带判别性用例（M7 的判别性是"把修复中和掉重跑得到 `FAIL view.spawn_pose actual=0`"实测出来的）。
> **C14 仍未通过双轴审查**（`Ac.Tests.FrameBench.Run` 入口在仓库里不存在），因此**未提交 MC14、未打标签**。根因是 H6：`client/Assets` 全树 0 个 `MonoBehaviour`、0 个 `.unity/.prefab`、`EditorBuildSettings.asset:7 m_Scenes: []`、无 `RuntimeInitializeOnLoadMethod` —— **C01–C15 没有任何一份计划交付"运行期装配根"**，而 C14 §6 的帧基准与 C15 的联合验收都必须跑在真实运行的游戏循环 + 真图形设备上。
> 需人工裁决：①装配根（补一份计划 vs 并入 C15）；②`.meta` 的 56 字符 base64 GUID 与 URP 悬空引用（手改 122 个 vs 让编辑器重生成并接受大 diff）；③`Batching.CullDistanceMeters(int kind)` 的 kind 取值（计划只冻结了签名）；④H4 准星散布值域（与 `server/src/config/weapons.hpp:25-27` 的 0.8/0.6/4.0 冲突）；⑤C08×C14 剔除/阴影阈值冲突（90m vs 60/80m、60m vs 20/35/50m）。

> 触发：用户要求"完整检查一遍整个 client，看看有没有遗漏的缺陷并完成相关测试"。
> 方法：4 个只读审计子代理（Core+Tests / Net+Sim / UI+View / 工程横切）+ C14 双轴审查，全部只读、未启编辑器；
> 详细原始报告在 `client/Logs/audit-*-findings.md` 与 `client/Logs/review-c14-*.md`（gitignore，不入库）。
> 本文件是**可入库的合并结论**，供逐条修复与人类裁决。

## 1. 本轮已修（工作树，未提交）

| 缺陷 | 处置 | 用例 |
|---|---|---|
| `View/SheepInstancePool.cs:37` 只清 `i < Count` 连续前缀，而实例下标是 `form*256+cursor` 跨步 ⇒ `Culling` 扫全 1024 时旧记录仍 `Visible` ⇒ 幽灵实例 | 改为整池失活（1024 次 bool 写入） | `sheep.pool_reset_multiform`：10 grunt+100 elite → Reset → 全池无 Visible；第二帧只画 10 grunt 时 256..1023 仍全 false（旧代码必红） |
| `Core/SettingsStore.cs` Load 的三条提前返回（文件不存在 / IO 异常 / 坏 JSON）不复位 `ReadOnlyFile` ⇒ 装过 v3 之后所有落盘被静默丢弃 | 三条路径都复位 `ReadOnlyFile = false` | `settings.readonly_resets_across_loads`：v3 → Load 空目录 → 必须能落盘；v3 → Load 坏 JSON → 不再只读 |
| `Core/CombatFlags`（跨层共享的命中位）此前**没有任何断言**，`server/src/config/combat.hpp:18-20` 改值全仓不会红 | 不动代码，补断言 | `audio.combat_flags`：1/2/4、位不重叠、命中位与击杀位可同时置(0b101)、`BusCount.MixBusCount == 4`、总线下标落在计数内 |
| 我一度把 C08 的 90m/60m 直接改成 C14 的 60m/档位表（**规格自签**），并因此打红既有 `sheep.culling` | 全部撤回，改为在 `docs/evidence/client-v2-frame.md` §4 登记冲突待裁决 | — |
| `Core/InputSampler.cs:274-291` 注入键表漏 `KeyCode.Q`（表只到 `F`=10，`_keys` 只有 11 槽）⇒ `SetKey(Q)` 静默丢弃 ⇒ 换武器在注入/回放路径**永不可达** | 补 `case KeyCode.Q: return 11;`，`_keys` 扩到 12 槽 | 待补 `input.*` 用例（需先读输入套件现有注入写法） |
| 门禁"失败必须变红"从未被验证 | 新增 `c01.gate.fail_probe`：默认通过，设 `AC_SELFTEST_SELFPROBE=1` 时故意失败 | **已实测**：`AC_SELFTEST_SELFPROBE=1` → `SELFTEST FAIL failures=1 cases=117` + `exit=1`；不设 → `SELFTEST OK cases=117` + `exit=0` |

当前工作树：`SELFTEST OK cases=116`，`check-assets` / `check-docs` 全绿。

## 2. 待修缺陷（按严重度，均带 文件:行）

### 2.1 高

| # | 位置 | 问题 | 来源 |
|---|---|---|---|
| H1 | `Core/InputSampler.cs:274-291` | 注入键表漏 `KeyCode.Q` ⇒ `SetKey(Q)` 静默丢弃 ⇒ 换武器 `switchTo`/`ButtonSwitchWeapon` 在用例与回放路径**永不可达**（全仓无 Q 用例） | audit-core |
| H2 | `Sim/SnapshotView.cs:90-91` | audit 认为"丢 1 帧后每帧都判 baselineMismatch 直到下一个全量帧"是缺陷 | **本轮复核：驳回（审计误读冻结语义）**。`server/src/replication/baseline.hpp:2-5` 原文："快照通道不可靠，基线在**编码完成时**随镜像一起前进，**不等客户端 ack**；客户端丢帧靠 `baselineTick = 0`（强制全量）语义 + 每 40 tick 的强制全量节拍自愈"（`kFullSnapshotIntervalTicks = 40`）。客户端 `:91` 的 `baselineTick != 0 && baselineTick > AppliedTick → 丢弃` 正是这条冻结语义的实现，丢帧后 ≤2s 自愈是设计而非缺陷。按"实现方不得自签、也不得改上游迎合实现"的既定口径，不动代码。另：`baselineTick < AppliedTick`（乱序到达的旧基线差分）被接受是**安全**的——差分记录是绝对值快照、`removedIds` 是相对旧基线的超集，套用在更新的镜像上是幂等的 | 本轮复核 |
| H3 | `UI/Hud.cs:124-148` | `Hud.Apply` 从不调 `_banner.SetIntermission` ⇒ 波间倒计时恒 0 | **本轮已修**（脏检查写 `_intermissionWritten` 的同一次写入里补 `_banner.SetIntermission`）+ 用例 `hud.intermission_wiring`（旧代码断言 `Banner.IntermissionMs == 12000` 必红） |
| H4 | `UI/Hud.cs:158` | `_crosshair.SetSpread(_crosshair.SpreadDeg)` 自赋值 + `HudSample` 无散布字段 ⇒ `SizePx` 恒 2px，C10 §5(c) 的 0.5°→2px / 5°→24px 映射**不可达** | audit-ui-view |
| H5 | `UI/Crosshair.cs:57-65` | C13 §5 的 `crosshairColor` / `colorblindSafe` 只写不读（`SettingsPanel.cs:52-60` 是唯一非测试引用）⇒ 设置生效点缺失 | audit-ui-view |
| H6 | `client/Assets` 全树 + `ProjectSettings/EditorBuildSettings.asset:7` | `m_Scenes: []`、0 MonoBehaviour、0 `.unity/.prefab`、无 `RuntimeInitializeOnLoadMethod` ⇒ **客户端无运行期装配根**；C10/C12/C13 验收把接线推给不存在的"场景装配步骤" | **已解决（本轮）**：新增组合根程序集 `Ac.Boot`（`GameLoop` 不依赖 UnityEngine，帧基准与游戏共用同一条回路；`GameBootstrap` 用 `RuntimeInitializeOnLoadMethod` 自举，不依赖场景内容），并补上 `EventEntry → HudEvent` 这条此前只存在于测试里的接线。C01 §5.2 的引用白名单相应扩充 `Ac.Boot`（理由见 `docs/evidence/client-v2-frame-acceptance.md`）。实测：帧回路端到端用例 + 稳态零分配用例常驻通过，`Ac.Tests.FrameBench.Run` 产出 §9 的 JSON |
| H7 | `client/Assets/Scripts/Core/SelfTest.cs:26-31,79` + `Tests/SuiteRegistry.cs:34` + `Logs/selftest*.cmd` | 契约要求"失败退出码 1"（C02 §9:182），但 Core 入口无 `EditorApplication.Exit`、cmd 不校验 errorlevel ⇒ 注册期异常可能让门禁恒绿（尚无失败运行的证据） | audit-core |

### 2.2 中

| # | 位置 | 问题 | 来源 |
|---|---|---|---|
| M1 | `Sim/SnapshotView.cs:135,137-158` | `TryGetEntity/TryGetLocalAuthority` 只查"最新帧的差分记录"，不查全量镜像 ⇒ 站着不动时本机记录缺席 ⇒ `Reconciler.cs:61` 早退（ack 裁剪/重放/误差平滑全停摆，随后一次性重放 ~40 条） | audit-net-sim，**本轮已读码确认**：`:135` → `:137-151` 只扫 `_ringEntities[RingSlot(0)]`，而同一文件 `:162-170` 的 `ForEachVisible` 读的是镜像 `_entities[id]` ⇒ 两处对"当前状态"的定义不一致。**本轮已修**：`TryGetEntity` 改读镜像（`_present[id]` + `_entities[id]`），逐帧历史语义归 `TryGetEntityInFrame(age, ...)` 不变；用例 `view.mirror_queries` 造"全量帧含本机 → 下一帧差分不含本机"，断言本机仍可取且 `XCm==1250`、`TryGetLocalAuthority` 为真、`TryGetEntityInFrame(0,1)` 为假（旧实现第一条必红），并断言遍历与查询同源 |
| M2 | `Net/EventCodec.cs:96,111` | tracker 为空时自建 tracker 做帧内去重，服务端 `codec.cpp:442/:548` 是**完全不去重** ⇒ 同字节两重载结果不同；每帧新建还丢掉跨帧幂等键 | **本轮已修**（`tracker == null` 就不去重，与服务端逐字对齐；跨帧幂等由调用方持有的长期 tracker 承担）。修完立刻打红了既有 `codec.roundtrip`（`expected=1 actual=2`）——**这正是审计说的"两重载结果不同"**，已把该断言改成服务端语义并补上"带 tracker 才去重、且跨帧有效"的对照 |
| M3 | `UI/Hud.cs:169-170`、`UI/RevivePrompt.cs:25-28` | 每帧 `SetFromRatio255(sample)` 无条件覆盖事件写的救援进度；`CanPrompt` 把距离 0（=没有测量）当成"队友贴在脚下" ⇒ 提示常显；`SetVisible` 还把 `selfDowned` 硬编码成 `false` | **本轮已修**（权威先核实：`server/src/room/room.cpp:361-362` 的 `reviveRatio255` 取的是**该玩家自己** `entity->downed` 的 `reviveRatio`，未倒地恒 0 ⇒ 它不能当"被救队友"的提示进度，那是 `ReviveProgress` 事件通道的事）。三处改动：①采样只在 `sample.Downed` 时生效；②`CanPrompt` 要求 `distance > 0` 且 `<= RangeM`；③`SetVisible` 传真实的 `sample.Downed`。用例 `hud.revive_event_vs_sample`（事件写 128/255 → 采样不许抹掉、距离 0 不许常显、自己倒地时采样才生效且不提示）；并修正了既有 `hud.throttle` 里一条**测的是被覆盖后取值**的断言 |
| M4 | `UI/Hud.cs:207` | `entry.VictimId = hudEvent.Wave`（把波次当受害者 id）；`HudEvent` 无 victim 字段 ⇒ 击杀记录无法显示谁杀谁 | audit-ui-view |
| M5 | `UI/UiThrottle.cs:7` | 声明 `StatsRefreshMs=250` 但缺 C10 §9 冻结的 `ShouldWriteStats` / `StatsWrites` | **本轮已修**：新增独立 250ms 窗口 + `StatsWrites`，`Tick` 同时推进两个累加器；用例 `hud.stats_throttle` 断言"统计首窗放行、数值窗口不被统计吃掉（无 Tick 时数值仍能写）、249ms 不放行、250ms 放行、跳过计数" |
| M6 | `View/SheepVisuals.cs:89` | 每帧入口不调 `_pool.Reset()` ⇒ 同形态累计 256 次后 `TryAcquire` 恒返 −1（全仓 `Reset()` 无运行时调用者）。注意 `Write` 是**逐实体**入口（取其形参 `in EntityView`），复位只能发生在帧首 | **本轮已修**：新增 `SheepVisuals.BeginFrame()`（Reset 池 + 归零 `SkippedCount`/`CorpseCount`）并注明调用契约；用例 `sheep.visuals_begin_frame` 连跑三帧各 200 次取槽位断言游标/计数每帧归零（不 Reset 时第二帧即红），外加"不复位必然饱和"的反证。**另发现**：在测试里直接调 `Write` 会 NRE，说明它依赖只有运行期宿主才建立的表（H6 的又一实例），已记入 | audit-ui-view + 本轮实测 |
| M7 | `View/EntityViews.cs:140-191,232-237` | 姿态只在插值窗口的 `newer` **差分**记录里写、可见性却按**镜像**判定 ⇒ 新生远端实体以 (0,0,0) 在场地中心渲染 100–250ms | **本轮已修并证明判别性**：`MarkVisible` 里用置 `Visible` 之前的 `wasVisible` 判"本帧新出现"，这类实体（及原有"本地玩家无预测值"分支）改为从镜像吸附姿态。用例 `view.spawn_pose` 造"帧 4 首次出现实体 9，而渲染时刻 1050 落在 (1050,1100) 窗口里、没有 9"——**把修复中和掉重跑，实测 `FAIL view.spawn_pose actual=0`**（正是 M7 的症状），恢复后 124 例绿 |
| M8 | `Net/UdpTransport.cs:452-489` | 积压超预算且无快照可丢时不拒收也不封顶（`Enqueue` 恒返回 true，与自身注释相反）；分片快照（type 9）永不被丢；丢快照的旋转把非快照数据报塞回队尾 ⇒ `[cmd1,snap,cmd2]` 变成 `[cmd2,cmd1]`（服务端按 seq 判 `kStaleTick`） | **本轮已修**：`Enqueue` 先算这批字节再 `MakeRoom(bytes)`，腾不出空间就**不入队**并返回 false；`DropOldestSnapshot` 改为常驻暂存队列**保序重建**；新增 `IsDroppableSnapshot`（type 5 或"type 9 且可靠位未置"）；`PacketWriter.FlagsOffset` 提为具名常量。用例 `net.backlog_cap`：硬失败 socket 上发 200 条 1KB **纯命令**，断言既有受理也有拒收（旧实现恒 true、拒收数为 0），并断言拒收发生时状态仍是 `Connected`（排除"拒收其实来自断线"的假阳性）。**测试期新增的两件基础设施**：①`MemoryLink.FailSends`（硬失败模式）——原先只有 `LossRate`，而它**丢弃时仍返回 true**，所以积压永远涨不起来，封顶路径根本无法被触达；②握手必须先跑通——`Send` 在非 `Connected` 时直接返回 false，不先握手测出来的"拒收"全是假的。
**附带发现（未修，登记）**：`Send` 在 `Enqueue` **之前**就把可靠消息挂进重传表（`UdpTransport.cs:237-242`），因此被积压封顶拒收的消息会留在重传表里"待发但永不出队"。是否要在拒收时回滚 `Track` 需要 S04 侧语义确认，不自行决定 |
| M9 | `Tests/FixtureSuite.cs:25-27`（`Sim/FixtureLoader.cs:104-111`） | 0 份向量时 `CompareAll` 返回 0，唯一正向断言**恒绿**。**已实测复现并定位根因**：加一条 `vectors.Count >= 4` 后立刻红（`actual=0`）——4 份 fixture 确实在 `docs/evidence/fixtures/`，`RelativeDirectory = "docs/evidence/fixtures"` 也对，**根因已定位**：`IsShaped` 要求**顶层** `expected`（`FixtureLoader.cs:37-38,80`），而 C06 产出的 4 份 fixture 顶层只有 `name/seed/configHash/ticks`，`expected` 在**每个 tick 内部**（实测 `still-60t.json`）⇒ `IsShaped` 对 4 份全部返回 false ⇒ `Load` 返回空。顺带 `FixtureLoader.cs:116` 读的是 `vector.Root.Get("expected")`，即使加载成功也只会拿到 null ⇒ **C02 §5.7 的"共享 fixture 对拍"门禁从落地起没比对过任何东西**。**本轮已修**：`IsShaped` 认"每帧 expected"，`CompareAll` 改为按帧比对（`Func<FixtureVector,int,JsonValue>`），`FixtureVector.ExpectedOfTick` 新增；`fixtures.loader` 现在断言"≥4 份向量 / 合计 ≥1000 帧 / 0 处不一致"，并用"每份向量第 1 帧不给实际输出"作判别性反例（必须恰好报向量数处不一致，旧实现恒 0）。实测日志：`[fixture] 4 份 / 1100 帧`、`PASS fixtures.loader`。修法：`IsShaped` 认顶层 `ticks`，比对按 `ticks[i].expected` 逐帧展开；计划正文写的顶层 `expected` 属计划与共享产物的格式分歧，按"不擅自改计划"登记待裁决 | audit-core + 本轮实测 |
| M10 | `client/tools/frame-bench.ps1` + `Ac.Tests.FrameBench.Run` 缺失 | §9 参数契约缺 `-Scene/-Frames/-Out`（PS5.1 静默吞未知参数）、环境不可用应 exit 2、只验字段存在不验场景身份、`stageP95` 汇总取第 1 次运行而判定用中位、偶数 Runs 的"中位"取上中位 | review-c14 两轴 |

### 2.3 低 / 结构性

- 拒绝码两侧不一致：`Net/CommandCodec.cs:50-51` 13B 载荷客户端 `Truncated` vs 服务端 `kBadLength`；`Net/SnapshotCodec.cs:76` `recordCount>128` 客户端 `BadValue`、服务端无此检查（`server/tests/codec_test.cpp:611-613`）。
- `Net/MatchStateCodec.cs:37` `FixedRecordBytes=13` 错误（应为 16，`net/codec.hpp:185-190`）且无人引用。
- `Net/Fragment.cs:86,157-164` 重组键 `(reliable,fragId)` ≠ 权威 `(session,channelType,fragId)`，今天靠可靠位恰好区分。
- `Sim/AmmoLedger.cs:33-40` `NoteLocalShot` 的 `slot` 死参数；切枪当帧重置未实现。
- `View/Batching.cs:80,125-145` `Pooled` 实为总空闲槽位；`Clear()` 后旧句柄可再归还（实测 alias）；`Clear` vs `Reset` 语义与 `SheepInstancePool.Reset` 相反且无注释。
- `Core/FrameProfiler.cs:14-15` vs `UI/DebugPanel.cs:34-35` 20/33ms 双源；`View/Batching.cs:14,20-23` vs `Core/SettingsStore.cs:43-44,80` 画质档范围双源；`frame-bench.ps1` 与 `Core/FrameProfiler.cs:14-27` 整张预算表两份。
- `Core/FrameProfiler.cs:45,122-143` 分位查询共用 `_scratch`（不可重入）未按 `View/Culling.cs:13` 惯例加注释。
- `Tests/PerfSuite.cs:56,65-66,136,232` 恒真/自证断言：`P95<=P99`（全 0 窗口也真）、`Pooled==Capacity-Live`、`sum>=0f`，且注释承诺的"第 228 小值"从未被真正测到（无注入缝隙）。
- `Tests/QuantizeSuite.cs:91,165,72-75` 循环内重复断言、同表自证、与 :22-26 重复；`Tests/SettingsSuite.cs:271-294` `AudioSeam.Bind` 无 finally（顺序依赖）。
- `122 个 .meta` 的 guid 是 **56 字符 base64**（16 字节 GUID 的 base64 应 24 字符）⇒ 非引擎格式；`UniversalRenderPipeline.asset:20` 用 32 位十六进制 guid 引用渲染器，而 `UniversalRenderer.asset.meta` 是 base64 ⇒ **悬空引用**，无门禁覆盖。
- `client/Assets/Shaders/Emblem.shader` 无调用方；`client/.gitignore:7` 让 `client/Logs/selftest.cmd` 不入库 ⇒ 证据命令字面不可复现。
- 计划正文与实现入口名漂移（C11/C12/C13/C04/C07 仍写 `*_test.cs` 与 `AudioTest/LobbyFlowTest/SettingsTest`），`check-docs` 只扫 markdown 链接抓不到。

## 3. 修复波次（下一轮起按此顺序，每波都带用例）

1. **门禁可信度**：H7（自检失败必须能让进程退出码非 0 + 一条"故意失败"的反向自检）、M9（fixture 加载数 ≥ 4 的正向断言）、M10（ps1 参数契约 + 场景身份校验 + exit 2 + 真中位）。
2. **真 bug**：H1（Q 键注入换武器）、H2/M1（快照基线与全量镜像新鲜度：用已有丢包链路 + 记录数 0 的合法差分帧）、H3/H4/H5（Hud 接线：波间、散布、准星配色）、M6/M7（池 Reset、新生实体姿态）、M3/M4/M5。
3. **结构性**：H6（运行期装配根 —— 需要单独一份计划或 C15 明确承担）、.meta GUID 契约测试、常量双源收敛（含 C08×C14 冲突裁决）、计划入口名统一。
