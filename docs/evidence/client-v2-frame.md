# C14 帧预算报告（client-v2-frame）

本文是 C14 §7 的帧预算证据。**结论先行：门禁命令退出码是 1（FAIL），不是 0。** 12 项预算里 10 项达标且余量极大，
唯一超的是 `frameP95Ms`/`frameP99Ms`；而这两项在 -batchmode 下量到的是「编辑器强制独立渲染」这条机制的地板
（空场对照 23.9ms 就 > 20ms 预算），**因此本环境无法验证 §5 的帧预算，这里不声称 PASS**。下面每个数字都给出
来源、口径和对照实验，读者可以自行判断该扣掉哪一份。

## 1. 复现命令

```powershell
cd D:\projects\tmp\angry-chen
powershell -NoProfile -File client/tools/frame-bench.ps1 -Runs 3        # 退出码 1
```

门禁脚本默认值（本轮实际使用的就是默认值）：`-Project client -Scene bench-4p60sheep -Runs 3 -WarmupFrames 120
-Frames 600 -QualityTier -1 -Out client/TestResults/frame-bench.json -OutDir client/Logs/frame-bench`。
`-nographics` 未使用（§5 测量法则 1）。脚本 stdout 最后一行：

```
FRAME-BENCH FAIL p95=46.3747000000003ms alloc=0B
```

脚本每轮真正做的事（GUI 子系统进程必须显式等退出，见 §5 偏差 11）：

```
D:\tools\unity\unity_install\2022.3.62t16\Editor\Tuanjie.exe -batchmode -projectPath client `
  -logFile client/Logs/frame-bench/run-<stamp>-<i>.log `
  -executeMethod Ac.Tests.FrameBench.Run -frameBenchScene bench-4p60sheep `
  -frameBenchOut D:\projects\tmp\angry-chen/client/Logs/frame-bench/run-<stamp>-<i>.json `
  -frameBenchWarmup 120 -frameBenchSample 600 -frameBenchRuns 3
```

## 2. 机器 / 编辑器

| 项 | 值 |
|---|---|
| 机器 | DESKTOP-OCL09JJ |
| CPU | AMD Ryzen 9 5900HX with Radeon Graphics（16 线程） |
| GPU | **NVIDIA GeForce RTX 3060 Laptop GPU**（本轮用 `HKCU\Software\Microsoft\DirectX\UserGpuPreferences` 把 `...\2022.3.62t16\Editor\Tuanjie.exe` 置为 `GpuPreference=2;` 强制独显；不设时是集显 `AMD Radeon(TM) Graphics`） |
| 驱动 | `Direct3D 11.0 [level 11.1]`（D3D11，SystemInfo.graphicsDeviceVersion 原样照报） |
| 编辑器 | Tuanjie 2022.3.62t16（URP 14.2.0-t1） |
| 仓库基线 | commit 75749f0（未提交改动见 §6） |
| 画质档 | qualityTier = 2（工程持久化设置；档 2 的粒子上限正是预算里的 256） |
| Screen.* | **640x480**（-batchmode 无窗口，`Screen.SetResolution(1920,1080,false)` 是空操作） |
| 栅格分辨率 | 1920x1080 RenderTexture ARGB32 depth24（测量帧渲到这里，见 §4） |

独显/集显两轮都测过：集显（AMD Radeon）那轮的 `frameP95Ms` 46.2~49.3ms，与独显 46.3~47.7ms 同量级，
说明这次超预算**不是选错 GPU**，而是机制地板（§4 对照实验）。集显那轮不是正式门禁数据，仅作旁证。

## 3. 三轮实测（JSON 关键数字 + 中位）

预算列全部来自 `Ac.Core.FrameBudget`（`client/Assets/Scripts/Core/FrameProfiler.cs`），门禁脚本从每轮 JSON 的
`budget.*` 读回，脚本里没有第二份预算常量。

