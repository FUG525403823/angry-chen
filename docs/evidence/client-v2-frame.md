# C14 帧预算报告（client-v2-frame）

本文是 C14 §7 的帧预算证据。**结论先行：按裁决后的机制（`frameP95/P99` 由"引擎自己拥有帧循环"的窗口化 player 量），
门禁命令退出码是 0（PASS）**：`frameP95Ms` = **4.4858ms**（限 20）、`frameP99Ms` = **4.9969ms**（限 33），
12 项预算全绿，客户端自己那份每帧做功 P95 = 0.2393ms。

同一次门禁里，`-batchmode` 编辑器路径照旧打印它自己那份数字（同轮 `frameP95` = 44.9787ms、`verdict` = FAIL），
**保留为诊断口径、不参与判定** —— 45ms 与 4.5ms 的差别是"机制"，不是"客户端"，本文把两条机制的数字、口径、
对照实验与每个数字的来源全部列出，读者可以自行复核。

**机制裁决已落地**：`docs/plans-v2/client/C14-性能剖析与优化.md` §5 第 1 条已按 [ADR-014](../00-共识/ADR/ADR-014-帧预算判据的测量机制.md)
换成 player 判据（`-batchmode` 降为诊断口径），§8 风险表的"P95 异常低（< 5ms）"改为按机制验证
（引擎帧计数 + 真光栅证据 + 同轮空场景地板三者齐备），报告字段补 `mechanism`。预算仍未动一个数。

> **复核状态（2026-09-29）**：完整的一次 `GATE-EXIT=0` 出自本轮实现方那次运行。随后独立复跑时与实现方的
> Unity 会话并发，player 三轮自身都是 `player exit code 0`，但编辑器诊断相位中断、脚本退 1
> ⇒ **这条 PASS 还没有被第二人独立复现过**。复跑必须独占编辑器（一次只允许一个 Unity 实例）。

## 1. 复现命令

```powershell
# ① 出包（裁决后新增：窗口化 player，引擎自己拥有帧循环）
cd D:\projects\tmp\angry-chen
powershell -NoProfile -File client/build.ps1 -Target Windows64 -Backend mono      # 退出码 0
# ② 门禁（默认 -Mechanism auto：有 client/Build/Windows64/*.exe 就走 player，否则退回 editor）
powershell -NoProfile -File client/tools/frame-bench.ps1 -Runs 3                  # 退出码 0
```

门禁脚本默认值（本轮实际使用的就是默认值）：`-Project client -Scene bench-4p60sheep -Runs 3 -WarmupFrames 120
-Frames 600 -QualityTier -1 -Mechanism auto -Out client/TestResults/frame-bench.json -OutDir client/Logs/frame-bench`。
`-nographics` 未使用（§5 测量法则 1）。脚本 stdout 最后一行（`p95=` 前缀是 §6/C15 冻结的判据格式，两条机制的
数字接在它后面）：

```
FRAME-BENCH PASS p95=4.48580000000038ms alloc=0B mechanism=player editorP95=44.9786999999997ms editorVerdict=FAIL
```

两条机制真正跑的命令行：

```
# player（判定）：窗口化，不许 -batchmode；-server 不能传给 player（Unity 自己解析 -server 并中止，见 §7 偏差 14），
# 用 AC_SERVER=127.0.0.1:0（端口非法 ⇒ 离线，与 editor 路径同语义）
client\Build\Windows64\angry-chen.exe -screen-fullscreen 0 -logFile <abs>.log `
  -frameBenchScene bench-4p60sheep -frameBenchOut <abs>.json -frameBenchWarmup 120 -frameBenchSample 600 -frameBenchRuns 3

# editor（诊断口径，只跑 1 轮，只为打印对照数字）：
Tuanjie.exe -batchmode -projectPath client -logFile <abs>.log -executeMethod Ac.Tests.FrameBench.Run `
  -frameBenchScene bench-4p60sheep -frameBenchOut <abs>.json -frameBenchWarmup 120 -frameBenchSample 600 -frameBenchRuns 3
```

两条机制共用**同一份**运行时基准实现 `client/Assets/Scripts/Boot/FrameBenchPlan.cs`（装配、合成来件、
逐帧采样、分位统计、JSON 序列化全在这一个文件里）；差别只有两条，都写进了 JSON：

