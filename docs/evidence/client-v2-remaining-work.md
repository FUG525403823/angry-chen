# 客户端链条（C01–C15）收官清单

> 更新：2026-09-24 ｜ 提交 `22b452a` ｜ 自测 `SELFTEST OK cases=135` ｜ 标签 `MC01 … MC14`，**无 `MC15`**
> 本文每条都有实测出处。A 段是要你出手的，B 段是我能独立做完的。

## 0. 现状

**能跑的**：帧回路（输入 → 预测/和解 → 快照镜像 → 视图同步 → HUD）、音频 mixer、版本行与日志落盘、一条命令出发布包、资源引用守卫。
**不能跑的**：**画面为空、界面流程不执行** —— `client/Assets` 里 0 个 `.unity`、0 个 `.prefab`，`ViewModel/Lobby/Results/Roster/Intermission/ArenaMesh/Effects/FpsCamera/DebugPanel/SheepInstancePool` 从未被实例化。这是 B1。

| 事实 | 结果 |
|---|---|
| 场景 / 预制体 | `scenes=0`、`prefabs=0` |
| 真正被构造的对象 | `EntityViews`、`FrameProfiler`、`SnapshotView`、`Hud`、`GameLoop`、`Mixer` |
| 零调用者类型 | `FpsCamera`、`Batching`、`DebugPanel` |
| 资产 guid | **133 个已全部重生为 32 位十六进制**，0 碰撞；`m_CustomRenderPipeline` / `m_SRPDefaultSettings` 引用已解析，编辑器跑完仍在 |
| 自测 | `SELFTEST OK cases=135`（退出码 0） |
| 发布包 | `BUILD OK ac-client-0.1.0+2bb9d18-win64.zip`，退出码 0，`backend=Mono` |
| CPU 帧预算 | P95 0.0069 ms、P99 0.0088 ms、0 B/帧、GC0=0 |

---

## A. 需要你出手

### A1. 一台有真实图形设备的机器 —— C14 收口前置
本机 `GPU = Null Device`、`Screen 640×480`，且 PlaybackEngines 只有 Mono 变体。C14 §6/DoD 的 `1920×1080` / `drawCalls ≤ 120` / `triangles ≤ 180000` 无法测量，所以 `MC14` 是"**未验收**"里程碑。拿到有 GPU 的机器后：

```
git pull && pwsh -File client/tools/frame-bench.ps1 -Runs 3 -Frames 600
```

门禁是 fail-closed：`verdict=UNVERIFIED`（本机必然如此）或 `sceneKind≠plan-scene` ⇒ 退出 2；超预算 ⇒ 退出 1 + `FRAME-BENCH FAIL`；**只有 JSON 说 PASS 才可能 PASS**。

### A2. 服务端在场跑完 C15 六步联合验收 —— `MC15` 唯一前置
连接 → 大厅 → 对局 → 波次 → 结算 → 重连，每步贴原始输出，且两侧版本行同时入档：

```
客户端：ac-client 0.1.0+<sha7> proto=1          （Core/VersionInfo.cs）
服务端：ac_server 0.1.0 protocol=1 tick=50ms     （S01/S15 冻结口径）
核对：proto == protocol 且 MAJOR.MINOR 相等；+<sha7> 与 tick=50ms 不参与
```

粘进 `docs/evidence/client-v2-acceptance.md` 第 2 节即可打 `MC15`。

### A3. 三项跨计划冲突需裁决（我不自签）
| # | 冲突 | 现状 |
|---|---|---|
| 1 | 剔除距离 | C08 `90m/60m` 在真实代码路径上；C14 档位表写 `60/80m` |
| 2 | 阴影距离 | C07 `LightingRig.ShadowDistanceM=60f`；C14 档位表 `20/35/50m`，高画质 50 < C07 的 60 |
| 3 | 准星散布值域 | H4 的 `SetSpread(SpreadDeg)` 自赋值；与 `server/src/config/weapons.hpp:25-27` 的 `0.8/0.6/4.0` 口径不一致，需服务端链结论 |

### A4. `Send` / `Track` 拒收回滚语义（跨服务端）
`UdpTransport` 在 `Enqueue` 之前就把可靠消息挂进重传表（`UdpTransport.cs:237-242`），被积压封顶拒收的消息会留在表里"待发但永不出队"。是否回滚取决于 S04 语义。

### A5.（可选）IL2CPP 模块
现在 `-Backend auto` 走 Mono 兜底并在日志与 `manifest.json` 标注 `backend=mono`。要发 IL2CPP 包需在 Hub 补装 IL2CPP（Windows x64）模块 + VS C++ 工作负载。

### A6. 决定我下一步：只有 B1 和 B5 了（见下）

---

## B. 我能独立做完的