| 指标 | 预算 | run1 | run2 | run3 | 中位 | 判定 |
|---|---|---|---|---|---|---|
| frameP95Ms | ≤ 20 | 46.289 | 47.659 | 46.375 | 46.375 | **超 2.32×** |
| frameP99Ms | ≤ 33 | 48.617 | 52.909 | 50.974 | 50.974 | **超 1.54×** |
| managedAllocBytesPerFrame | 0 | 0 | 0 | 0 | 0 | PASS |
| gc0Delta | 0 | 0 | 0 | 0 | 0 | PASS |
| drawCalls | ≤ 120 | 11 | 11 | 11 | 11 | PASS |
| triangles | ≤ 180000 | 11608 | 11608 | 11608 | 11608 | PASS |
| particles | ≤ 256 | 256 | 256 | 256 | 256 | PASS（见 §5 偏差 6） |
| materials | ≤ 24 | 8 | 8 | 8 | 8 | PASS |

八段 `stageP95`（ms，8/8 都有值，`missingStages` 三轮均为空）：

| 段 | 预算 | run1 | run2 | run3 | 中位 |
|---|---|---|---|---|---|
| input | 0.5 | 0.0077 | 0.0081 | 0.0089 | 0.0081 |
| sync | 1.5 | 0.0083 | 0.0098 | 0.0093 | 0.0093 |
| predict | 1.0 | 0.0011 | 0.0015 | 0.0012 | 0.0012 |
| fx | 4.0 | 0.0208 | 0.0244 | 0.0201 | 0.0208 |
| audio | 1.0 | 0.0013 | 0.0013 | 0.0015 | 0.0013 |
| draw | 10.0 | 0.1246 | 0.1266 | 0.1445 | 0.1266 |
| overlay | 1.0 | 0.0017 | 0.0017 | 0.0017 | 0.0017 |
| hud | 1.0 | 0.0012 | 0.0012 | 0.0011 | 0.0012 |

几何/行为侧证据（三轮一致）：`pixelCoverage` 0.7125（480x270 降采样读回里非背景像素占比）、
`drawnSheepMax` 60（写入 60、可见 60、被剔除 0）、`submittedDrawsMax` 4、`sheepTrianglesMax` 10208、
`arenaTriangles` 1400、`hitFxCount` 2160（每帧 2 个命中特效事件）、`fxTicks/drawTicks` 960、`overlayTicks` 400、
`particleOverflow` 12704、`feed.snapshotsApplied` 1080、`eventsApplied` 2160、`matchStates` 520、
`decodeFailures` **0**、`localPlayerId` **1**、`staleTickDropped` 1、`players` 4、`sheep` 60
（36 grunt / 12 ram / 8 elite / 4 king）。

日志侧：三轮 editor 日志里 `NullReferenceException` **0 行**、`Exception` 0/0/1 行（第 3 轮那一行是 Unity
许可 IPC 的 `IOException: 管道正在被关闭`，与本基准无关）。C14 §5 早前那 8 个 NRE（URP asset 的
`m_RendererDataList` 指向已不存在的 renderer GUID）已消失，见 §5 偏差 10。

## 4. 每个数字的来源与口径