| | player（判定） | editor（诊断口径） |
|---|---|---|
| 渲染 | **引擎自己的帧循环**（`Update` 返回后引擎渲一帧、present）；全程 `Camera.Render()` 调用数 = 0 | 手动 `Camera.Render()`（batchmode 里引擎不做任何渲染） |
| 帧时间口径 | 相邻两次 `Update` 起点之间的墙钟（= 引擎一帧，含渲染与 present） | 一次迭代的墙钟和（合成来件 + `GameLoop.Frame` + `Camera.Render`） |

## 2. 机器 / 编辑器 / player

| 项 | 值 |
|---|---|
| 机器 | DESKTOP-OCL09JJ |
| CPU | AMD Ryzen 9 5900HX with Radeon Graphics（16 线程） |
| GPU（player） | **NVIDIA GeForce RTX 3060 Laptop GPU**（player 自己选到独显，JSON `gpu` 原样照报） |
| GPU（editor 诊断） | 同机；本轮用 `HKCU\Software\Microsoft\DirectX\UserGpuPreferences` 把 `...\2022.3.62t16\Editor\Tuanjie.exe` 置为 `GpuPreference=2;` 强制独显（不设时是集显 `AMD Radeon(TM) Graphics`，两轮数字同量级，见 §4） |
| 驱动 | `Direct3D 11.0 [level 11.1]`（D3D11，`SystemInfo.graphicsDeviceVersion` 原样照报） |
| 引擎 | Tuanjie 2022.3.62t16（URP 14.2.0-t1），player 为 Mono 出包（本机未装 il2cpp 模块） |
| 仓库基线 | commit 75749f0 + 本轮工作区改动（未提交，清单见 §8）；出包注入的 `commit` = `d08e310`（工作区当时的 git commit） |
| 画质档 | qualityTier = 2（工程持久化设置；档 2 的粒子上限正是预算里的 256） |
| player 分辨率 | **1920x1080 真窗口**（`Screen.SetResolution(1920,1080,false)` 生效，JSON `resolution` = 1920x1080） |
| editor 分辨率 | **640x480**（`-batchmode` 无窗口，`Screen.SetResolution` 是空操作；这正是 §5 偏差 1，player 机制下不再存在） |
| 栅格分辨率 | 两条机制都真的渲了 1920x1080（player：引擎渲到窗口；editor：渲到 1920x1080 RenderTexture） |

## 3. player 机制三轮实测（判定数据）

预算列全部来自 `Ac.Core.FrameBudget`（`client/Assets/Scripts/Core/FrameProfiler.cs`），门禁脚本从每轮 JSON 的
`budget.*` 读回，脚本里没有第二份预算常量。三次运行 `verdict` 均 PASS（对应进程退出码 0）。

| 指标 | 预算 | run1 | run2 | run3 | 中位 | 判定 |
|---|---|---|---|---|---|---|
| frameP95Ms | ≤ 20 | 4.5433 | 4.3147 | 4.4858 | 4.4858 | PASS（余量 4.5×） |
| frameP99Ms | ≤ 33 | 4.9969 | 4.9638 | 5.3541 | 4.9969 | PASS（余量 6.6×） |
| managedAllocBytesPerFrame | 0 | 0 | 0 | 0 | 0 | PASS（口径见 §6） |
| gc0Delta | 0 | 0 | 0 | 0 | 0 | PASS |
| drawCalls | ≤ 120 | 11 | 11 | 11 | 11 | PASS（提交口径；同轮引擎统计见下） |
| triangles | ≤ 180000 | 11608 | 11608 | 11608 | 11608 | PASS |
| particles | ≤ 256 | 256 | 256 | 256 | 256 | PASS（见 §7 偏差 6） |
| materials | ≤ 24 | 8 | 8 | 8 | 8 | PASS |

八段 `stageP95`（ms，8/8 都有值，`missingStages` 三轮均为空）：

| 段 | 预算 | run1 | run2 | run3 | 中位 |
|---|---|---|---|---|---|
| input | 0.5 | 0.0057 | 0.0052 | 0.0057 | 0.0057 |
| sync | 1.5 | 0.0114 | 0.0115 | 0.0141 | 0.0115 |
| predict | 1.0 | 0.0012 | 0.0014 | 0.0020 | 0.0014 |
| fx | 4.0 | 0.0176 | 0.0178 | 0.0189 | 0.0178 |
| audio | 1.0 | 0.0016 | 0.0010 | 0.0015 | 0.0015 |
| draw | 10.0 | 0.1429 | 0.1598 | 0.1802 | 0.1598 |
| overlay | 1.0 | 0.0019 | 0.0018 | 0.0015 | 0.0018 |
| hud | 1.0 | 0.0018 | 0.0011 | 0.0019 | 0.0018 |

