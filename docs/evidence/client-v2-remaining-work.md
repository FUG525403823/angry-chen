# 客户端链条（C01–C15）收官清单

> 更新：2026-09-28 ｜ 最近提交 `4611f2e`（B5）＋本轮 B1 修复轮（未提交前） ｜ 标签 `MC01 … MC14`，**无 `MC15`**
> 本文每条都有实测出处。A 段是要你出手的，B 段是我能独立做完的。

## 0. 现状

**能跑的**：帧回路（输入 → 预测/和解 → 快照镜像 → 视图同步 → HUD → 特效 → 覆盖层 → 绘制）、音频 mixer、**运行期呈现层装配**（相机/灯光/竞技场/羊群/特效/屏幕流/调试面板）、版本行与日志落盘、一条命令出发布包、资源引用守卫。
**还不能的**：**HUD/准星/屏幕流的渲染器**还没写（全仓库 `OnGUI|Canvas|DrawTexture|Blit` 命中 0），所以上屏的只有竞技场的 `MeshRenderer` 与羊的 `DrawMesh*`；以及真实出包时**本地玩家身份没有来源**（`GameLoop.LocalPlayerId` 在生产路径上没人设）。

| 事实 | 结果 |
|---|---|
| 场景 / 预制体 | 仍为 `0 / 0`——**这是设计选择**：`GameBootstrap` 用 `[RuntimeInitializeOnLoadMethod]` 纯代码自举（不手写 `.unity` YAML） |
| B1 后真正被构造的类 | `Camera`+`FpsCamera`、`LightingRig`、`ArenaMesh`（7 部件/6 材质/5 碰撞盒）、`Materials`、`SheepInstancePool`+`SheepVisuals`（经 `Culling`/`Batching`）、`Effects`、`ViewModel`、`Lobby`/`Results`/`Intermission`（按 `MatchStatePayload.Phase` 驱动）、`DebugPanel`（F3，走键位表）、准星调色板 |
| 帧分段 | `input/sync/predict/hud/fx/overlay/draw` 有生产打点；`audio` 仅在音频设备可用时接线。**打点规则**：只在该段真的做功时 `Mark` |
| 资产 guid | 136 个 `.meta` **全部 32 位十六进制**，0 例外、0 重复 |
| 自测 | `cases=145`，**非 fixture 失败 = 0**；另有 2 条失败来自并行会话未提交的 fixture 重写（见 §C.5） |
| 发布包 | `BUILD OK ac-client-0.1.0+2bb9d18-win64.zip`（`backend=Mono`） |
| CPU 帧预算 | P95 0.0069 ms、P99 0.0088 ms、0 B/帧、GC0=0 |

---

## A. 需要你出手

### A1. 一台有真实图形设备的机器 —— C14 收口前置
本机 `GPU = Null Device`、`Screen 640×480`，PlaybackEngines 只有 Mono 变体，`frame-bench.ps1 -Runs 1` 两次都是 `exit 2`（"run produced no JSON"）。C14 §6/DoD 的 `1920×1080` / `drawCalls ≤ 120` / `triangles ≤ 180000` 无法测量，所以 `MC14` 是"**未验收**"里程碑。拿到有 GPU 的机器后：

```
git pull && pwsh -File client/tools/frame-bench.ps1 -Runs 3 -Frames 600
```

门禁 fail-closed：`verdict=UNVERIFIED`（本机必然如此）或 `sceneKind≠plan-scene` ⇒ 退出 2；超预算 ⇒ 退出 1 + `FRAME-BENCH FAIL`；**只有 JSON 说 PASS 才可能 PASS**。

### A2. 服务端在场跑完 C15 六步联合验收 —— `MC15` 唯一前置
连接 → 大厅 → 对局 → 波次 → 结算 → 重连，每步贴原始输出，两侧版本行同时入档：

```
客户端：ac-client 0.1.0+<sha7> proto=1
服务端：ac_server 0.1.0 protocol=1 tick=50ms
核对：proto == protocol 且 MAJOR.MINOR 相等；+<sha7> 与 tick=50ms 不参与
```

粘进 `docs/evidence/client-v2-acceptance.md` 第 2 节即可打 `MC15`。

### A3. 三项跨计划冲突需裁决
| # | 冲突 | 现状 |
|---|---|---|
| 1 | 剔除距离 | `Batching` 档位表已有生产调用者，但 `View/Culling` 仍在用 C08 的 `90m/60m`，C14 档位表写 `60/80m` |
| 2 | 阴影距离 | C07 `LightingRig.ShadowDistanceM=60f`；C14 档位表 `20/35/50m`，高画质 50 < C07 的 60 |
| 3 | 准星散布值域 | 与 `server/src/config/weapons.hpp:25-27` 的 `0.8/0.6/4.0` 口径不一致，需服务端链结论 |

### A4. `Send` / `Track` 拒收回滚语义（跨服务端）
`UdpTransport` 在 `Enqueue` 之前就把可靠消息挂进重传表（`UdpTransport.cs:237-242`），被积压封顶拒收的消息会留在表里"待发但永不出队"。是否回滚取决于 S04 语义。

