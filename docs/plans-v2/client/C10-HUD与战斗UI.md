# C10 HUD 与战斗 UI
> 里程碑：MC10 ｜ 预估：1.5 人日 ｜ 上游移交物：HANDOFF-C09

## 1. 目标

本步骤结束后，对局内 HUD 全部由系统字体自绘：血量/护甲、弹药与备弹、怒气、准星、倒地遮罩与救援提示、波次横幅与波间倒计时、击杀记录。数值类刷新 ≤ 10Hz、状态类由事件驱动，任何一帧都不因 UI 产生托管分配；不引入 TMPro 等素材包。

## 2. 入口条件
**入口条件**：HANDOFF-C09

**跨链前置**：`S08`（弹药/怒气/命中事件的字段与语义）与 `S10`（`MatchPhase` 阶段切换、波次与波间计时），二者在 [README](../README.md) 的 order 表中都排在本步之前。

| # | 必须已成立的事实 | 验证命令 | 期望 |
|---|---|---|---|
| 1 | 权威对局状态字段可指认（冻结对照，备份仓库） | `Select-String -Path D:\projects\tmp\angry-chen-bak\packages\shared\src\match\state.ts -Pattern 'reviveRatio255'` | 命中 `reviveRatio255: number` |
| 2 | 事件表可指认（备份仓库） | `Select-String -Path D:\projects\tmp\angry-chen-bak\packages\shared\src\net\protocol.ts -Pattern 'reviveProgress'` | 命中 `reviveProgress: 6` |
| 3 | 阶段枚举可指认（备份仓库） | `Select-String -Path D:\projects\tmp\angry-chen-bak\packages\shared\src\match\phase.ts -Pattern 'MATCH_PHASE'` | 命中 `MATCH_PHASE =` |
| 4 | 前一步交付在位 | `Test-Path client/Assets/Scripts/View/HitMarker.cs` | `True` |
| 5 | 系统字体可用 | 编辑器内 `Font.CreateDynamicFontFromOSFont("Microsoft YaHei", 32)` 返回非空 | 非空（人工确认一次） |

## 3. 交付物

| 文件 | 状态 | 职责 |
|---|---|---|
| `client/Assets/Scripts/UI/UiThrottle.cs` | 新建 | 数值类 10Hz 节流与显示精度脏检查 |
| `client/Assets/Scripts/UI/Hud.cs` | 新建 | 根容器、安全区、系统字体与字号表、采样对象复用 |
| `client/Assets/Scripts/UI/Crosshair.cs` | 新建 | 十字准星（尺寸随散布、四色表） |
| `client/Assets/Scripts/UI/AmmoCounter.cs` | 新建 | 弹药 / 备弹 / 换弹环 |
| `client/Assets/Scripts/UI/RageBar.cs` | 新建 | 怒气条与狂暴倒计时 |
| `client/Assets/Scripts/UI/DownedOverlay.cs` | 新建 | 倒地遮罩与可救援提示底 |
| `client/Assets/Scripts/UI/RevivePrompt.cs` | 新建 | 救援提示与 3 秒进度 |
| `client/Assets/Scripts/UI/WaveBanner.cs` | 新建 | 波次横幅、波间倒计时、波次刻度 |
| `client/Assets/Scripts/UI/KillFeed.cs` | 新建 | 击杀记录（含爆头样式）与队伍状态行 |
| `client/Assets/Tests/HudSuite.cs` | 新建 | 元素映射、节流、准星、安全区、可见性断言（C03 起测试文件名统一 `*Suite.cs`） |

## 4. 任务清单