分相（`phaseMs`，ms）与"引擎真的在出帧"的证据：

| 读数 | run1 | run2 | run3 | 说明 |
|---|---|---|---|---|
| workP95（客户端每帧做功 = 来件 + 帧回路） | 0.2288 | 0.2453 | 0.2393 | **唯一能归到客户端头上的那一份** |
| feedP95（合成来件） | 0.0395 | 0.0438 | 0.0391 | 快照 + MatchState/事件 编码→解码→应用 |
| loopP95（`GameLoop.Frame`） | 0.1944 | 0.2085 | 0.1991 | 内含 8 段打点 |
| gapP95（帧时间 − 做功） | 4.3342 | 4.0693 | 4.2345 | 引擎渲染 + present + 等待 |
| engineDeltaP95（`Time.unscaledDeltaTime`） | 4.5112 | 4.2624 | 4.3824 | 引擎自己的帧时间读数，与 `frameP95` 同量级 ⇒ 两条独立读数互证 |
| engineFrames（采样窗内引擎前进了几帧） | 720 | 720 | 720 | `-batchmode` 下这个数恒为 0（§4） |

几何/行为侧证据（三轮一致，与 editor 机制逐项对得上）：`pixelCoverage` **0.7160**（窗口截图降采样后非背景像素占比；
editor 机制同场景 0.7125）、`drawnSheepMax` 60（写入 60、可见 60、被剔除 0）、`submittedDrawsMax` 4、
`sheepTrianglesMax` 10208、`arenaTriangles` 1400、`hitFxCount` 1440、`fxTicks/drawTicks` 720、`overlayTicks` 240、
`overlayBuilds` 721、`particleOverflow` 8384、`feed.snapshotsApplied` 720、`eventsApplied` 1440、`matchStates` 240、
`decodeFailures` **0**、`localPlayerId` **1**、`staleTickDropped` 1、`players` 4、`sheep` 60
（36 grunt / 12 ram / 8 elite / 4 king）。

`drawCalls`/`triangles` 本轮取的是**提交计数**（run1/run2 的 `graphics.engineDrawCallsMax` = 0）。引擎渲染统计在同一份
exe 上**时有时无**：同一轮门禁里 run3 就拿到了 `engineDrawCallsMax` = **51**、`engineTrianglesMax` = **34917**
（含阴影/后处理的真引擎口径），run1/run2 拿不到就回退提交计数 11 / 11608。两套数都在预算内（≤120 / ≤180000），
JSON 用 `graphics.engine*` 与 `metricSource.drawCalls` 记录当轮实际用了哪一套 —— 报出来的是 11，不是 51，
**方向是少算**（见 §7 偏差 4、12）。

日志侧：三轮 player 日志里 `NullReferenceException` 0 行、`Exception` 0 行（`FRAMEBENCH` 行由 `Console.Out` 与
`Debug.Log` 各写一次，所以日志里能看到两行同样的结论行）。

## 4. 可证伪的那一步：空场对照（player 机制的地板）

裁决只允许改机制、不许动预算，所以先做**可证伪的对照**：同一套 `PlanBench`、同一个场景名，只把来件与
`GameLoop.Frame` 关掉、场地网格全部停用（相机保持活着），让引擎渲一个**空场景**（`-frameBenchEmpty 1`）：

| 机制 | 空场 frameP95 | 空场 frameP99 | 满场景 frameP95 | 结论 |
|---|---|---|---|---|
| **player**（引擎帧循环） | **3.9360ms** | 4.9474ms | 4.4805ms | 地板 3.9ms ⇒ 留给客户端与场景的空间充足 |
| editor（batchmode + 手动 `Camera.Render()`） | **23.920ms**（三轮 26.142 / 23.316 / 23.920） | —— | 46.375ms | 地板 23.9ms **已经超掉整个 20ms 预算** |

