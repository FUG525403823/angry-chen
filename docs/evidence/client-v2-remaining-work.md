# 客户端链条（C01–C15）收官清单

> 取证时间：2026-09-24 ｜ commit `1cdc567`（含客户端 `71c123b`）｜ 标签：`MC01 … MC14`，**无 `MC15`**
> 本文每条都有实测出处；凡是"我没做/不能做"的都单列，不混在"已完成"里。

## 0. 一句话现状

**能跑的**：帧回路（输入 → 预测/和解 → 快照镜像 → 视图同步 → HUD）、音频 mixer、版本行与日志落盘、一条命令出发布包。
**不能跑的**：**画面上什么都没有，也没有任何界面流程** —— 因为没有任何场景、没有任何预制体，而且视图/界面类**从未被实例化**。

实测证据：

| 事实 | 命令/出处 | 结果 |
|---|---|---|
| 场景与预制体 | `Get-ChildItem client/Assets -Recurse -Include *.unity,*.prefab` | `scenes=0`、`prefabs=0` |
| 真正被构造的对象 | grep `new <T>` / `AddComponent<T>`（排除 Tests） | 只有 `EntityViews`、`FrameProfiler`、`SnapshotView`、`Hud`、`GameLoop`、`Mixer`（`GameBootstrap.cs:16-31`） |
| 零调用者的类型 | 生产代码引用扫描 | `FpsCamera`、`Batching`、`DebugPanel` 调用者为空 |
| 从未被 `new` 的视图/界面类 | 同上 | `ViewModel`、`Lobby`、`Results`、`Roster`、`Intermission`、`ArenaMesh`、`Effects`、`FpsCamera`、`DebugPanel`、`SheepInstancePool`、`AmmoLedger` |
| 自测 | `SELFTEST OK cases=132` | 全绿 |
| 发布包 | `BUILD OK ac-client-0.1.0+2bb9d18-win64.zip`，退出码 0 | 158 条目、自包含、`backend=Mono` |
| CPU 帧预算 | `client/Logs/frame-bench/direct.json` | P95 0.0069 ms、P99 0.0088 ms、0 B/帧、GC0=0 |
| `.meta` guid | 扫描 `client/Assets/**/*.meta` | `132/132` 都是 **56 字符**（Unity 惯例是 32 位十六进制） |

---

## A. 需要你出手（我做不了 / 不能单边决定）

### A1. 一台有真实图形设备的机器 —— C14 收口的前置
本机 `GPU = Null Device`、`Screen 640×480`，PlaybackEngines 里也**只有 Mono 变体**。C14 §6/DoD 的 `1920×1080`、`drawCalls ≤ 120`、`triangles ≤ 180000` 无法测量，所以 `MC14` 只能是"**未验收**"里程碑。拿到有 GPU 的机器后一条命令即可补验收：

```
git pull && pwsh -File client/tools/frame-bench.ps1 -Runs 3 -Frames 600
```

（门禁是 fail-closed 的：环境不可用退出 2、缺段判 FAIL，**不会**给你假 PASS。）

### A2. 服务端在场，跑完 C15 六步联合验收 —— `MC15` 的唯一前置
六步：连接 → 大厅 → 对局 → 波次 → 结算 → 重连。每步要贴原始输出，并且**两侧版本行必须同时入档**：

```
客户端：ac-client 0.1.0+<sha7> proto=1        （来源 Core/VersionInfo.cs）
服务端：ac_server 0.1.0 protocol=1 tick=50ms  （来源 S01/S15 冻结口径）
核对：proto == protocol 且 MAJOR.MINOR 相等；+<sha7> 与 tick=50ms 不参与判定
```

粘到 `docs/evidence/client-v2-acceptance.md` 第 2 节后即可打 `MC15`。

### A3. 三项跨计划冲突，需要裁决（我不自签）
| # | 冲突 | 现状 | 需要你/服务端确认什么 |
|---|---|---|---|
| 1 | 剔除距离 | C08 的 `90m/60m` 已在真实代码路径上；C14 §5 档位表写 `60/80m` | 以哪个为准（改代码路径等于改 C08 的已验收行为） |
| 2 | 阴影距离 | C07 `LightingRig.ShadowDistanceM = 60f`；C14 档位表 `20/35/50m`，且**高画质 50 < C07 的 60** | 三者取值与语义统一 |
| 3 | 准星散布值域 | H4：`SetSpread(SpreadDeg)` 自赋值；客户端概念与服务端 `server/src/config/weapons.hpp:25-27` 的 `0.8/0.6/4.0` 口径不一致 | 跨端语义由服务端链给结论，我不能单边改 |

### A4. `Send` / `Track` 的拒收回滚语义（跨服务端）
审计发现：`UdpTransport` 在 `Enqueue` **之前**就把可靠消息挂进重传表（`UdpTransport.cs:237-242`），因此被积压封顶拒收的消息会留在重传表里"待发但永不出队"。**是否在拒收时回滚 `Track`，取决于 S04 的语义**，我没有自行决定。

### A5.（可选）IL2CPP 模块
现在 `-Backend auto` 走 Mono 兜底并在日志与 `manifest.json` 标注 `backend=mono`。要真正发 IL2CPP 包，需要在引擎 Hub 里补装该编辑器的 IL2CPP（Windows x64）模块 + VS 的"使用 C++ 的桌面开发"。