- [ ] 1. 写 `client/Assets/Scripts/UI/UiThrottle.cs`：`NumericRefreshMs = 100`（≤10Hz）、`StatsRefreshMs = 250`、`ShouldWrite(int rounded)` 脏检查；状态类事件走回调，不经节流。
- [ ] 2. 写 `client/Assets/Scripts/UI/Hud.cs`：`SafeAreaPercent = 0.04`，系统字体 `Font.CreateDynamicFontFromOSFont`（回退 `Segoe UI` → `Arial`），字号 32/18/16/48 四档，装配全部子元素并复用 `HudSample`。
- [ ] 3. 写 `client/Assets/Scripts/UI/Crosshair.cs`：四段十字，尺寸按 `spreadDeg` 映射（`0.5°→2px`、`5°→24px`），颜色按 §5(c) 表切换。
- [ ] 4. 写 `client/Assets/Scripts/UI/AmmoCounter.cs`：显示值取 `AmmoLedger.mag`（C06），备弹取 `MatchStatePlayer.reserve`，`AmmoLowRatio = 0.3` 以下变红，换弹环取 `reloadLeft10Ms / 10`。
- [ ] 5. 写 `client/Assets/Scripts/UI/RageBar.cs`：取 `MatchStatePlayer.rage`（满值 `RageFull = 100`）与 `rageLeft100Ms`，满值时显示可激活提示，狂暴期间显示剩余时间。
- [ ] 6. 写 `client/Assets/Scripts/UI/DownedOverlay.cs` 与 `client/Assets/Scripts/UI/RevivePrompt.cs`：可见性条件见 §5(e)；救援进度 = `reviveRatio255 / 255`，总时长 3000ms，进度事件步长 5%。
- [ ] 7. 写 `client/Assets/Scripts/UI/WaveBanner.cs` 与 `client/Assets/Scripts/UI/KillFeed.cs`：横幅 4500ms / 队列 3、波次刻度 10 段（Boss 波间隔 5）、波间倒计时读 `intermissionMs`；击杀记录 3000ms / 上限 6 / 渐隐 600ms，爆头条目单独样式。
- [ ] 8. 写 `client/Assets/Tests/HudSuite.cs`：元素到字段的映射逐条断言、100ms 窗口内多次 `Tick` 只写一次、准星尺寸与颜色、安全区、倒地与救援可见性、击杀记录上限与过期；用例注册进 `Ac.Tests.SuiteRegistry.RunAll`（`hud.*`）。

## 5. 冻结契约

冻结的 v1 对照实现：`packages/client/src/ui/hud.ts` 与 `packages/client/src/combat/localTimers.ts`（只作对照，不修改）。

**(a) HUD 元素与数据来源**

| 元素 | 来源 | 字段 |
|---|---|---|
| 血量 / 护甲 | 快照 + S10 `MatchState` 单播 | `SnapshotEntity.hpRatio`、`MatchStatePlayer.hpRatio`；**护甲顺延**：`MatchStatePayload` / `SnapshotCodec` / `codec.cpp` 目前都没有 armor 字段，本层不猜（2026-04 审查实查后修订） |
| 弹药 / 备弹 | S10 `MatchState` 单播 + 本地账 | `MatchStatePlayer.mag` / `reserve`、`AmmoLedger.mag` / `gateMag` |
| 换弹环 | S10 `MatchState` 单播 | `MatchStatePlayer.reloadLeft10Ms`（1/10 ms） |
| 怒气 / 狂暴剩余 | S10 `MatchState` 单播 | `MatchStatePlayer.rage` / `rageLeft100Ms` |
| 倒地 / 狂暴位域 | 快照 | `SNAPSHOT_FLAG.downed = 1` / `rageMode = 2` / `reloading = 4` / `charging = 8`（掩码以 `Ac.Net.SnapshotCodec` 为准） |

> 阶段码以 S10 `MatchPhase` 为权威：`lobby = 0` / `loading = 1` / `playing = 2` / `intermission = 3` / `ended = 4`。**不要**按「playing = 1」猜——那样 playing 时 HUD 会全不可见、loading 时反而可见（2026-04 审查实查后修订）。
| 受伤与命中 | 事件 | `EVENT_TYPE.playerHit = 1` + `HIT_FLAG.headshot = 1` / `killed = 4` |
| 击杀记录 | 事件 | `EVENT_TYPE.sheepKilled = 2` |
| 波次横幅 | 事件 | `EVENT_TYPE.waveStart = 3` / `waveClear = 4` |
| 波次与波间 | S10 `MatchState` 单播 | `MatchState.phase` / `wave` / `intermissionMs` |
| 救援提示 | 事件 + S10 `MatchState` 单播 | `EVENT_TYPE.reviveProgress = 6` / `reviveDone = 7`、`MatchStatePlayer.reviveRatio255` |

`MatchStatePlayer.*` / `MatchState.*` 的字段（`mag` / `reserve` / `rage` / `rageLeft100Ms` / `reloadLeft10Ms` / `reviveRatio255` / `phase` / `wave` / `intermissionMs`；`aliveMs` 顺延——单播里还没有这个字段）与 S10 §5 的字段逐名一致，**全部由 S10 的 `MatchState` 单播提供**（本步只读，不走快照通道）。

**(b) 刷新节流（继承 `O06`）**