空场景地板：player 3.94ms vs editor 23.92ms —— **同一条代码路径、同一个空场景，只换了"谁拥有帧循环"，
地板差 6 倍**。这把"`-batchmode` 里量到的是机制地板，不是客户端成本"从推断变成了实测；同时它是本轮的
**证伪点**：如果 player 的空场地板也 ≈24ms，就说明"引擎拥有帧循环"这个假设是错的，本报告会直接报 FAIL 而不是继续。

## 5. editor 机制（诊断口径）三轮实测与对照实验

以下数字是**上一轮已入档的那份证据**（commit `60647c5`），本轮改用同一套 `PlanBench` 重跑过 4 次诊断轮（45.17 / 45.83 / 45.99 / 44.98），最后一次（本轮门禁同轮）
`frameP95` = 44.9787ms、`frameP99` = 48.2999ms、`verdict` = FAIL，与归档值同量级（44.98~46.33ms 之间波动）。

| 指标 | 预算 | run1 | run2 | run3 | 中位 | 判定 |
|---|---|---|---|---|---|---|
| frameP95Ms | ≤ 20 | 46.289 | 47.659 | 46.375 | 46.375 | **超 2.32×** |
| frameP99Ms | ≤ 33 | 48.617 | 52.909 | 50.974 | 50.974 | **超 1.54×** |
| managedAllocBytesPerFrame | 0 | 0 | 0 | 0 | 0 | PASS |
| gc0Delta | 0 | 0 | 0 | 0 | 0 | PASS |
| drawCalls | ≤ 120 | 11 | 11 | 11 | 11 | PASS |
| triangles | ≤ 180000 | 11608 | 11608 | 11608 | 11608 | PASS |
| particles | ≤ 256 | 256 | 256 | 256 | 256 | PASS |
| materials | ≤ 24 | 8 | 8 | 8 | 8 | PASS |

八段 `stageP95` 中位（ms）：input 0.0081、sync 0.0093、predict 0.0012、fx 0.0208、audio 0.0013、draw 0.1266、
overlay 0.0017、hud 0.0012（8/8 有值）。分相：`workP95` 0.196、`feedP95` 0.0455、`loopP95` 0.157、
`renderP95` **46.222**（这一项 player 机制里不存在，JSON 记 -1）。

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

**这两组表合起来说明什么**

1. 640x480 与 1920x1080 同价（53.9 vs 55.5ms）⇒ editor 那份开销跟**像素量无关**，不是客户端的 1080p 栅格成本。
2. **空场**就要 23.9ms ⇒ 那条机制自身的地板 ≥ 20ms 预算（player 机制实测地板 3.9ms，见 §4）。
3. 换内建管线渲同一批网格只要 16.2ms（比 URP 空场还快）⇒ URP 这条"每次 `Camera.Render()` 独立重建"的路径才是大头。
4. `renderMedianFirst100` 15.5~16.7ms → `renderMedianLast100` 45.0~45.6ms ⇒ 这个地板随独立渲染次数**越跑越高**。

因此：editor 机制的 `frameP95Ms` 46.375ms **不能读成"客户端 1080p 下 P95 = 46ms"**；能归到客户端头上的每帧做功是
`phaseMs.workP95`（editor 0.196ms / player 0.2405ms，两条机制同量级）。

## 6. 两条机制对照与每个数字的来源

| 读数 | player（判定） | editor（诊断） | 谁更可信 / 为什么 |
|---|---|---|---|
| frameP95Ms | **4.4858** | 44.9787 | player：引擎自己出帧，`frame` = 真实帧时间；editor：`frame` = 手动 `Camera.Render()` 的墙钟和 |
| frameP99Ms | **4.9969** | 48.2999 | 同上 |
| frameP95Ms 的独立旁证 | `engineDeltaP95` 4.26~4.51、`gapP95` 4.07~4.33 | `engineFrames` = 0（引擎根本没前进） | player 的两条独立读数互证 |
| workP95（客户端做功） | 0.2393 | 0.196（同轮诊断 0.3407） | 都一样小 ⇒ 45ms 与 4.5ms 的差不在客户端 |
| pixelCoverage | 0.7160（窗口截图） | 0.7125（RT 读回） | 两条机制的"真的有画面"证据互相印证（差 <0.4%） |
| drawCalls / triangles | 11 / 11608（提交口径；同轮 run3 的引擎统计是 51 / 34917，见偏差 4） | 11 / 11608（提交口径；引擎统计恒 0） | 引擎口径 ≥ 提交口径；两套都在预算内 |
| managedAllocBytesPerFrame | 0 B/帧（`GC.GetTotalMemory(false)` 逐帧窗口） | 0 B/帧（引擎计数器 `Memory/GC Allocated In Frame`） | 同一窗口（来件 + `GameLoop.Frame`），两条独立手段都得 0 |
| gc0Delta | 0 | 0 | 同窗口 `GC.CollectionCount(0)` 差值之和 |
| particles / materials | 256 / 8 | 256 / 8 | `Ac.View.Particles.LiveCount` / 装配层非空材质数 |
| 8 段 stageP95 | 最大 0.1598（draw） | 归档中位最大 0.127（draw）；同轮诊断 0.2244 | `FrameProfiler` 打点，同一份代码 |