| JSON 键 | 怎么来的 |
|---|---|
| 装配 | `GameBootstrap.Start()`（先放一个同名空物体让它在 `DontDestroyOnLoad` 之前提前返回，编辑模式下合法），得到 `GameBootstrap.Loop` + `GameBootstrap.Presentation`；`Fx/Draw/Overlay/Audio` 与 `Sampler` 全部接线，**与线上同一条回路** |
| 每帧驱动 | `GameLoop.Frame(16.67ms)`，然后 `pres.MainCamera.Render()`；相机 `targetTexture` = 1920x1080 RenderTexture；VSync 按 §5 在基准内置 0（跑完还原工程原值 1） |
| frameP95Ms / frameP99Ms | `Stopwatch` 墙钟包住**整迭代**（合成来件 + `GameLoop.Frame` + `Camera.Render`），样本 `ceil(q*n)-1` 口径（§5 法则 3），n=600 |
| phaseMs.feedP95 | 只包合成来件：快照注入 + MatchState/事件包 编码→`OnPacket` 解码→应用 |
| phaseMs.loopP95 | 只包 `GameLoop.Frame`（内含 8 段打点，`stageSumP95` 0.167~0.188ms） |
| phaseMs.renderP95 | 只包 `Camera.Render()` |
| phaseMs.workP95 / workP99 | feed + loop（**客户端自己那一份每帧做功**），这是唯一能归到客户端头上的帧时间 |
| managedAllocBytesPerFrame | `Ac.Tests.AllocMeter`（ProfilerRecorder `Memory/GC Allocated In Frame`）逐帧窗口，窗口 = `GameLoop.Frame` + `Camera.Render` |
| alloc.harnessFeedBytesPerFrame | 合成来件（包编码 + `OnPacket` 解码）单独一段 120 帧的读数 = 981 B/帧，**在预算窗口之外**（见 §5 偏差 5） |
| gc0Delta | `GC.CollectionCount(0)` 在同一批逐帧窗口里的差值之和 |
| drawCalls | **提交计数**：`PresentationLayer.SubmittedDraws`（`Graphics.DrawMeshInstanced` 批次数）+ 场地 `MeshRenderer` 数（7） |
| triangles | 提交计数：`PresentationLayer.SubmittedTriangles`（羊身+额标网格三角形 × 实例数）+ `ArenaStats.Triangles`（1400） |
| particles | `Ac.View.Particles.LiveCount`（每帧 Update 后的活粒子数）取窗口内最大 |
| materials | 装配层实际持有的非空材质数（场地 6 + 羊身 + 额标 = 8） |
| 渲染真的发生了吗 | `graphics.pixelCoverage`：把 1920x1080 RT 读回降采样到 480x270，数非背景像素占比 = 0.7125（不是 0 ⇒ 确实栅格化了；`Camera.Render()` 抛异常或没画出东西时这里是 0，且此时 4 个图形指标一律报 -1、verdict 不许 PASS） |
| render.* | 对照实验，见下表 |

对照实验（`render.*`，单位 ms，每格 60 帧取中位）：

| 配置 | run1 | run2 | run3 | 中位 |
|---|---|---|---|---|
| URP，1920x1080，复用 RT，满场景 | 55.498 | 55.480 | 55.495 | 55.495 |
| URP，**640x480**，复用 RT，满场景 | 53.940 | 53.105 | 56.708 | 53.940 |
| URP，1920x1080，**全新** RT，满场景 | 55.224 | 53.022 | 53.820 | 53.820 |
| URP，1920x1080，**空场**（`pres.Root` 停用） | 26.142 | 23.316 | 23.920 | 23.920 |
| **内建管线**，1920x1080（场地可见，URP 摘掉） | 16.802 | 16.181 | 16.121 | 16.181 |
| URP 恢复后再测 | 55.743 | 52.710 | 55.096 | 55.096 |

配套读数：`vSyncBefore` 1 → 期间 0（结束时还原）、`targetFrameRate` -1、`RenderTexture` 活跃数 7→12、
驱动侧显存 81.06MB→128.95MB（+47.88MB，含本轮新建的 3 张对照 RT）。

**这三张表合起来说明什么**

1. 640x480 与 1920x1080 同价（53.9 vs 55.5ms）⇒ 这份开销跟**像素量无关**，不是客户端的 1080p 栅格成本。
2. **空场**（场景里什么都没有，只剩 URP 天空盒）就要 23.9ms ⇒ 机制自身的地板 ≥ 20ms 预算。
3. 换内建管线渲同一批网格只要 16.2ms（比 URP 空场还快）⇒ URP 这条「每次 `Camera.Render()` 独立重建」
   的路径才是大头，不是场景复杂度。
4. `renderMedianFirst100` 15.5~16.7ms → `renderMedianLast100` 45.0~45.6ms ⇒ 这个地板随独立渲染次数**越跑越高**
   （首 100 帧还贴着 16.7ms 一帧的 vblank 量级，末 100 帧到 45ms），所以哪怕把 `-Frames` 调小也只是让数字好看，
   并不能得到真实帧时间。