| 项 | 值 / 定义 |
|---|---|
| 数值类 | ≤ 10Hz：`NumericRefreshMs = 100` |
| 状态类 | 事件驱动，立即刷新：命中 / 击杀 / 倒地 / 救援进度 / 波次 / 阶段切换 |
| 统计行 | `StatsRefreshMs = 250`（= v1 `STATS_REFRESH_MS`） |
| 脏检查 | 先按显示精度取整再比较；同值不写文本、不写样式 |
| 分配 | 每帧零托管分配：`HudSample` 复用、事件不建数组、不拼临时字符串 |
| UI 预算 | 1080p 单帧 UI ≤ `1.5ms`（P95）；文本重建 ≤ `6` 次/秒 |

**(c) 准星**

| 项 | 值 / 定义 |
|---|---|
| 形状 | 四段十字（上 / 下 / 左 / 右），中心留空 |
| 尺寸映射 | `spreadDeg` 线性映射：`0.5° → 2px`、`5° → 24px`，超出即夹取 |
| 常规色 | `0xE8E8F0` |
| 可命中目标 | `0xFFD24D` |
| 受伤 | `0xD2402F`（与红闪同步 150ms） |
| 倒地 / 阵亡 | 隐藏 |

**(d) 字号与安全区**

| 项 | 值 / 定义 |
|---|---|
| 字号 | 主数值 `32px`、标签 `18px`、击杀记录 `16px`、波次横幅 `48px` |
| 字体 | 「唯一来源」`Font.CreateDynamicFontFromOSFont(string[], size)`（系统字体，数组重载本身就是回退链）入库名 `Microsoft YaHei → Segoe UI → Arial`；**禁用 `TextMeshPro` / `TMPro` 与任何字体素材文件**（2026-04 修订：调用形式按实现） |
| 安全区 | `SafeAreaPercent = 0.04`（1080p 下内缩 ≥ `43px`） |
| 层级 | HUD 在视图模型之上；所有 UI 元素不接收射线，不拦截输入（渲染层装配在场景装配步骤落地；本层把参数、可见性与映射冻住并断言） |

**(e) 可见性与时长**

| 项 | 条件 / 值 |
|---|---|
| 倒地遮罩 | `MatchStatePlayer.downed == true` 且 `phase == playing` |
| 救援提示 | 自己未倒地 且 2.0m 内有倒地队友（`REVIVE.rangeM = 2.0`），进度 = `reviveRatio255 / 255` |
| 隐藏条件 | `phase == ended` 时隐藏全部战斗 UI；自己倒地时隐藏准星与弹药 |
| 救援总时长 | `3000ms`（= `REVIVE.durationMs`），进度事件步长 `5%` |
| 波次横幅 | `4500ms`，队列上限 `3`；波次刻度 `10` 段，Boss 波间隔 `5` |
| 击杀记录 | `3000ms`，上限 `6`，渐隐 `600ms` |
| 伤害数字 / 血量尾影 | `900ms` / `800ms` |
| 冲锋警戒 | `1600ms`（由 `SNAPSHOT_FLAG.charging` 驱动） |

## 6. 验证

| # | 命令 | 期望 | 失败意味着 |
|---|---|---|---|
| 1 | `& $env:AC_UNITY -batchmode -quit -nographics -projectPath client -executeMethod Ac.Tests.SuiteRegistry.RunAll -logFile -` | 末行 `SELFTEST OK` 且全输出无 `FAIL` | 映射、节流或可见性断言不成立 |
| 2 | 上条批处理命令的输出 | 含 `PASS hud.` 前缀的用例 ≥ 6 条 | 覆盖不足（节流或可见性漏测） |
| 3 | `Select-String -Path client/Assets/Scripts/UI/UiThrottle.cs -Pattern 'NumericRefreshMs = 100'` | 命中 | 节流常量被改，数值刷新可能超过 10Hz |
| 4 | `Get-ChildItem client/Assets/Scripts/UI -Recurse -Include *.cs \| Select-String -Pattern 'TextMeshPro'` | **无输出** | 引用了 TMPro 素材包，违反零素材约束 |
| 5 | `node tools/check-docs.mjs` 与 `node tools/check-assets.mjs` | 退出码 0 | 文档或素材门禁被拒 |
| 6 | 人工：4 人局 + 40 羊，观察 60 秒 | 帧时间 P95 ≤ 20ms、每帧托管分配 `0B` | 节流或对象复用未生效 |

## 7. DoD（验收标准）