逐项来源（JSON 里同名键 `metricSource.*` 也写着同样的句子，且**每轮**由基准自己填）：

| JSON 键 | player 机制怎么来的 | editor 机制怎么来的 |
|---|---|---|
| 装配 | 出包 player 启动时 `GameBootstrap` 的 `RuntimeInitializeOnLoadMethod` 已装配好；基准用 `-frameBenchOut` 钩子接管（`client/Assets/Scripts/Boot/FrameBenchPlayer.cs`），**复用同一份 `PresentationLayer`**，不复制逻辑 | `Ac.Tests.FrameBench.Run` 里 `GameBootstrap.Start()`（先放一个同名空物体让它提前返回，编辑模式下合法） |
| 每帧驱动 | `FrameBenchDriver.Update` → `PlanBench.Tick()`（合成来件 + `GameLoop.Frame(16.67ms)`），**渲染交给引擎** | `PlanBench.Tick()` + 手动 `Camera.Render()` 渲到 1920x1080 RT |
| frameP95Ms / frameP99Ms | 相邻两次 `Update` 起点之间的 `Stopwatch` 墙钟，样本 `ceil(q*n)-1` 口径（§5 法则 3），n=600 | 一次迭代的墙钟和（来件 + `GameLoop.Frame` + `Camera.Render`），同一分位口径 |
| phaseMs.renderP95 | 不存在（-1）：没有手动渲染这一步 | 只包 `Camera.Render()` |
| managedAllocBytesPerFrame | `GC.GetTotalMemory(false)` 逐帧窗口（release player 没有 profiler 计数器，见 §7 偏差 13） | `Ac.Tests.AllocMeter` 同源的引擎计数器 `ProfilerRecorder(Memory/"GC Allocated In Frame")` |
| alloc.planWindow* | 整帧（含引擎）`GC.GetTotalMemory(false)` 前后差/帧：raw ≈ 799~806 B/帧、GC 稳定后 retained 20.5~27.3 B/帧。**只作量级指示**：Mono 的堆以块增长，这个数不是"每帧分配量" | 同左（editor 轮还出现过负值：窗口外跑过 GC） |
| gc0Delta | `GC.CollectionCount(0)` 在逐帧窗口里的差值之和 | 同左 |
| drawCalls / triangles | 引擎渲染统计（`ProfilerCategory.Render/"Draw Calls Count"`、`"Triangles Count"`）优先；拿不到时回退提交计数（`PresentationLayer.SubmittedDraws/SubmittedTriangles` + 场地 7 个 `MeshRenderer` / `ArenaStats.Triangles` 1400） | 只有提交计数（引擎统计在 `-batchmode` 恒 0） |
| particles | `Ac.View.Particles.LiveCount` 取窗口内最大 | 同左 |
| materials | 装配层实际持有的非空材质数（场地 6 + 羊身 + 额标 = 8） | 同左 |
| 渲染真的发生了吗 | `graphics.pixelCoverage`：`ScreenCapture.CaptureScreenshotAsTexture()` 拿引擎刚渲完的窗口截图，每 4 像素取样数"与左上角像素差 >24"的比例 = 0.7160 | 把 1920x1080 RT 读回降采样到 480x270 数同一口径 = 0.7125 |

## 7. 与 C14 §5 的偏差（逐条，全部是"照实说"）

1. **分辨率**：player 机制下 `resolution` 真的是 1920x1080（窗口化 `Screen.SetResolution` 生效）⇒ 上一轮的偏差 1
   在判定机制里**已经不存在**。editor 诊断轮仍是 640x480（batchmode 无窗口），JSON 照报 640x480。