### 已完成（本轮）
| # | 事项 | 结果 |
|---|---|---|
| **B2** | `.meta` guid 统一 | **已完成**。根因不是"不合惯例"而是**真故障**：56 字符 base64 的 guid 被 Unity 当无效引用**静默清零**（`m_CustomRenderPipeline` 曾因此变成 `{fileID: 0}`）。133 个 guid 用旧 guid 的 SHA1 确定性重生为 32 hex，引用同步重写，编辑器跑完引用仍在 |
| **B3** | URP 悬空引用 | **已完成**。`m_CustomRenderPipeline` → `Assets/Settings/UniversalRenderPipeline.asset`；`m_SRPDefaultSettings` → `UniversalRenderPipelineGlobalSettings.asset` |
| **B4** | 预算同值双源 | **已完成**。`FrameBench` 直接从 `FrameBudget` 输出 `budget`（8 指标 + 8 段），脚本删掉两张硬编码表、改用 JSON 里的值判定；护栏从子串扫描（`20` 会命中 `200`）改为"禁止出现硬编码字面量" |
| **B7** | H5 准星颜色无效 | **已完成**。`Crosshair.SetPalette` 消费 `crosshairColor`/`colorblindSafe`；色盲安全用整数亮度权重在既有调色板里选对比最大一档（不新增数据） |
| **B8** | M4 击杀受害者串位 | **已完成**。`HudEvent` 增加 `TargetId`，`PushKill` 用它（原来写的是 `hudEvent.Wave`，且 `KillEntry.Wave` 从未赋值）；`GameLoop` 拷贝 `EventEntry.TargetId`；`KillFeed`/`Hud` 新增只读 `TryGet` 使映射可观测 |
| **B9** | 帧基准场景口径 | **已完成（诚实化）**。JSON 记录 `sceneKind=synthetic-cpu`，`sceneKind≠plan-scene` ⇒ 门禁退出 2，不再可能把合成负载当计划场景判 PASS。真正造出计划场景依赖 B1 |
| **B10** | 门禁词表 | **已完成**。`verdict` 收敛为 `PASS`/`FAIL`/`UNVERIFIED`；图形指标未测得 ⇒ `UNVERIFIED` ⇒ 退出 2 |
| **B11** | 自测入口入库 | **已完成**。`client/tools/selftest.ps1`（带 BOM、读 `AC_UNITY`）成为可复现入口 |
| **B12** | 打包清单 guard | **已完成**。`build.ps1` 写完 `latest.txt`/`manifest.json` 后回读并与实时 `Get-FileHash` 比对，不一致即退出 1 |
| 新增 | 资源引用守卫 | 新用例 `assets.guid_references_resolve`：`ProjectSettings/*.asset` 里每个 32-hex 资产引用都必须在某个 `.meta` 里存在（全零/内建/包内 guid 白名单）。这条本来第一天就能抓到悬空引用 |

### 还没做
| # | 事项 | 规模 | 说明 |
|---|---|---|---|
| **B1** | **场景与呈现装配** | 大（建议单独一轮） | 建 Boot 场景（或继续纯代码自举）+ 由 `GameBootstrap` 构造 `ViewModel`/`FpsCamera`/`ArenaMesh`/`Effects`/`SheepInstancePool`/`DebugPanel`，接起大厅→对局→结算。做完 `stageP95` 的 `fx/overlay/draw` 才有真数字，C14 的 GPU 段与 B9 的计划场景才有意义 |
| **B5** | JSON 读取实现去重 | 中 | `Core/MiniJson.cs`（352 行真解析器）与 `SettingsStore` 自带的扁平读取（`Dictionary<string,string>` + `ReadInt/ReadBool`）两份。收敛需改 `SettingsStore`（613 行）并保住 `SettingsSuite` 全绿 |
| **B6** | 计划文本漂移 | 小，需你点头 | 计划里仍写 `*_test.cs`/`AudioTest`/`LobbyFlowTest`/`SettingsTest`，实现是 `*Suite.cs`。按规矩我不改上游计划，只登记 |

---

## C. 已知陷阱（入库的教训）

| 陷阱 | 症状 | 处理 |
|---|---|---|
| **非 32 位十六进制的资产 guid** | Unity 把引用**静默清零**成 `{fileID: 0}`，不报错 | 已全部重生为 32 hex；新增守卫用例防复发 |
| **无 BOM 的 UTF-8 `.ps1` 含中文** | Windows PowerShell 5.1 按 ANSI 解码，中文字节解出引号 ⇒ 解析失败 | `build.ps1`/`frame-bench.ps1`/`selftest.ps1` 均带 BOM；运维手册故障表有记录 |
| 编辑器副作用 | 打开工程会改 `ProjectSettings.asset`（bundle id、像素密度） | 每次回退、不入库 |
| 并发写入 | 与服务端链共用仓库，出现过索引竞争导致 `git commit` 静默失败 | 逐条检查退出码；提交只带自己路径 |

---

## D. 各计划状态

| 计划 | 实现 | §6/DoD | 双轴审查 | 标签 |
|---|---|---|---|---|
| C01、C02 | 早期会话交付 | 已验收 | 已审 | `MC01`、`MC02` |
| C03–C13 | 已交付 | 已验收 | 已审并修复 | `MC03 … MC13` |
| C14 | 已交付（含基准入口） | CPU 段达标；**GPU 三项未验收**（A1） | 两轮，blocker 已修 | `MC14` = 未验收里程碑 |
| C15 | 发布链路实测出包；版本行/日志已交付 | 构建、日志、门禁已验收；**六步联合验收未做**（A2） | 未跑双轴审查 | **无 `MC15`** |

本轮相关提交：`ac9b9ec` → `099d1f6` → `a2bbe2b` → `42e0260` → `88663c9` → `2bb9d18` → `71c123b` → `982092a` → `22b452a`。