- [ ] §5(a) 每个 HUD 元素在 `HudSuite.cs` 里都有"字段 → 显示"用例
- [ ] 数值类刷新 ≤ 10Hz（100ms 窗口内多次 `Tick` 只写一次），状态类事件驱动立即刷新
- [ ] 准星形状、尺寸映射（`0.5°→2px`、`5°→24px`）与四色表与 §5(c) 一致
- [ ] 字号四档（32/18/16/48）与安全区 4% 生效；倒地与救援提示可见性条件被断言
- [ ] UI 无 TMPro / 素材依赖，文本全部系统字体；UI 元素不拦截输入
- [ ] 分组自检 `Ac.Tests.SuiteRegistry.RunAll` 无 `FAIL`、末行 `SELFTEST OK`；`node tools/check-docs.mjs`、`node tools/check-assets.mjs` 退出码 0

## 8. 风险与回滚

| 风险 | 触发信号 | 对策 |
|---|---|---|
| 节流过粗导致数字滞后 | 血量/弹药显示落后 > 150ms | 数值类 100ms；命中/击杀/倒地等状态类事件立即刷新 |
| 系统字体缺失 | 文本渲染成空框 | 回退链 `Microsoft YaHei → Segoe UI → Arial`，启动时打一条日志 |
| 波间倒计时与服务器不同步 | 倒计时跳变或提前归零 | 只读 `MatchState.intermissionMs`，本地不做插值外推 |
| UI 抢输入 | 鼠标点击被 HUD 吃掉 | 所有 HUD 元素禁用射线目标，输入只由指针锁定层处理 |
| 文本重建触发 GC | 每帧托管分配 > 0B | `HudSample` 复用 + 脏检查；事件文本入环形池而非新建对象 |

**回滚目标**：删除 9 个 `UI` 文件与 `HudSuite.cs`，回到 `HANDOFF-C09` 状态（无 HUD，渲染与模拟不受影响）。

## 9. 移交物
**移交物 ID**：HANDOFF-C10

稳定接口清单：

- `Hud`：`Apply(in HudSample)` / `PushEvent(in HudEvent)` / `Tick(float dtMs)` / `CreateFont(int)` / `FontPx(role)` / `SafeAreaInsetPx(int)` / `CombatUiVisible(phase)` / `CrosshairVisible(in HudSample)` / `AmmoVisible(in HudSample)`
  - 事件视图用本层的 `HudEvent`（`Type` / `HitFlags` / `Wave` / `ReviveRatio255` / `Enemy`）+ `HudEventType` 常量；S10 的 `SimEventView` 交付后再换成它（2026-04 修订，值先按 S08/S10 事件表：playerHit=1 / sheepKilled=2 / waveStart=3 / waveClear=4 / reviveProgress=6 / reviveDone=7）
  - 数值类是一个组：一个 100ms 窗口写一次，脏检查的键是整组数值的组合值；状态类走 `PushEvent` 立即刷新
- `UiThrottle`：`ShouldWrite(int)` / `ShouldWriteStats(int)` / `NoteEventWrite()` / `NumericWrites` / `StatsWrites` / `SkippedWrites`
- `Crosshair`：`SetSpread(float)` / `SetState(CrosshairState)` / `MarkHurt()` / `SizePx` / `CurrentColor`
- `AmmoCounter`：`Set(int, int, int)` / `IsLow` / `Color` / `ReloadRingMs` / `MagazineSize`
- `RageBar`：`Set(int, int, bool)` / `CanActivate` / `RageFull` / `RageLeftMs` / `Fill01`
- `RevivePrompt`：`SetVisible(bool)` / `SetProgress(float)` / `SetFromRatio255(int)` / `RemainingMs` / `CanPrompt(bool, float)` / `RangeM` / `ProgressStep`
- `DownedOverlay`：`Update(bool downed, byte phase)` / `SetVisible(bool)`
- `WaveBanner`：`ShowWave(int)` / `SetIntermission(int)` / `IsBossWave(int)` / `TickIndex(int)` / `QueueCapacity` / `OverflowCount`
- `KillFeed`：`Push(in KillEntry)` / `Tick(float)` / `Alpha(float)` / `ColorOf(bool)` / `Entries` / `Remaining`
- `RageBar`：`Set(int rage, int rageLeft100Ms, bool rageMode)`

已验证能力清单：见 §7（元素映射、10Hz 节流、准星、字号与安全区、可见性、零分配）。