2. **服务端**：没有起 `ac_server`、没有设可达的 `AC_SERVER`（player 轮显式设成端口非法的 `127.0.0.1:0` 保持离线，
   与 editor 路径"编辑器/批处理一律不真连"同语义）。来件由进程内确定性生成器构造，经 `GameLoop.ApplySnapshot` /
   `OnPacket` 进镜像（4 玩家 + 60 羊）。§5 想要的是本机 `ServerProcess`。
3. **快照节奏**：基准按**每帧 60Hz** 灌快照（§5/线上是 20Hz 服务器 tick），MatchState 每 3 帧（20Hz），
   命中特效事件每帧 2 个。快照更密 = 对客户端更严，不是更松。
4. **图形指标的引擎口径在 `-batchmode` 下不可得**：编辑器里引擎渲染统计恒为 0，所以 editor 轮只能用提交计数。
   player 里同一条记录器**时有时无**（同一份 exe、同一轮门禁：run3 拿到 51 / 34917，run1/run2 拿不到 ⇒ 回退提交计数 11 / 11608）。
   提交计数**少算**（影子 pass、后处理的额外 draw call 不在其中）⇒ 真实 GPU draw call **≥** 报出的值；
   预算 120/180000 而实测 11/11608（引擎口径 51/34917 也远在预算内），量级余量足够，但方向必须写明。
5. **分配口径**：§5 法则 2 是 `GC.GetTotalMemory(false)` 前后差、且包含收包。基准用的是**逐帧窗口**里的
   帧内分配（player：`GC.GetTotalMemory(false)` 逐帧差；editor：引擎计数器 `GC Allocated In Frame`），
   窗口 = 合成来件 + `GameLoop.Frame`（editor 轮还含手动渲染）。收包那一份在生产代码里是分配的
   （`EventCodec`/`MatchStateCodec`），基准的合成来件走同一条 `OnPacket` 解码路径，也在这个窗口内 ⇒
   窗口口径比"客户端帧回路"更严。整帧（含引擎 IMGUI 等）的堆增长单列在 `alloc.planWindow*`，**没有算进预算**。
   也就是说：0 B/帧是"这一窗口内 0 分配"的结论，不是"整条链路 0 分配"。
6. **粒子池饱和**：预算 256 正好等于档 2 的粒子池容量，而每帧 2 个命中事件（6 血 + 4 羊毛）超过池子回收速度，
   于是 `particles` 报的就是容量上限 256，`particleOverflow` = 8384（player）/ 12704（editor，帧数不同）。
   **达标是因为池子封顶，不是需求真的只有 256**；真实客户端要么降事件率，要么扩容，见 `Ac.View.Particles`。
7. **帧预算是机制相关的**：同一份代码、同一个场景，player 4.4805ms vs editor 46.3264ms；空场对照
   3.9360ms vs 23.920ms（§4）。所以两条机制的数字**不能相减**，只能各自说清"量的是哪一段时间"。
   本报告的判定只认 player 那条（引擎拥有帧循环）。
8. **HUD/overlay**：player 有真窗口，IMGUI 真的在跑（`overlayTicks` 240、`overlayBuilds` 721、
   `overlay` 段 0.0020ms）⇒ 上一轮"batchmode 里 `OnGUI` 不一定重绘、HUD 成本可能被少算"的隐患在 player 机制下
   缓解了。但基准仍然不模拟真实玩家操作（没有键鼠事件流），HUD 成本只按相位 Apply 计数。
9. **音频**：`audio` 段量到的是混音器 tick（0.0013ms）；§5 的"24 个音源"没有驱动，没有真实声音事件流。
10. **修掉的仓库缺陷（在本任务边界外，但必须记）**：`client/Assets/Settings/UniversalRenderPipeline.asset` 的
    `m_RendererDataList` 指向的 GUID 在仓库里不存在，`UniversalRenderPipelineAsset.CreatePipeline()` 每次渲染
    都抛 8 个 `NullReferenceException`。已把该 GUID 指向仓库里已有的 `UniversalRenderer.asset`
    （`df86462a85b13112f131e5b91dfb33a2`），NRE 归零（两条机制的日志都是 0 行）。
    另外 `PresentationLayer` 新增只读属性 `SubmittedTriangles`（构造期缓存每网格三角形数，逐帧累加，不进测量窗口）。