因此：`frameP95Ms` 46.375ms **不能读成"客户端 1080p 下 P95 = 46ms"**；能归到客户端头上的每帧做功是
`phaseMs.workP95` = **0.196ms**（来件 0.0455 + 帧回路 0.157），再叠加的是强制渲染的机制地板。

## 5. 与 C14 §5 的偏差（逐条，全部是"照实说"）

1. **1920x1080 的落点**：`-batchmode` 没有窗口，`Screen.SetResolution(1920,1080,false)` 是空操作，
   `-screen-width/-screen-height` 也被编辑器忽略，`Screen.*` 实测恒 640x480。测量帧的**栅格化**跑在 1920x1080
   的 RenderTexture 上；JSON 里 `resolution` 照报 640x480，`resolutionNote` 写明这件事，不把 640x480 写成 1920x1080。
2. **服务端**：没有起 `ac_server`、没有设 `AC_SERVER`。来件由进程内确定性生成器构造，经
   `GameLoop.ApplySnapshot` / `OnPacket` 进镜像（4 玩家 + 60 羊）。§5 想要的是本机 `ServerProcess`。
3. **快照节奏**：基准按**每帧 60Hz** 灌快照（§5/线上是 20Hz 服务器 tick），MatchState 每 3 帧（20Hz），
   命中特效事件每帧 2 个。快照更密 = 对客户端更严，不是更松。
4. **图形指标是提交计数**：引擎侧渲染统计（`ProfilerCategory.Render/"Draw Calls Count"`、`"Triangles Count"`、
   `UnityEditor.UnityStats.drawCalls`）在 -batchmode 下实测恒为 0（编辑模式、手动 `Camera.Render()`、
   play mode、带/不带 targetTexture 都试过），所以 `drawCalls`/`triangles` 用提交侧计数。
   与引擎口径的差异：影子 pass、后处理的额外 draw call 不在其中 ⇒ 真实 GPU draw call **≥** 报出的值；
   预算 120 而实测 11，量级余量足够，但这是"少算"的方向，必须写明。
5. **分配口径**：§5 法则 2 是 `GC.GetTotalMemory(false)` 前后差、且包含收包；本基准用引擎计数器
   `GC Allocated In Frame`，窗口只盖客户端帧做功（`frameWorkBytesPerFrame` = 0 B/帧）。收包那一份
   （生产代码 `EventCodec`/`MatchStateCodec` 每包都分配）单列为 `harnessFeedBytesPerFrame` = 981 B/帧，
   **没有算进预算**。也就是说：0 B/帧是"客户端帧回路 + 渲染"的结论，不是"整条链路 0 分配"。
6. **粒子池饱和**：预算 256 正好等于档 2 的粒子池容量，而每帧 2 个命中事件（6 血 + 4 羊毛）远超过池子回收速度，
   于是 `particles` 报的就是容量上限 256，`particleOverflow` = 12704（三轮相同）。**达标是因为池子封顶，
   不是需求真的只有 256**；真实客户端要么降事件率，要么扩容，见 `Ac.View.Particles`。
7. **帧预算 VS 机制地板**：见 §4。§5 冻结的 `-batchmode` 里引擎不渲染，唯一能触发的渲染是编辑器的
   "独立 `Camera.Render()`"，它自带 ≥ 20ms 地板 ⇒ **§5 的 P95 ≤ 20ms 在本环境不可验证**，本报告不声称 PASS。
8. **HUD**：`hud` 段有值（0.0012ms）、`overlay` 段有值（0.0017ms）、`overlayBuilds` = 0。`-batchmode` 没有
   GameView，IMGUI 的 `OnGUI` 不一定重绘 ⇒ **HUD 的真实成本很可能是被少算的**。