### A5. **本地玩家身份**（协议层缺口，需要设计）
呈现层已接上传输（`GameBootstrap.AttachTransport` → `Loop.OnPacket`），相位数一到达屏幕流就会切；但**没人给 `GameLoop.LocalPlayerId` 赋值**，真实出包时相机/HUD 绑不到本地玩家。协议里"客户端身份从哪来"（握手分配？房间槽位？）需要你或服务端链定。

### A6.（可选）IL2CPP 模块
`-Backend auto` 现走 Mono 兜底并在日志与 `manifest.json` 标注 `backend=mono`。要发 IL2CPP 包需补装 IL2CPP（Windows x64）模块 + VS C++ 工作负载。

### A7. **并行会话的 fixture 冲突**（不是我能碰的）
`docs/evidence/fixtures/*.json`（7 份，C1 冻结向量）在 11:23 被并行会话**未提交**地重写，客户端 `fixtures.loader` 因此加载到 0 份、`fixture_predict.*` 全红（本次复跑：`cases=145`、2 条失败全在此）。这些文件不属于客户端，我没有动；等他们提交后客户端自测会自然回绿。**两侧需要约定：fixture 冻结向量不能单方面改格式。**

---

## B. 已完成

### B1. 场景与呈现装配 ✅（含已知缺口）
- 新增 `client/Assets/Scripts/Boot/PresentationLayer.cs`、`ScreenFlow.cs`，`GameBootstrap` 成为真正的组合根：`SettingsStore` → `GameLoop` → 呈现层 → 三条呈现缝（`Fx`/`Draw`/`Overlay`）＋ `EventApplied`，破坏性重建时重造残留层。
- 屏幕流按 `MatchStatePayload.Phase`（lobby=0/loading=1/playing=2/intermission=3/ended=4）驱动；相位包（type=10）走既有 `Loop.OnPacket` 入口 ⇒ `LastMatchState`/`MatchStateCount` ⇒ `TickOverlay`。
- 传输已接：地址 `-server host:port` → `AC_SERVER` → 默认 `127.0.0.1:8787`；编辑器/批处理一律不真连。
- 打点诚实：`fx`/`draw`/`overlay` 都只在真做功时 `Mark`（`draw` 仅在真有 `DrawMeshInstanced/DrawMesh` 提交时；`overlay` 仅在相位真 Apply 或面板真可见时），未接线不打点。`FrameBench.cs` 与 `frame-bench.ps1` **未改**，门禁未放宽。

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
1. HUD/准星/屏幕流**没有渲染器**（全仓 `OnGUI|Canvas|DrawTexture|Blit` 命中 0）⇒ 上屏只有竞技场网格与羊的实例化绘制。
2. `GameLoop.LocalPlayerId` 在生产路径无人赋值（见 A5）。
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
| **非 32 位十六进制的资产 guid** | Unity 把引用**静默清零**成 `{fileID: 0}`，不报错 | 全部重生为 32 hex；新增守卫用例 `assets.guid_references_resolve` 防复发 |
| **无 BOM 的 UTF-8 `.ps1` 含中文** | Windows PowerShell 5.1 按 ANSI 解码，中文字节解出引号 ⇒ 解析失败 | `build.ps1`/`frame-bench.ps1`/`selftest.ps1` 均带 BOM |
| **假绿打点** | 没接线也 `Mark` ⇒ 值恒 0 ⇒ 该段预算永远通过 | 打点只在真做功时；`fx/draw/overlay` 三条都有"未接线 ⇒ 不打点"的用例守着 |
| 编辑器副作用 | 打开工程会改 `ProjectSettings.asset`（bundle id、像素密度） | 每次回退、不入库 |
| 并发写入 | 与服务端链共用仓库 | 逐条检查退出码；提交只带自己路径；**fixture 冻结向量被单方面改写会让客户端自测变红（见 A7）** |

---

## D. 各计划状态

| 计划 | 实现 | §6/DoD | 双轴审查 | 标签 |
|---|---|---|---|---|
| C01、C02 | 早期会话交付 | 已验收 | 已审 | `MC01`、`MC02` |
| C03–C13 | 已交付 | 已验收 | 已审并修复 | `MC03 … MC13` |
| C14 | 已交付（含基准入口） | CPU 段达标；**GPU 三项未验收**（A1） | 两轮，blocker 已修 | `MC14` = 未验收里程碑 |
| C15 | 发布链路实测出包；版本行/日志已交付 | 构建、日志、门禁已验收；**六步联合验收未做**（A2） | 未跑双轴审查 | **无 `MC15`** |
| 补齐 | B1 呈现层装配、B2 guid 统一、B3 URP 引用、B4 预算单源、B5 JSON 去重、B7 准星调色板、B8 击杀受害者、B9 基准口径、B10 门禁词表、B11 自测入口、B12 打包 guard | — | B1 已跑两轴并修复；其余为审查/审计发现 | — |

提交链：`ac9b9ec` → `099d1f6` → `a2bbe2b` → `42e0260` → `88663c9` → `2bb9d18` → `71c123b` → `982092a` → `22b452a` → `1b36859`（B1）→ `4611f2e`（B5）→ 本轮 B1 修复轮。