11. **门禁脚本两处真缺陷**（上一轮修）：`& $Unity` 不等 GUI 子系统进程 ⇒ 每轮都报"没有 JSON"，改用
    `Start-Process -PassThru` + `WaitForExit()`；退出码次序修成"先判超预算 FAIL(1)、再判环境不可用(2)"。
12. **门禁脚本本轮新增**：`-Mechanism auto|editor|player`（默认 auto：有出包用 player，否则退回 editor）、
    `-Player`、`-TimeoutSeconds`（player 轮等不到退出就杀掉并退 2）。player 轮强制校验两件事：
    `mechanism` 字段必须等于实际机制、`phaseMs.engineFrames > 0` 且 `engineDeltaP95 > 0`
    （**这是 player 机制区别于 editor 机制的唯一实质判据**：`-batchmode` 里引擎永远不前进帧）。
13. **release player 没有 profiler 计数器**：`ProfilerRecorder(Memory/"GC Allocated In Frame")` 在出包 player 里
    `Valid == false`（实测该轮 600/600 个窗口全部不可测 ⇒ 第一版 player 门禁 `managedAllocBytesPerFrame = -1`、
    `verdict = UNVERIFIED`）。改成：有引擎计数器就用引擎计数器，没有就退回同一窗口的
    `GC.GetTotalMemory(false)` 逐帧差（窗口里跑过 GC 的那一帧记"不可测"而不是猜一个数，见
    `alloc.unmeasurableReason`）。两种手段在**同一窗口**下都得 0 B/帧，JSON 里 `alloc.source` 记录当轮用了哪个。
14. **`-server` 不能传给 player**：Unity 播放器自己的参数表也认 `-server <slave_count> <ip:port>`，实测它把进程
    判成参数错误后直接中止（exit 1、脚本还没跑到）。改用环境变量 `AC_SERVER=127.0.0.1:0`（端口非法 ⇒ 不连）。
    顺带说明：`GameBootstrap` 的 `-server` 解析在**出包版本里根本用不上**（被引擎自己吃掉），这是产品侧的既有问题。
15. **出包 player 一度装配不出计划场景**：呈现层的材质全在运行期用 `Shader.Find` 造，而仓库里 **0 个 `.mat` 资源**、
    URP 全局设置也没引用它 ⇒ URP/Lit 在出包里被剥离，`materialsReady=false`、`Prepare()` 直接失败（ENV 2）。
    修法：把 URP/Lit 通过一张 `Resources` 材质打进包（`client/Assets/Resources/FrameBenchUrpLit.mat`）。
    先试过更"正统"的 `ProjectSettings/GraphicsSettings.asset` 的 Always Included Shaders：那要编译
    **589824 个变体**（实测 1300 秒才 0.8%），不可用，已回退。注意这暴露的是产品缺陷：**当前出包版本运行时造不出任何材质**。
16. **player 轮只跑 1 次 editor 诊断**（不是 3 次）：诊断口径的作用是"这条机制的地板有多高"，不需要中位；
    判定轮仍是 3 次取中位。`-Mechanism editor` 单独跑时判定回到 editor（退出码沿用旧语义：超预算 = 1）。
17. **VSync**：工程 `QualitySettings.vSyncCount` = 1，两条机制都在基准内按 §5 置 0、跑完把原值写回
    （player JSON `render.vSyncBefore` = 1 / `vSyncDuring` = 0；`overlayBuilds` 等读数在 720 帧后才收工）。
18. **本报告没有 git commit**：改动留在工作区（§8），出包注入的 `commit` 字段是当时的 HEAD `d08e310`，
    与"报告对应的代码"不是一回事。

## 8. 改动清单与门禁自检

改动文件（不含 `docs/plans-v2/**`，那份文件本轮**一个字都没动**）：

- `client/Assets/Scripts/Boot/FrameBenchPlan.cs`【新增】：两条机制共用的运行时基准实现（装配复用、合成来件、
  逐帧采样、分位统计、JSON 序列化、`BenchAlloc` 逐帧分配窗口）。
- `client/Assets/Scripts/Boot/FrameBenchPlayer.cs`【新增】：`[RuntimeInitializeOnLoadMethod(BeforeSceneLoad)]` 钩子，
  只在命令行有 `-frameBenchOut` 时接管；没有该参数时**一个字节都不改**（正常启动路径不受影响）。