### A6. 决定我下一步做哪条线
B1（场景与呈现装配）是从"回路能跑"到"游戏能玩"的唯一路径，也是工作量最大的一块。你说做我就做；不做的话，C14 的 GPU 段与 `stageP95` 的 `fx/overlay/draw` 会一直空着。

---

## B. 我可以独立做完（不需要你）

### B1. 场景与呈现装配（最大缺口，建议优先级最高）
现象：`scenes=0`、`prefabs=0`，`ViewModel/Lobby/Results/Roster/Intermission/ArenaMesh/Effects/FpsCamera/DebugPanel/SheepInstancePool` **从未被实例化** ⇒ 视图层与屏幕流是"写好了但没人调用"的状态。
要做：一个 Boot 场景（或继续纯代码自举）+ `GameBootstrap` 里构造 `ViewModel`/`FpsCamera`/`ArenaMesh`/`Effects`/`SheepInstancePool`/`DebugPanel`，并把大厅→对局→结算的流程接起来。
做完的连带收益：`stageP95` 的 `fx/overlay/draw` 才有真实数字，C14 的门禁才有资格谈 PASS。

### B2. `.meta` GUID 统一为 32 位十六进制
实测 `132/132` 都是 56 字符。Unity 把 guid 当不透明 ID，所以**这不是必然故障**，但不符合惯例，且很多外部流水线校验 32 hex。要做一次性重生 + 引用校验（会改变资源 ID）。

### B3. URP 悬空 guid 引用复核并修（审计已登记，本轮未处理）。

### B4. 同值双源收敛
`FrameBudget`（20/33、draw/tri/particle/material、warmup/sample/runs）vs `client/tools/frame-bench.ps1` 里的第二份表 vs `DebugPanel` 的别名常量。审查建议：由 `FrameBench` 从 `FrameBudget` 输出 `budget` 字段，ps1 用 JSON 里的值判定，**删掉文本扫描护栏**（现在是子串扫描，`20` 能匹配到 `200`）。

### B5. JSON 读取实现去重
`Core/MiniJson.cs` 与 settings 读取各一份（审计登记）。

### B6. 计划文本与实现的文件名漂移
计划里仍写 `*_test.cs` / `AudioTest` / `LobbyFlowTest` / `SettingsTest`，实现用的是 `*Suite.cs`。按规矩我不改上游计划，只能登记；要不要改计划文本需你点头。

### B7. H5：`crosshairColor` / `colorblindSafe` 只写不读（设置项无效）。

### B8. M4：`entry.VictimId = hudEvent.Wave` —— 事件字段串了，需确认字段语义后修。

### B9. 帧基准的场景口径
`bench-4p60sheep` 目前是**合成的 64 实体**负载，不是计划描述的场景（无 9:3:2:1 兵种混编、无粒子、无 24 路音源、无 30Hz bot 指令）。要么依赖 B1 造真场景，要么在证据里把该场景名判为"未达成"。

### B10. 门禁细节
GPU 指标从 `-1` 哨兵改为显式 `unverified`，`verdict` 词表收敛为 `PASS/FAIL/UNVERIFIED`。

### B11. 自测入口入库
`client/Logs/selftest.cmd` 在 gitignore 里，别人 clone 后无法照证据文档复现自测。需要把自测入口纳入仓库（或并进 `build.ps1`）。

### B12. 打包 guard
`manifest.json` 的 sha256 与归档必须始终同步（ps1 顺序是对的，但"手动重打包"会脱节）；建议打包后加一条自校验。

---

## C. 有意保留 / 已知陷阱（透明登记）

| 项 | 说明 |
|---|---|
| 脚本编码 | **无 BOM 的 UTF-8 + 中文** 会让 Windows PowerShell 5.1 解析失败（本轮 `build.ps1` 就因此根本没启动）。`build.ps1`、`tools/frame-bench.ps1` 已加 BOM，规则写进 `docs/运维手册.md` 故障表 |
| 编辑器副作用 | 打开工程会改 `client/ProjectSettings/ProjectSettings.asset`（bundle id、像素密度）。我每次回退、不入库；若某天要固化这些值需人工决定 |
| 并发写入 | 服务端链与本链共用同一仓库，出现过**索引竞争导致我的 `git commit` 静默失败**。已改为逐条检查退出码；提交只带自己的路径 |
| 门禁语义 | 帧基准两种走法都不打印 PASS（§5 合规 → 环境不可用 exit 2；`-nographics` → 缺段 FAIL）。这是刻意设计，不是故障 |

---

## D. 各计划状态一览

| 计划 | 实现 | §6 验证 / DoD | 双轴审查 | 标签 |
|---|---|---|---|---|
| C01、C02 | 早期会话交付 | 已验收 | 已审 | `MC01`、`MC02` |
| C03–C13 | 已交付 | 已验收（各自轮次） | 已审并修复 | `MC03 … MC13` |
| C14 | 已交付（含基准入口） | **CPU 段达标**；GPU 三项**未验收**（A1） | 双轴审查两轮，blocker 已修 | `MC14` = **未验收**里程碑 |
| C15 | 发布链路已实测出包；版本行/日志已交付 | 构建与日志**已验收**；六步联合验收**未做**（A2） | 未跑双轴审查 | **无 `MC15`** |

本轮客户端链条落地的提交：`ac9b9ec` → `099d1f6` → `a2bbe2b` → `42e0260` → `88663c9` → `2bb9d18` → `71c123b`。