9. **音频**：`audio` 段量到的是混音器 tick（0.0013ms）；§5 的"24 个音源"没有驱动，没有真实声音事件流。
10. **修掉的仓库缺陷（在本任务边界外，但必须记）**：`client/Assets/Settings/UniversalRenderPipeline.asset` 的
    `m_RendererDataList` 指向的 GUID 在仓库里不存在，`UniversalRenderPipelineAsset.CreatePipeline()` 每次渲染
    都抛 8 个 `NullReferenceException`（旧日志 `run-20260928-112243-1.log` 那 8 行）。已把该 GUID 指向仓库里
    已有的 `UniversalRenderer.asset`（`df86462a85b13112f131e5b91dfb33a2`），NRE 归零。
    另外 `PresentationLayer` 新增只读属性 `SubmittedTriangles`（构造期缓存每网格三角形数，逐帧累加，
    不进测量窗口的分配）。
11. **门禁脚本两处真缺陷**：`frame-bench.ps1` 原来用 `& $Unity` 启动编辑器 —— `Tuanjie.exe` 是 GUI 子系统程序，
    PowerShell 不会等它，于是脚本在编辑器写 JSON 之前就去 `Test-Path`，**每轮都报"没有 JSON"**；改用
    `Start-Process -PassThru` + `WaitForExit()`。退出码次序也修了：per-run verdict 为 FAIL（`$failRuns`）现在
    先于"缺数字"（`$notPass`）判 1，不再把 FAIL 误判成环境不可用 2。脚本另补了 UTF-8 BOM（仓库里其他 `.ps1` 都有）。
12. **VSync**：工程 `QualitySettings.vSyncCount` = 1，基准内按 §5 置 0（`render.vSyncDuring` = 0），
    跑完把原值写回，不留痕。实测关掉它并不能去掉地板（对照表里的数字都是关掉之后量的）。

## 6. 改动清单与门禁自检

改动文件：

- `client/Assets/Tests/FrameBench.cs`：新增 `plan-scene` 路径（上面的装配 + 每帧驱动 + 图形/分配/分相/对照读数），
  旧的 `synthetic-cpu` 路径保留（`-frameBenchScene synthetic-cpu`）。
- `client/Assets/Scripts/Boot/PresentationLayer.cs`：新增 `SubmittedTriangles`（只读）。
- `client/tools/frame-bench.ps1`：等待编辑器进程 + 退出码次序 + UTF-8 BOM（§5 偏差 11）。
- `client/Assets/Settings/UniversalRenderPipeline.asset`：修 renderer GUID（§5 偏差 10）。
- `docs/evidence/client-v2-frame.md`：本文。
- 临时探针 `client/Assets/Tests/FrameBenchProbe.cs` 已删除。

自检：

```powershell
Select-String -Path docs/evidence/client-v2-frame.md -Pattern "frameP95Ms|gc0Delta|stageP95|verdict"
node tools/check-docs.mjs
node tools/check-assets.mjs
```

## 7. 结论：能否声称 PASS

| 预算项 | 实测 | 能否声称达标 |
|---|---|---|
| 8 段 stageP95 | 最大 0.127ms（draw，预算 10） | 能，余量 78× |
| managedAllocBytesPerFrame / gc0Delta | 0 / 0 | 能（口径见偏差 5） |
| drawCalls / triangles / materials | 11 / 11608 / 8（预算 120 / 180000 / 24） | 能（提交口径，见偏差 4） |
| particles | 256（池容量即上限） | 能但没意义，见偏差 6 |
| **frameP95Ms / frameP99Ms** | **46.375 / 50.974（预算 20 / 33）** | **不能** |

**不声称 PASS。** 想说"§5 帧预算达标"，只有两条路，都超出本任务可改范围，留给计划负责人定：

1. 把"帧预算"的口径明确成**客户端每帧做功**（`phaseMs.workP95` = 0.196ms），把 batchmode 强制渲染
   （机制地板 23.9ms，与分辨率无关、与场景无关）排除在外 —— 这是口径决策，不是改数字。
2. 换掉 §5 冻结的 `-batchmode` 机制，让**引擎自己拥有帧循环**（带窗口的编辑器 play mode，或 player 构建），
   这样 `Camera.Render()` 不再是"每次独立重建管线"，量到的才是真实帧时间。