- `client/Assets/Scripts/Boot/FrameBenchDriver.cs`【新增】：唯一的新 MonoBehaviour，把 `PlanBench` 的生命周期
  摊在引擎帧循环上（装配 → 采样 → 取窗口截图证据 → 结算 → `Application.Quit(0/1/2)`）。
  （MonoBehaviour 必须与文件同名，否则 `AddComponent` 找不到脚本类。）
- `client/Assets/Tests/FrameBench.cs`：`plan-scene` 路径改为调用 `FrameBenchPlan.cs` 的同一份实现（不复制逻辑），
  保留 editor 机制所需的对照实验与手动渲染；旧的 `synthetic-cpu` 路径保留。
- `client/tools/frame-bench.ps1`：`-Mechanism/-Player/-TimeoutSeconds`、两条机制的编排、player 轮的
  `engineFrames>0` 校验、末行同时打印两条机制的 `frameP95`（保持 §6/C15 冻结的 `p95=… alloc=…B` 前缀）。
  改动后重新补了 UTF-8 BOM（无 BOM 的中文 `.ps1` 会被 Windows PowerShell 5.1 按 ANSI 解码而解析失败，实测）。
- `client/Assets/Resources/FrameBenchUrpLit.mat`【新增】：把 URP/Lit 打进包（见偏差 15）。
- `client/Assets/Scripts/Boot/PresentationLayer.cs`：新增只读属性 `SubmittedTriangles`。
- `client/Assets/Settings/UniversalRenderPipeline.asset`：修 renderer GUID（偏差 10）。
- `docs/evidence/client-v2-frame.md`：本文；`docs/evidence/client-v2-remaining-work.md`：A1 段与优先级行。

自检：

```powershell
powershell -NoProfile -File client/tools/frame-bench.ps1 -Runs 3        # 退出码 0、末行 FRAME-BENCH PASS p95=…
node tools/check-docs.mjs                                              # 退出码 0
node tools/check-assets.mjs                                            # 退出码 0
powershell -NoProfile -File client/tools/selftest.ps1                  # SELFTEST OK cases=187
```

## 9. 结论：能否声称 PASS

| 预算项 | player 实测（判定） | editor 实测（诊断口径） | 能否声称达标 |
|---|---|---|---|
| frameP95Ms / frameP99Ms | **4.4858 / 4.9969** | 44.9787 / 48.2999 | **能**（机制见 §1；editor 那条是机制地板，不是客户端成本） |
| 8 段 stageP95 | 最大 0.1598ms（draw，预算 10） | 归档中位最大 0.127ms（draw） | 能，余量 60×+ |
| managedAllocBytesPerFrame / gc0Delta | 0 / 0 | 0 / 0 | 能（口径见偏差 5；player 用 GC 堆逐帧窗口，见偏差 13） |
| drawCalls / triangles / materials | 11 / 11608 / 8（预算 120 / 180000 / 24） | 11 / 11608 / 8 | 能（提交口径，见偏差 4） |
| particles | 256（池容量即上限） | 256 | 能但没意义，见偏差 6 |

**按裁决后的机制，12 项预算全部达标，门禁退 0，这里可以声称 PASS。** 三条必须一起读：

1. 这不是"改了预算/换了宽松口径"：预算表一个字没动（`Ac.Core.FrameBudget` 是唯一来源，脚本从每轮 JSON 读回），
   player 轮的三条运行 `verdict` 都是 PASS，且 fail-closed 的护栏（`sceneKind`、`mechanism`、`engineFrames>0`、
   图形/分配必须实测）全在。
2. 判定机制与 §5 冻结的命令（第 76 行 `-batchmode`）**不一致**：本轮按裁决用 player 判定，batchmode 路径降为诊断口径。
   把 `-Mechanism player` 写成门禁的正式机制，需要计划所有人确认 —— **待计划所有人确认**（`docs/plans-v2/**` 未改）。
3. `MC14`：按裁决后的机制判定为**通过**（A1 已闭环，见 `client-v2-remaining-work.md`）；但它依赖"§5 第 76 行那条命令
   是否同步改成 player 口径"这个悬而未决的点，所以在计划文件更新前，`MC14` 的标签信息里应当写明这条注解。
