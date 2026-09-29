using System;
using System.Globalization;
using System.IO;
using System.Text;
using Ac.Boot;
using Ac.Core;
using Ac.Net;
using Ac.Sim;
using Ac.UI;
using Ac.View;
using UnityEditor;
using UnityEngine;

namespace Ac.Tests
{
    // C14 §9 的批处理入口：
    //   Tuanjie.exe -batchmode -projectPath client -executeMethod Ac.Tests.FrameBench.Run
    //               -frameBenchScene <name> -frameBenchOut <abs path> -frameBenchWarmup N -frameBenchSample N
    // 两条路径，场景名决定走哪条：
    //   bench-4p60sheep（默认）= C14 §5 的计划场景。装配走 GameBootstrap（运行期的同一条 GameLoop +
    //     PresentationLayer），每帧驱动 Loop.Frame + Camera.Render()，图形指标从"真的提交了什么"数出来
    //     （引擎的渲染统计在 -batchmode 下恒为 0，见 docs/evidence/client-v2-frame.md 的实测与偏差表）。
    //   synthetic-cpu = v1 的合成 CPU 负载，保留旧数字：图形指标恒 -1、段不齐，因此永不 PASS。
    public static class FrameBench
    {
        public const string SyntheticScene = "synthetic-cpu";
        public const string PlanSceneFragment = "4p60sheep";
        // 60fps 固定步长：预算表本身就是按 60fps 冻结的（P95 ≤ 20ms）。
        public const double DtMs = 1000.0 / 60.0;
        // 服务端 tick = 20Hz（S12 冻结）：MatchState 按这个节奏来，战斗事件每帧都来。
        // 快照本基准每帧一发全量基线帧（理由见 RunPlanScene 的注释与 evidence 的偏差表）。
        public const int MatchStateEveryFrames = 3;
        public const int DiagnosticsFrames = 120;
        // 本地身份：靠 MatchState 里的昵称表认领 pid（Ac.Net.LocalIdentity 的运行期路径）。
        public const string BenchLocalName = "bench";

        public static void Run()
        {
            try { RunInner(); }
            catch (Exception ex)
            {
                // 基准自己抛异常绝不能让批处理挂住：写日志 + 非 0 退出码（门禁宁可红，不可卡）。
                Debug.LogError("FRAMEBENCH EXCEPTION " + ex);
                EditorApplication.Exit(1);
            }
        }

        private static void RunInner()
        {
            var args = Environment.GetCommandLineArgs();
            var scene = Arg(args, "-frameBenchScene", PlanSceneFragment);
            var outPath = Arg(args, "-frameBenchOut", "");
            var warmup = ArgInt(args, "-frameBenchWarmup", FrameBudget.WarmupFrames);
            var sample = ArgInt(args, "-frameBenchSample", FrameBudget.SampleFrames);
            var runs = ArgInt(args, "-frameBenchRuns", FrameBudget.Runs);
            var tier = ArgInt(args, "-frameBenchQuality", -1);
            if (tier >= 0) Batching.SetQualityTier(tier);
            if (!string.IsNullOrEmpty(scene) && scene == SyntheticScene)
            {
                RunSynthetic(scene, outPath, warmup, sample, runs);
                return;
            }
            if (scene == null || scene.IndexOf(PlanSceneFragment, StringComparison.Ordinal) < 0)
            {
                Console.Out.WriteLine("ENV: scene '" + scene + "' cannot be built by this harness");
                Console.Out.Flush();
                EditorApplication.Exit(2);
                return;
            }
            RunPlanScene(scene, outPath, warmup, sample, runs);
        }

        // ---- C14 §5 计划场景 ----

        private static void RunPlanScene(string scene, string outPath, int warmup, int sample, int runs)
        {
            // ① 分辨率：C14 §5 冻结 1920x1080。batchmode 没有窗口，SetResolution 是空操作（实测 Screen 恒
            //    640x480），所以下面把相机渲到 1920x1080 的 RenderTexture 上：栅格化分辨率就是冻结的那一档，
            //    而 Screen.* 照实报（不许把 640x480 写成 1920x1080）。
            var screenBefore = Screen.width + "x" + Screen.height;
            Screen.SetResolution(1920, 1080, false);
            var screenAfter = Screen.width + "x" + Screen.height;

            // ② 运行期装配：GameBootstrap.Start() 造出 Loop + PresentationLayer 并把 Fx/Draw/Overlay/Audio 与
            //    Sampler 接线。编辑模式下它最后一步 DontDestroyOnLoad 非法（编辑模式不允许），而 Start 在那之前
            //    已经完成全部装配、只在 GameObject.Find(RootName) != null 时提前返回：先放一个同名空物体，
            //    装配照跑、驱动不建 —— 基准本来就要自己按帧驱动 Loop.Frame（要量的就是这一步）。
            var placeholder = new GameObject(GameBootstrap.RootName);
            GameBootstrap.Start();
            var loop = GameBootstrap.Loop;
            var pres = GameBootstrap.Presentation;
            if (loop == null || pres == null || pres.MainCamera == null || !pres.MaterialsReady)
            {
                Console.Out.WriteLine("ENV: plan scene could not be assembled (loop=" + (loop != null)
                    + " presentation=" + (pres != null) + " materialsReady=" + (pres != null && pres.MaterialsReady) + ")");
                Console.Out.Flush();
                EditorApplication.Exit(2);
                return;
            }
            loop.LocalName = BenchLocalName;

            var renderTarget = new RenderTexture(1920, 1080, 24, RenderTextureFormat.ARGB32);
            if (!renderTarget.Create())
            {
                Console.Out.WriteLine("ENV: no graphics device (1920x1080 RenderTexture could not be created)");
                Console.Out.Flush();
                EditorApplication.Exit(2);
                return;
            }
            pres.MainCamera.targetTexture = renderTarget;

            // §5 冻结契约：VSync off / targetFrameRate 不设限。工程里的 QualitySettings 是 1（编辑器默认），
            // 而 batchmode 下没有交换链、present 是否仍按 vblank 阻塞只能实测 —— 先按 §5 关掉，
            // 跑完再写回原值（不把基准的设置留进工程文件）。
            var vSyncBefore = QualitySettings.vSyncCount;
            var targetFpsBefore = Application.targetFrameRate;
            QualitySettings.vSyncCount = 0;
            Application.targetFrameRate = -1;

            // ③ 几何/材质基数：构造期数一次（Mesh.triangles 每次取值都会分配一份 int[]，绝不能进测量窗口）
            var arenaRenderers = 0;
            var renderers = pres.Root.GetComponentsInChildren<MeshRenderer>();
            for (var i = 0; i < renderers.Length; i++) if (renderers[i].enabled) arenaRenderers += 1;
            var arenaTriangles = pres.ArenaStats.Triangles;
            var materialsLive = CountLiveMaterials(pres);

            var frame = default(SnapshotFrame);
            var encoder = new PacketEncoder();
            var eventPayload = new byte[EventPayloadBytes];
            var matchPayload = BuildMatchStatePayload();
            var header = default(PacketHeader);
            var snapshotTick = 1u;
            var eventId = 1u;
            var eventTick = 1u;

            // ④ 预热：快照每帧一发（全量基线帧）。§5 的服务器 tick 是 20Hz，但基准场景要的是"屏幕上
            //    真的有 60 只羊"——稀疏快照会让镜像把一部分实体判成离场（实测只剩 29 只可见），
            //    那样量到的就不是 60 羊场景了。快照率这条偏差写在 evidence 文档里。
            for (var i = 0; i < warmup; i++)
            {
                snapshotTick += 1;
                FeedSnapshot(loop, ref frame, snapshotTick);
                if (i % MatchStateEveryFrames == 0) FeedMatchState(loop, encoder, matchPayload, ref header, (uint)(i + 1));
                FeedEvents(loop, encoder, eventPayload, ref header, ref eventId, eventTick++);
                loop.Frame(DtMs);
                pres.MainCamera.Render();
            }
            // 陈旧 tick 必须被镜像整帧丢弃（§5.2）：喂同一 tick 第二次，SnapshotsApplied 不许动。
            var appliedBeforeDuplicate = loop.SnapshotsApplied;
            var duplicateAccepted = loop.ApplySnapshot(frame);
            var staleTickDropped = !duplicateAccepted && loop.SnapshotsApplied == appliedBeforeDuplicate;

            // ⑤ 渲染证据：真渲到一张小 RT 上读回像素，数"不是背景色"的比例。读回不可用时记 -1（不拿它判 PASS，
            //    但覆盖率为 0 说明这一帧什么都没画出来 —— 那时图形指标一律记 -1，判 UNVERIFIED）。
            var pixelCoverage = -1.0;
            var pixelNote = "readback unavailable";
            try
            {
                pixelCoverage = MeasurePixelCoverage(pres, 480, 270);
                pixelNote = "480x270 RenderTexture readback, pixels differing from the top-left pixel by >24";
            }
            catch (Exception ex) { pixelNote = "readback failed: " + ex.GetType().Name; }

            // ⑥ 测量：每帧 = [合成来件] + [帧回路 + 渲染]。分配/GC0 的窗口只盖住"客户端帧做功"这一段
            //    （合成来件的构造与 OnPacket 的解码在窗口外，单独量出来报，不当预算内的数字）。
            var frameMs = new double[sample];
            var feedMs = new double[sample];
            var loopMs = new double[sample];
            var renderMs = new double[sample];
            var workMs = new double[sample];
            var measureFrames = 0;
            long allocBytes = 0;
            var allocUnmeasurable = 0;
            string allocReason = null;
            var gc0Delta = 0;
            var drawCallsMax = 0;
            var trianglesMax = 0;
            var particlesMax = 0;
            var drawnSheepMax = 0;
            var visibleSheepMax = 0;
            var skippedSheepMax = 0;
            for (var i = 0; i < warmup + sample; i++)
            {
                var tFeed0 = NowMs();
                snapshotTick += 1;
                FeedSnapshot(loop, ref frame, snapshotTick);
                if (i % MatchStateEveryFrames == 0) FeedMatchState(loop, encoder, matchPayload, ref header, (uint)(i + 1));
                FeedEvents(loop, encoder, eventPayload, ref header, ref eventId, eventTick++);
                var feedCost = NowMs() - tFeed0;
                if (i < warmup)
                {
                    loop.Frame(DtMs);
                    pres.MainCamera.Render();
                    continue;
                }
                var allocStart = AllocMeter.Begin();
                var gc0Start = GC.CollectionCount(0);
                var t0 = NowMs();
                loop.Frame(DtMs);
                var tLoop = NowMs();
                pres.MainCamera.Render();
                var tEnd = NowMs();
                frameMs[measureFrames] = tEnd - t0;
                feedMs[measureFrames] = feedCost;
                loopMs[measureFrames] = tLoop - t0;
                renderMs[measureFrames] = tEnd - tLoop;
                workMs[measureFrames] = feedCost + (tLoop - t0);
                string reason;
                var bytes = AllocMeter.BytesSince(allocStart, out reason);
                if (bytes < 0) { allocUnmeasurable += 1; allocReason = reason; }
                else allocBytes += bytes;
                gc0Delta += GC.CollectionCount(0) - gc0Start;
                var draws = pres.SubmittedDraws + arenaRenderers;
                var tris = pres.SubmittedTriangles + arenaTriangles;
                var live = pres.Effects.ParticlePool.LiveCount;
                if (draws > drawCallsMax) drawCallsMax = draws;
                if (tris > trianglesMax) trianglesMax = tris;
                if (live > particlesMax) particlesMax = live;
                if (pres.DrawnSheepCount > drawnSheepMax) drawnSheepMax = pres.DrawnSheepCount;
                if (pres.CulledSheepCount > visibleSheepMax) visibleSheepMax = pres.CulledSheepCount;
                if (pres.Sheep.SkippedCount > skippedSheepMax) skippedSheepMax = pres.Sheep.SkippedCount;
                measureFrames += 1;
            }
            var allocMeasured = allocUnmeasurable == 0 && measureFrames > 0;
            var allocPerFrame = allocMeasured ? allocBytes / (double)measureFrames : -1.0;

            // ⑦ 诊断：合成来件本身的解码代价（生产代码每包都分配，见 evidence 的偏差表）；以及把整个迭代
            //    都算进去的透明读数。两段都不进上面的预算窗口。
            var feedAlloc = MeasureFeedAlloc(loop, ref frame, encoder, eventPayload, matchPayload, ref header, ref snapshotTick, ref eventId, ref eventTick, DiagnosticsFrames);
            var wholeAlloc = MeasureWholeLoopAlloc(loop, pres, ref frame, encoder, eventPayload, matchPayload, ref header, ref snapshotTick, ref eventId, ref eventTick, DiagnosticsFrames);

            // ⑧ 渲染成本定标：同一相机在"复用 1920x1080 / 640x480 / 全新 1920x1080"三种目标上各渲
            //    DiagnosticsFrames 帧取中位，再报 RenderTexture 活跃数与驱动侧显存。batchmode 没有引擎
            //    帧循环，Camera.Render() 是"独立渲染"（URP 每次都要建全屏临时 RT），这一步就是量它到底
            //    是像素量、分辨率还是固定开销；显存/RT 计数用来排除"越跑越漏"。
            var rtCountBefore = CountLiveRenderTextures();
            var driverMemBefore = DriverMemoryMb();
            var bigReuseMs = MeasureRenderMs(pres, renderTarget, DiagnosticsFrames);
            var smallRt = new RenderTexture(640, 480, 24, RenderTextureFormat.ARGB32);
            smallRt.Create();
            var smallMs = MeasureRenderMs(pres, smallRt, DiagnosticsFrames);
            var freshRt = new RenderTexture(1920, 1080, 24, RenderTextureFormat.ARGB32);
            freshRt.Create();
            var freshMs = MeasureRenderMs(pres, freshRt, DiagnosticsFrames);
            pres.MainCamera.targetTexture = renderTarget;

            // ⑨ 对照组：这个地板到底是"场景/像素"还是"独立渲染这条路本身"。
            //    空场：把装配整体停掉（场地也是 pres.Root 的子物体），只留 URP 的天空盒 —— 分辨率相同。
            //    内建管线：同一批网格、同一分辨率，换成内建管线渲（URP 被摘掉），比"同路径同分辨率"更能说明问题。
            var rootWasActive = pres.Root == null || pres.Root.activeSelf;
            if (pres.Root != null) pres.Root.SetActive(false);
            var emptyMs = MeasureRenderMs(pres, renderTarget, DiagnosticsFrames);
            if (pres.Root != null) pres.Root.SetActive(rootWasActive);
            var urpAsset = UnityEngine.Rendering.GraphicsSettings.renderPipelineAsset;
            UnityEngine.Rendering.GraphicsSettings.renderPipelineAsset = null;
            var builtinMs = MeasureRenderMs(pres, renderTarget, DiagnosticsFrames);
            UnityEngine.Rendering.GraphicsSettings.renderPipelineAsset = urpAsset;
            var urpAgainMs = MeasureRenderMs(pres, renderTarget, DiagnosticsFrames);
            var rtCountAfter = CountLiveRenderTextures();
            var driverMemAfter = DriverMemoryMb();
            smallRt.Release();
            freshRt.Release();
            QualitySettings.vSyncCount = vSyncBefore;
            Application.targetFrameRate = targetFpsBefore;

            Array.Sort(frameMs);
            var p95 = Quantile(frameMs, 0.95);
            var p99 = Quantile(frameMs, 0.99);
            // 分相归因：帧预算的 45ms 到底花在哪一段（帧回路自己的 8 段 P95 加起来只有 0.3ms，
            // 差额只能在"合成来件"与"渲染"两处）。同时报前 100 帧/后 100 帧的中位，
            // 用来区分"稳态就是这么慢"与"随时间累积变慢"。
            // 先取"前 100 / 后 100 帧"的中位（必须在排序之前，否则取到的是最小/最大的 100 个样本）。
            var feedFirst = MedianOf(feedMs, 0, 100);
            var feedLast = MedianOf(feedMs, measureFrames - 100, 100);
            var renderFirst = MedianOf(renderMs, 0, 100);
            var renderLast = MedianOf(renderMs, measureFrames - 100, 100);
            Array.Sort(feedMs);
            Array.Sort(loopMs);
            Array.Sort(renderMs);
            Array.Sort(workMs);
            var workP95 = Quantile(workMs, 0.95);
            var workP99 = Quantile(workMs, 0.99);
            var feedP95 = Quantile(feedMs, 0.95);
            var loopP95 = Quantile(loopMs, 0.95);
            var renderP95 = Quantile(renderMs, 0.95);
            var profiler = loop.Profiler;

            var graphicsMeasured = pixelCoverage != 0.0;
            var drawCalls = graphicsMeasured ? drawCallsMax : -1;
            var triangles = graphicsMeasured ? trianglesMax : -1;
            var particles = graphicsMeasured ? particlesMax : -1;
            var materials = graphicsMeasured ? materialsLive : -1;

            var stageMissing = 0;
            for (var i = 0; i < FrameProfiler.StageCount; i++) if (!(profiler.P95Ms(i) > 0f)) stageMissing += 1;

            var overBudget = p95 > FrameBudget.FrameP95BudgetMs || p99 > FrameBudget.FrameP99BudgetMs
                || (allocMeasured && allocPerFrame > FrameBudget.ManagedAllocBudgetBytes) || gc0Delta > FrameBudget.Gc0DeltaBudget;
            for (var i = 0; i < FrameProfiler.StageCount; i++) if (profiler.P95Ms(i) > FrameBudget.StageBudgetMs[i]) overBudget = true;
            if (drawCalls > FrameBudget.DrawCallBudget || triangles > FrameBudget.TriangleBudget
                || particles > FrameBudget.ParticleBudget || materials > FrameBudget.MaterialBudget) overBudget = true;

            // PASS 只留给"图形指标齐 + 8 段齐 + 分配可测 + 全部已测指标未超预算"。
            var verdict = overBudget ? "FAIL"
                : (graphicsMeasured && stageMissing == 0 && allocMeasured) ? "PASS" : "UNVERIFIED";

            var sb = new StringBuilder();
            sb.Append("{\n");
            Meta(sb, "machine", Environment.MachineName);
            Meta(sb, "cpu", Safe(delegate { return SystemInfo.processorType; }, "unknown"));
            Meta(sb, "gpu", Safe(delegate { return SystemInfo.graphicsDeviceName; }, "none"));
            Meta(sb, "driver", Safe(delegate { return SystemInfo.graphicsDeviceVersion; }, "none"));
            Meta(sb, "unityVersion", Application.unityVersion);
            // 照实报 Screen（batchmode 恒 640x480）：真正的栅格分辨率在 renderTarget 里。
            Meta(sb, "resolution", Screen.width > 0 ? Screen.width + "x" + Screen.height : "headless");
            Meta(sb, "qualityTier", Batching.QualityTier.ToString(CultureInfo.InvariantCulture));
            Meta(sb, "scene", scene);
            Meta(sb, "sceneKind", "plan-scene");
            Meta(sb, "warmupFrames", warmup.ToString(CultureInfo.InvariantCulture));
            Meta(sb, "sampleFrames", measureFrames.ToString(CultureInfo.InvariantCulture));
            Meta(sb, "runs", runs.ToString(CultureInfo.InvariantCulture));
            Meta(sb, "commit", Safe(delegate { return GitCommit(); }, "unknown"));
            Meta(sb, "verdict", verdict);
            Meta(sb, "renderMechanism", "GameBootstrap 装配同一条运行期回路；每帧 GameLoop.Frame(16.67ms) + Camera.Render()"
                + "（camera.targetTexture = 1920x1080 RenderTexture；-nographics 未使用）");
            Meta(sb, "renderTarget", "1920x1080 RenderTexture ARGB32 depth24");
            Meta(sb, "resolutionNote", "-batchmode 无窗口：Screen.SetResolution(1920,1080,false) 是空操作（screenBefore=" + screenBefore
                + " screenAfter=" + screenAfter + "），-screen-width/-screen-height 也被编辑器忽略；测量帧栅格化在 1920x1080 的 RenderTexture 上，"
                + "Screen.* 按实测照报，不写成 1920x1080");
            Meta(sb, "renderPixelEvidence", pixelNote);
            Meta(sb, "entitySource", "本进程内的确定性生成器（4 玩家 + 60 羊），经 GameLoop.ApplySnapshot 进镜像；未设 AC_SERVER、未起 ac_server 进程");
            Meta(sb, "metricScopeNote", "frameP95Ms/frameP99Ms = 整迭代（合成来件 + GameLoop.Frame + 强制 1920x1080 Camera.Render）。"
                + "batchmode 里 Camera.Render() 是编辑器独立渲染，实测有与分辨率无关的地板：空场 URP 24.35ms、内建管线 17.48ms、"
                + "640x480 与 1920x1080 同价（见 render.*）。所以这一项量到的主要是机制地板，不能算到客户端头上；"
                + "归因见 phaseMs.*（workP95 = 来件 + 帧回路）与 render.*（对照实验）。");
            Meta(sb, "verdictNote", verdict == "PASS"
                ? "plan-scene：GameBootstrap 装配 + 每帧 Camera.Render()；图形指标是提交计数（引擎渲染统计在 -batchmode 下恒 0），分配窗口只盖客户端帧做功"
                : "plan-scene：非 PASS。overBudget=" + overBudget + " stageMissing=" + stageMissing
                    + " graphicsMeasured=" + graphicsMeasured + " allocMeasured=" + allocMeasured
                    + " pixelCoverage=" + pixelCoverage.ToString("R", CultureInfo.InvariantCulture)
                    + "；超的只有 frameP95Ms/frameP99Ms（整迭代），其构成见 phaseMs（workP95=" + workP95.ToString("R", CultureInfo.InvariantCulture)
                    + "）与 render 对照（空场地板 " + emptyMs.ToString("R", CultureInfo.InvariantCulture) + "ms）");
            sb.Append("  \"budget\": {\n");
            Num(sb, 4, "frameP95Ms", FrameBudget.FrameP95BudgetMs);
            Num(sb, 4, "frameP99Ms", FrameBudget.FrameP99BudgetMs);
            Num(sb, 4, "managedAllocBytesPerFrame", FrameBudget.ManagedAllocBudgetBytes);
            Num(sb, 4, "gc0Delta", FrameBudget.Gc0DeltaBudget);
            Num(sb, 4, "drawCalls", FrameBudget.DrawCallBudget);
            Num(sb, 4, "triangles", FrameBudget.TriangleBudget);
            Num(sb, 4, "particles", FrameBudget.ParticleBudget);
            Num(sb, 4, "materials", FrameBudget.MaterialBudget);
            sb.Append("    \"stageP95Ms\": {\n");
            for (var i = 0; i < FrameProfiler.StageCount; i++) Num(sb, 6, FrameBudget.StageNames[i], FrameBudget.StageBudgetMs[i]);
            TrimLastComma(sb);
            sb.Append("    }\n");
            sb.Append("  },\n");
            Num(sb, "frameP95Ms", p95);
            Num(sb, "frameP99Ms", p99);
            Num(sb, "managedAllocBytesPerFrame", allocPerFrame);
            Num(sb, "gc0Delta", gc0Delta);
            Num(sb, "drawCalls", drawCalls);
            Num(sb, "triangles", triangles);
            Num(sb, "particles", particles);
            Num(sb, "materials", materials);
            // 八段一律报（没测到的报 0 ⇒ verdict 已经因此不是 PASS；ps1 也按"缺段"处理）。
            sb.Append("  \"stageP95\": {\n");
            for (var i = 0; i < FrameProfiler.StageCount; i++) Num(sb, 4, FrameBudget.StageNames[i], profiler.P95Ms(i));
            TrimLastComma(sb);
            sb.Append("  },\n");
            // 每个数字的来源必须自报（谁读这张表都要知道它量的是什么）。
            sb.Append("  \"metricSource\": {\n");
            Meta(sb, "  frameP95Ms", "Stopwatch 墙钟：一次测量迭代 = 合成来件 + GameLoop.Frame + Camera.Render，样本 ceil(0.95*n)-1 口径");
            Meta(sb, "  frameP99Ms", "同上，ceil(0.99*n)-1");
            Meta(sb, "  managedAllocBytesPerFrame", "Ac.Tests.AllocMeter（ProfilerRecorder Memory/'GC Allocated In Frame'），每帧一个窗口，窗口 = GameLoop.Frame + Camera.Render；合成来件在窗口外，另行单列 alloc.harnessFeedBytesPerFrame");
            Meta(sb, "  gc0Delta", "GC.CollectionCount(0) 在同一批逐帧窗口里的差值之和");
            Meta(sb, "  drawCalls", "提交计数：PresentationLayer.SubmittedDraws（Graphics.DrawMeshInstanced 次数）+ 场地 MeshRenderer 数（" + arenaRenderers
                + "）；取样本窗口内最大值。引擎侧 ProfilerCategory.Render/'Draw Calls Count' 与 UnityEditor.UnityStats.drawCalls 在 -batchmode 下实测恒为 0，故不采用");
            Meta(sb, "  triangles", "提交计数：PresentationLayer.SubmittedTriangles（羊身+额标网格三角形 × 实例数）+ ArenaStats.Triangles（" + arenaTriangles
                + "）；取样本窗口内最大值");
            Meta(sb, "  particles", "Ac.View.Particles.LiveCount（特效层活粒子数，每帧 Update 后的实时值）；取样本窗口内最大值");
            Meta(sb, "  materials", "装配层实际持有的非空材质数（场地 6 + 羊身 + 额标）");
            TrimLastComma(sb);
            sb.Append("  },\n");
            sb.Append("  \"render\": {\n");
            Num(sb, 4, "vSyncBefore", vSyncBefore);
            Num(sb, 4, "vSyncDuring", 0);
            Num(sb, 4, "targetFrameRate", -1);
            Num(sb, 4, "bigReuseMedianMs", bigReuseMs);
            Num(sb, 4, "small640MedianMs", smallMs);
            Num(sb, 4, "freshBigMedianMs", freshMs);
            Num(sb, 4, "emptySceneUrpMedianMs", emptyMs);
            Num(sb, 4, "builtinPipelineMedianMs", builtinMs);
            Num(sb, 4, "urpAgainMedianMs", urpAgainMs);
            Num(sb, 4, "rtCountBefore", rtCountBefore);
            Num(sb, 4, "rtCountAfter", rtCountAfter);
            Num(sb, 4, "driverMemBeforeMb", driverMemBefore);
            Num(sb, 4, "driverMemAfterMb", driverMemAfter);
            Num(sb, 4, "driverMemDeltaMb", driverMemAfter - driverMemBefore);
            TrimLastComma(sb);
            sb.Append("  },\n");
            sb.Append("  \"phaseMs\": {\n");
            Num(sb, 4, "feedP95", feedP95);
            Num(sb, 4, "loopP95", loopP95);
            Num(sb, 4, "renderP95", renderP95);
            Num(sb, 4, "workP95", workP95);
            Num(sb, 4, "workP99", workP99);
            Num(sb, 4, "feedMedianFirst100", feedFirst);
            Num(sb, 4, "feedMedianLast100", feedLast);
            Num(sb, 4, "renderMedianFirst100", renderFirst);
            Num(sb, 4, "renderMedianLast100", renderLast);
            Num(sb, 4, "stageSumP95", StageSumP95(profiler));
            TrimLastComma(sb);
            sb.Append("  },\n");
            sb.Append("  \"alloc\": {\n");
            Num(sb, 4, "frameWorkBytesPerFrame", allocMeasured ? allocBytes / (double)measureFrames : -1.0);
            Num(sb, 4, "harnessFeedBytesPerFrame", feedAlloc);
            Num(sb, 4, "wholeLoopBytesPerFrame", wholeAlloc);
            Num(sb, 4, "unmeasurableWindows", allocUnmeasurable);
            TrimLastComma(sb);
            sb.Append("  },\n");
            sb.Append("  \"graphics\": {\n");
            Num(sb, 4, "pixelCoverage", pixelCoverage);
            Num(sb, 4, "arenaRenderers", arenaRenderers);
            Num(sb, 4, "arenaTriangles", arenaTriangles);
            Num(sb, 4, "materialsLive", materialsLive);
            Num(sb, 4, "submittedDrawsMax", drawCallsMax - arenaRenderers);
            Num(sb, 4, "sheepTrianglesMax", trianglesMax - arenaTriangles);
            Num(sb, 4, "drawnSheepMax", drawnSheepMax);
            Num(sb, 4, "visibleSheepMax", visibleSheepMax);
            Num(sb, 4, "writtenSheepMax", pres.WrittenSheepCount);
            Num(sb, 4, "skippedSheepMax", skippedSheepMax);
            Num(sb, 4, "particleOverflow", pres.Effects.ParticlePool.OverflowCount);
            Num(sb, 4, "drawTicks", pres.DrawTicks);
            Num(sb, 4, "fxTicks", pres.FxTicks);
            Num(sb, 4, "overlayTicks", pres.OverlayTicks);
            Num(sb, 4, "hitFxCount", pres.HitFxCount);
            Num(sb, 4, "overlayBuilds", pres.OverlayBuilds);
            TrimLastComma(sb);
            sb.Append("  },\n");
            sb.Append("  \"feed\": {\n");
            Num(sb, 4, "snapshotEveryFrames", 1);
            Num(sb, 4, "matchStateEveryFrames", MatchStateEveryFrames);
            Num(sb, 4, "eventPayloadBytes", eventPayload.Length);
            Num(sb, 4, "matchStatePayloadBytes", matchPayload.Length);
            Num(sb, 4, "players", 4);
            Num(sb, 4, "sheep", 60);
            Meta(sb, 4, "sheepKindMix", "36 grunt / 12 ram / 8 elite / 4 king（羊形由快照 state 字节推：state>=9 才是羊王，其余一律 grunt —— PresentationLayer.SheepFormOf）");
            Num(sb, 4, "staleTickDropped", staleTickDropped ? 1 : 0);
            Num(sb, 4, "snapshotsApplied", loop.SnapshotsApplied);
            Num(sb, 4, "eventsApplied", loop.EventsApplied);
            Num(sb, 4, "matchStates", loop.MatchStateCount);
            Num(sb, 4, "localPlayerId", loop.LocalPlayerId);
            Num(sb, 4, "decodeFailures", loop.DecodeFailures);
            Num(sb, 4, "frames", loop.Frames);
            TrimLastComma(sb);
            sb.Append("  },\n");
            Num(sb, "frames", loop.Frames);
            Num(sb, "snapshotsApplied", loop.SnapshotsApplied);
            Num(sb, "bootFrames", loop.Frames);
            TrimLastComma(sb);
            sb.Append("\n}\n");

            WriteOut(outPath, sb.ToString());
            Console.Out.WriteLine("FRAMEBENCH " + verdict
                + " p95=" + p95.ToString("R") + " p99=" + p99.ToString("R")
                + " alloc=" + (allocMeasured ? allocPerFrame.ToString("R") : "UNMEASURED") + " gc0=" + gc0Delta
                + " drawCalls=" + drawCalls + " triangles=" + triangles + " particles=" + particles + " materials=" + materials
                + " workP95=" + workP95.ToString("R") + " feedP95=" + feedP95.ToString("R") + " renderP95=" + renderP95.ToString("R")
                + " renderFloor(emptyUrp/builtin)=" + emptyMs.ToString("R") + "/" + builtinMs.ToString("R")
                + " frames=" + loop.Frames + " out=" + (outPath ?? "(none)"));
            Console.Out.Flush();
            EditorApplication.Exit(verdict == "PASS" ? 0 : 1);
        }

        private static bool locationWarn(bool bad) { return bad; }

        private static void WriteOut(string outPath, string json)
        {
            if (string.IsNullOrEmpty(outPath)) return;
            var dir = Path.GetDirectoryName(outPath);
            if (!string.IsNullOrEmpty(dir)) Directory.CreateDirectory(dir);
            File.WriteAllText(outPath, json);
        }

        // 场地 6 种 + 羊身 + 额标：非空的才算（材质造不出来时是 null，不能拿常量凑）。
        private static int CountLiveMaterials(PresentationLayer pres)
        {
            var live = 0;
            if (pres.MaterialTable.Grass != null) live += 1;
            if (pres.MaterialTable.Dirt != null) live += 1;
            if (pres.MaterialTable.Fence != null) live += 1;
            if (pres.MaterialTable.BarnWall != null) live += 1;
            if (pres.MaterialTable.BarnRoof != null) live += 1;
            if (pres.MaterialTable.Hay != null) live += 1;
            if (pres.MaterialsReady) live += 2;
            return live;
        }

        // 渲染证据：渲到一张小 RT 上读回像素，数"与左上角像素差 >24"的比例。
        private static double MeasurePixelCoverage(PresentationLayer pres, int width, int height)
        {
            var rt = RenderTexture.GetTemporary(width, height, 24, RenderTextureFormat.ARGB32);
            var previous = pres.MainCamera.targetTexture;
            pres.MainCamera.targetTexture = rt;
            pres.MainCamera.Render();
            RenderTexture.active = rt;
            var texture = new Texture2D(width, height, TextureFormat.RGBA32, false);
            texture.ReadPixels(new Rect(0, 0, width, height), 0, 0, false);
            texture.Apply(false);
            RenderTexture.active = null;
            pres.MainCamera.targetTexture = previous;
            var pixels = texture.GetPixels32();
            UnityEngine.Object.DestroyImmediate(texture);
            RenderTexture.ReleaseTemporary(rt);
            var reference = pixels[0];
            var different = 0;
            for (var i = 0; i < pixels.Length; i++)
            {
                var p = pixels[i];
                var delta = Math.Abs(p.r - reference.r) + Math.Abs(p.g - reference.g) + Math.Abs(p.b - reference.b);
                if (delta > 24) different += 1;
            }
            return (double)different / pixels.Length;
        }

        private static void FeedSnapshot(GameLoop loop, ref SnapshotFrame frame, uint tick)
        {
            FillPlanFrame(ref frame, tick);
            loop.ApplySnapshot(frame);
        }

        private static void FeedMatchState(GameLoop loop, PacketEncoder encoder, byte[] payload, ref PacketHeader header, uint tick)
        {
            encoder.Begin(payload);
            encoder.U8(2);                          // phase = playing
            encoder.U8((byte)(tick / 20u % 4u + 1u));   // wave
            encoder.U16(3000);                      // intermissionMs
            encoder.U8(4);
            for (var i = 0; i < 4; i++)
            {
                encoder.U16((ushort)(i + 1));
                var name = PlayerName(i);
                encoder.U8((byte)name.Length);
                encoder.Bytes(name);
                encoder.U8(1);                      // ready
                encoder.U8(0);                      // weapon
                encoder.U8(255);                    // hp
                encoder.U16((ushort)(i * 3));       // kills
                encoder.U8(12);                     // mag
                encoder.U16(60);                    // reserve
                encoder.U8(0);                      // reloadLeft
                encoder.U8(0);                      // rage
                encoder.U8(0);                      // rageLeft
                encoder.U8(0);                      // downed
                encoder.U8(0);                      // reviveRatio
            }
            encoder.End();
            header.Type = PacketType.MatchState;
            loop.OnPacket(header, payload);
        }

        // 昵称必须与 MatchStateCodec 的 MinNameBytes/MaxNameBytes（1..12）相容。
        private static string PlayerName(int index)
        {
            return index == 0 ? BenchLocalName : "bot" + (index + 1).ToString(CultureInfo.InvariantCulture);
        }

        // MatchState 载荷：u8 phase + u8 wave + u16 intermission + u8 count + 4 × (16 定长 + 昵称字节)。
        // 解码器要求 Remaining == 0（多余字节 = BadLength），所以缓冲必须**恰好**这么长。
        private static byte[] BuildMatchStatePayload()
        {
            var bytes = 5;
            for (var i = 0; i < 4; i++) bytes += MatchStateCodec.FixedRecordBytes + PlayerName(i).Length;
            return new byte[bytes];
        }

        // 每帧一发战斗事件（1 个 PlayerHit + 1 个 SheepKilled）：走 type=6 的真正收包路径，
        // 于是 Hud.PushEvent 与 GameLoop.EventApplied → PresentationLayer.OnEventApplied → Effects.SpawnHit 真的做功。
        // 载荷固定 33B：u32 tick + u8 count + PlayerHit 18B + SheepKilled 10B。
        private const int EventPayloadBytes = 33;

        private static void FeedEvents(GameLoop loop, PacketEncoder encoder, byte[] payload, ref PacketHeader header, ref uint eventId, uint tick)
        {
            encoder.Begin(payload);
            encoder.U32(tick);
            encoder.U8(2);                          // count
            encoder.U32(eventId++);                 // PlayerHit
            encoder.U8((byte)Ac.Net.EventType.PlayerHit);
            encoder.U16(1);                         // subject = 本地玩家
            encoder.U16(8);                         // target = 一只羊
            encoder.U16(40);                        // damage
            encoder.U8(1);                          // flags = headshot
            encoder.I16(0);
            encoder.I16(100);
            encoder.I16(300);
            encoder.U32(eventId++);                 // SheepKilled
            encoder.U8((byte)Ac.Net.EventType.SheepKilled);
            encoder.U16(12);                        // target
            encoder.U16(1);                         // subject
            encoder.U8(0);                          // kindFlags
            encoder.End();
            header.Type = PacketType.Event;
            loop.OnPacket(header, payload);
        }

        private static double MeasureFeedAlloc(GameLoop loop, ref SnapshotFrame frame, PacketEncoder encoder, byte[] eventPayload,
            byte[] matchPayload, ref PacketHeader header, ref uint snapshotTick, ref uint eventId, ref uint eventTick, int frames)
        {
            var start = AllocMeter.Begin();
            for (var i = 0; i < frames; i++)
            {
                snapshotTick += 1;
                FeedSnapshot(loop, ref frame, snapshotTick);
                FeedMatchState(loop, encoder, matchPayload, ref header, (uint)(i + 1));
                FeedEvents(loop, encoder, eventPayload, ref header, ref eventId, eventTick++);
            }
            string reason;
            var bytes = AllocMeter.BytesSince(start, out reason);
            return bytes < 0 ? -1.0 : bytes / (double)frames;
        }

        private static double MeasureWholeLoopAlloc(GameLoop loop, PresentationLayer pres, ref SnapshotFrame frame,
            PacketEncoder encoder, byte[] eventPayload, byte[] matchPayload, ref PacketHeader header,
            ref uint snapshotTick, ref uint eventId, ref uint eventTick, int frames)
        {
            var start = AllocMeter.Begin();
            for (var i = 0; i < frames; i++)
            {
                snapshotTick += 1;
                FeedSnapshot(loop, ref frame, snapshotTick);
                FeedMatchState(loop, encoder, matchPayload, ref header, (uint)(i + 1));
                FeedEvents(loop, encoder, eventPayload, ref header, ref eventId, eventTick++);
                loop.Frame(DtMs);
                pres.MainCamera.Render();
            }
            string reason;
            var bytes = AllocMeter.BytesSince(start, out reason);
            return bytes < 0 ? -1.0 : bytes / (double)frames;
        }

        // 计划场景内容：4 玩家（本地 pid 1 站在 (0,0,-8) 朝 +Z）+ 60 羊（36 grunt / 12 ram / 8 elite / 4 king）。
        // 羊群聚在相机前方 6m 处的环带（半径 2.5~6.5m）：全体落在视锥与屏幕占比阈值内，
        // 屏幕上的可见数才真的是 60（环带放到 9m 时近侧的羊会被上下视锥切掉，实测只剩 29 只）。
        private static void FillPlanFrame(ref SnapshotFrame frame, uint tick)
        {
            if (frame.Entities == null || frame.Entities.Length < SnapshotView.MaxRecordsPerFrame)
                frame.Entities = new FrameEntity[SnapshotView.MaxRecordsPerFrame];
            if (frame.RemovedIds == null) frame.RemovedIds = new ushort[SnapshotView.MaxRemovedPerFrame];
            frame.Tick = tick;
            frame.ServerTimeMs = tick * 50u;                        // 20Hz 服务端时钟（快照按帧注入，时钟仍按 50ms 走）
            frame.LastAckedSeq = (ushort)tick;
            frame.BaselineTick = 0u;                                // 每帧全量：差分帧会让站着不动的实体从最新帧里消失
            frame.EntityCount = 64;
            frame.RemovedCount = 0;
            var playerX = new short[] { 0, 600, -600, 0 };
            var playerZ = new short[] { -800, 600, 600, 1000 };
            for (var i = 0; i < 4; i++)
            {
                var e = default(FrameEntity);
                e.Id = (ushort)(i + 1);
                e.XCm = playerX[i];
                e.ZCm = playerZ[i];
                e.HpRatioUnits = 255;
                frame.Entities[i] = e;
            }
            for (var i = 0; i < 60; i++)
            {
                var angle = i * (2.0 * Math.PI / 60.0) + tick * 0.004;
                var radius = 2.5 + (i % 6) * 0.8;
                byte state;
                if (i < 36) state = (byte)SheepAnim.Idle;            // grunt
                else if (i < 48) state = (byte)SheepAnim.Run;        // ram
                else if (i < 56) state = (byte)SheepAnim.Attack;     // elite
                else state = (byte)SheepAnim.KingIdle;               // king
                var e = default(FrameEntity);
                e.Id = (ushort)(i + 5);
                e.KindFlags = PresentationLayer.WireKindSheep;
                e.XCm = (short)(Math.Cos(angle) * radius * 100.0);
                e.ZCm = (short)(Math.Sin(angle) * radius * 100.0 + 600.0);
                e.YawUnits = Quantize.QuantizeAngle(angle);
                e.HpRatioUnits = 255;
                e.State = state;
                frame.Entities[i + 4] = e;
            }
        }

        // 定长小端写入器：合成来件的字节写进**恰好长度**的载荷缓冲。
        // 每次 new List<byte>() 是基准自己的分配，会把"来件代价"量成"基准代价"；而多留一个字节，
        // MatchStateCodec/EventCodec 的 Remaining == 0 判定就会把整包判成 BadLength。
        private sealed class PacketEncoder
        {
            private byte[] _buffer;
            private int _length;
            internal void Begin(byte[] target) { _buffer = target; _length = 0; }
            // 长度不符必须炸出来：静默多写/少写都会变成"解码失败"而看不出原因。
            internal void End()
            {
                if (_length != _buffer.Length)
                    throw new InvalidOperationException("payload size mismatch: wrote " + _length + " of " + _buffer.Length);
            }
            private void Ensure(int extra)
            {
                if (_length + extra <= _buffer.Length) return;
                throw new InvalidOperationException("payload overflow: " + (_length + extra) + " > " + _buffer.Length);
            }
            internal void U8(byte value) { Ensure(1); _buffer[_length++] = value; }
            internal void U16(ushort value) { Ensure(2); _buffer[_length++] = (byte)(value & 0xFF); _buffer[_length++] = (byte)((value >> 8) & 0xFF); }
            internal void I16(short value) { U16(unchecked((ushort)value)); }
            internal void U32(uint value)
            {
                Ensure(4);
                _buffer[_length++] = (byte)(value & 0xFF); _buffer[_length++] = (byte)((value >> 8) & 0xFF);
                _buffer[_length++] = (byte)((value >> 16) & 0xFF); _buffer[_length++] = (byte)((value >> 24) & 0xFF);
            }
            internal void Bytes(string ascii)
            {
                Ensure(ascii.Length);
                for (var i = 0; i < ascii.Length; i++) _buffer[_length++] = (byte)ascii[i];
            }
        }

        // ---- 合成 CPU 负载（v1 路径，保留旧数字）----

        private static void RunSynthetic(string scene, string outPath, int warmup, int sample, int runs)
        {
            const int entityCount = 64;              // 4 玩家 + 60 羊

            var loop = new GameLoop(new SnapshotView(), new EntityViews(), new Hud(), new FrameProfiler());
            loop.LocalPlayerId = 1;
            var frame = default(SnapshotFrame);
            frame.Entities = new FrameEntity[SnapshotView.MaxRecordsPerFrame];

            for (var i = 0; i < warmup + sample; i++)
            {
                FillFrame(ref frame, entityCount, (uint)(i + 1));
                loop.ApplySnapshot(frame);
                loop.Frame(1000.0 / 60.0);
            }

            // 帧级分位用自己收的样本算（剖析器只有 240 帧窗口，装不下 600 帧的采样窗口）。
            var frameMs = new double[Math.Min(sample, loop.Frames)];
            var profiler = loop.Profiler;
            // 分配读数走 AllocMeter（引擎的 GC Allocated In Frame 计数器，逐字节精确）：
            // GC.GetAllocatedBytesForCurrentThread() 在本机恒为 0，报出来的 "0 B/帧" 是假绿。
            var allocBefore = AllocMeter.Begin();
            var gc0Before = GC.CollectionCount(0);
            var measureFrames = frameMs.Length;
            for (var i = 0; i < measureFrames; i++)
            {
                var t0 = NowMs();
                // tick 必须继续单调：回到旧 tick 会被镜像按"旧帧"整帧丢弃，测出来的就是空跑。
                FillFrame(ref frame, entityCount, (uint)(warmup + sample + i + 1));
                loop.ApplySnapshot(frame);
                loop.Frame(1000.0 / 60.0);
                frameMs[i] = NowMs() - t0;
            }
            string allocUnavailable;
            var allocBytes = AllocMeter.BytesSince(allocBefore, out allocUnavailable);
            // 测不到就是 -1（与图形指标同一套 fail-closed 语义），绝不当成"预算内"。
            var allocMeasured = allocBytes >= 0;
            var allocPerFrame = allocMeasured ? allocBytes / (double)(measureFrames == 0 ? 1 : measureFrames) : -1.0;
            var gc0Delta = GC.CollectionCount(0) - gc0Before;

            Array.Sort(frameMs);
            var p95 = Quantile(frameMs, 0.95);
            var p99 = Quantile(frameMs, 0.99);
            // 图形指标在无渲染装配时不可测：恒 -1（这条路径本来就不装配呈现层）。
            var drawCalls = -1;
            var triangles = -1;
            var particles = -1;
            var materials = -1;
            var graphicsMeasured = false;

            var stageMissing = 0;
            for (var i = 0; i < FrameProfiler.StageCount; i++) if (!(profiler.P95Ms(i) > 0f)) stageMissing++;

            var overBudget = p95 > FrameBudget.FrameP95BudgetMs || p99 > FrameBudget.FrameP99BudgetMs
                || (allocMeasured && allocPerFrame > FrameBudget.ManagedAllocBudgetBytes) || gc0Delta > FrameBudget.Gc0DeltaBudget;
            for (var i = 0; i < FrameProfiler.StageCount; i++) if (profiler.P95Ms(i) > FrameBudget.StageBudgetMs[i]) overBudget = true;

            var verdict = overBudget ? "FAIL" : (graphicsMeasured && stageMissing == 0 && allocMeasured) ? "PASS" : "UNVERIFIED";

            var sb = new StringBuilder();
            sb.Append("{\n");
            Meta(sb, "machine", Environment.MachineName);
            Meta(sb, "cpu", Safe(delegate { return SystemInfo.processorType; }, "unknown"));
            Meta(sb, "gpu", Safe(delegate { return SystemInfo.graphicsDeviceName; }, "none"));
            Meta(sb, "driver", Safe(delegate { return SystemInfo.graphicsDeviceVersion; }, "none"));
            Meta(sb, "unityVersion", Application.unityVersion);
            Meta(sb, "resolution", Screen.width > 0 ? Screen.width + "x" + Screen.height : "headless");
            Meta(sb, "qualityTier", Batching.QualityTier.ToString(CultureInfo.InvariantCulture));
            Meta(sb, "scene", scene);
            Meta(sb, "warmupFrames", warmup.ToString(CultureInfo.InvariantCulture));
            Meta(sb, "sampleFrames", measureFrames.ToString(CultureInfo.InvariantCulture));
            Meta(sb, "runs", runs.ToString(CultureInfo.InvariantCulture));
            Meta(sb, "commit", Safe(delegate { return GitCommit(); }, "unknown"));
            // 本入口喂的是**合成 CPU 负载**，不是 C14 §5 计划里的真实场景：场景种类必须自报，
            // 否则 ps1 会把合成数字当成计划场景的数字。
            Meta(sb, "sceneKind", "synthetic-cpu");
            Meta(sb, "allocMetric", allocMeasured ? AllocMeter.MarkerCategory + "/" + AllocMeter.MarkerName
                : "UNMEASURED (" + allocUnavailable + ")");
            Meta(sb, "verdict", verdict);
            Meta(sb, "verdictNote", "sceneKind=synthetic-cpu: this entry point drives the runtime GameLoop with a synthetic CPU load (64 entities), not the planned scene; "
                + "drawCalls/triangles/particles/materials need the presentation layer (reported -1 here) and fx/audio/draw/overlay are never marked, so this path cannot reach PASS; "
                + "managedAllocBytesPerFrame/gc0Delta come from the Ac.Tests.AllocMeter counter (GC.GetAllocatedBytesForCurrentThread is dead on this machine)");
            sb.Append("  \"budget\": {\n");
            Num(sb, 4, "frameP95Ms", FrameBudget.FrameP95BudgetMs);
            Num(sb, 4, "frameP99Ms", FrameBudget.FrameP99BudgetMs);
            Num(sb, 4, "managedAllocBytesPerFrame", FrameBudget.ManagedAllocBudgetBytes);
            Num(sb, 4, "gc0Delta", FrameBudget.Gc0DeltaBudget);
            Num(sb, 4, "drawCalls", FrameBudget.DrawCallBudget);
            Num(sb, 4, "triangles", FrameBudget.TriangleBudget);
            Num(sb, 4, "particles", FrameBudget.ParticleBudget);
            Num(sb, 4, "materials", FrameBudget.MaterialBudget);
            sb.Append("    \"stageP95Ms\": {\n");
            for (var i = 0; i < FrameProfiler.StageCount; i++) Num(sb, 6, FrameBudget.StageNames[i], FrameBudget.StageBudgetMs[i]);
            TrimLastComma(sb);
            sb.Append("    }\n");
            sb.Append("  },\n");
            Num(sb, "frameP95Ms", p95);
            Num(sb, "frameP99Ms", p99);
            Num(sb, "managedAllocBytesPerFrame", allocPerFrame);
            Num(sb, "gc0Delta", gc0Delta);
            Num(sb, "drawCalls", drawCalls);
            Num(sb, "triangles", triangles);
            Num(sb, "particles", particles);
            Num(sb, "materials", materials);
            // 只报**真的被 Mark 过**的段：这条路径没有呈现层，fx/audio/draw/overlay 永远不会被打点。
            sb.Append("  \"stageP95\": {\n");
            foreach (var stage in SyntheticMarkedStages) Num(sb, 4, FrameBudget.StageNames[(int)stage], profiler.P95Ms(stage));
            TrimLastComma(sb);
            sb.Append("  },\n");
            Num(sb, "frames", loop.Frames);
            Num(sb, "snapshotsApplied", loop.SnapshotsApplied);
            Num(sb, "bootFrames", loop.Frames);
            TrimLastComma(sb);
            sb.Append("\n}\n");

            WriteOut(outPath, sb.ToString());
            Console.Out.WriteLine("FRAMEBENCH " + verdict
                + " p95=" + p95.ToString("R") + " p99=" + p99.ToString("R")
                + " alloc=" + (allocMeasured ? allocPerFrame.ToString("R") : "UNMEASURED") + " gc0=" + gc0Delta
                + " frames=" + loop.Frames + " out=" + (outPath ?? "(none)"));
            Console.Out.Flush();
            // UNVERIFIED 也是红：拿不到数字就不许绿。
            EditorApplication.Exit(verdict == "PASS" ? 0 : 1);
        }

        // 合成路径真的会 Mark 的段（GameLoop 的内建装配）；其余四段没有装配，报 0 等于凭空给它们发"通过"。
        private static readonly FrameStage[] SyntheticMarkedStages =
        {
            FrameStage.Input, FrameStage.Sync, FrameStage.Predict, FrameStage.Hud,
        };

        private static void FillFrame(ref SnapshotFrame frame, int entityCount, uint tick)
        {
            if (frame.Entities == null || frame.Entities.Length < SnapshotView.MaxRecordsPerFrame)
                frame.Entities = new FrameEntity[SnapshotView.MaxRecordsPerFrame];
            if (frame.RemovedIds == null) frame.RemovedIds = new ushort[SnapshotView.MaxRemovedPerFrame];
            frame.Tick = tick;
            frame.ServerTimeMs = tick * 16u;                       // 60Hz 虚拟时钟
            frame.LastAckedSeq = (ushort)tick;
            frame.BaselineTick = (tick % 40u) == 0u ? 0u : tick - 1u;   // 每 40 tick 一次全量（S12 §5）
            frame.EntityCount = entityCount;
            frame.RemovedCount = 0;
            for (var i = 0; i < entityCount; i++)
            {
                var angle = (i * 6.283185307179586) / entityCount + tick * 0.01;
                var e = default(FrameEntity);
                e.Id = (ushort)(i + 1);
                e.XCm = (short)(Math.Cos(angle) * 2000.0);
                e.ZCm = (short)(Math.Sin(angle) * 2000.0);
                e.YCm = 0;
                e.YawUnits = (ushort)(((int)(angle * 10430.0)) & 0xFFFF);
                e.PitchUnits = 32768;
                e.HpRatioUnits = 255;
                // 前 4 个是玩家（kind 占低 2 位），其余是羊
                e.KindFlags = (byte)(i < 4 ? 0 : 1);
                frame.Entities[i] = e;
            }
        }

        private static double NowMs() { return (double)System.Diagnostics.Stopwatch.GetTimestamp() * 1000.0 / System.Diagnostics.Stopwatch.Frequency; }
        // 采样序列（未排序）里 [from, from+count) 的中位数：用来区分"稳态就这么慢"与"越过越慢"。
        private static double MedianOf(double[] values, int from, int count)
        {
            if (values.Length == 0 || count <= 0) return -1.0;
            if (from < 0) from = 0;
            if (from >= values.Length) return -1.0;
            if (from + count > values.Length) count = values.Length - from;
            var slice = new double[count];
            Array.Copy(values, from, slice, 0, count);
            Array.Sort(slice);
            return slice[count / 2];
        }
        // 8 段 P95 之和：与 frameP95Ms 对比就知道"没被分相计时的那部分"有多大。
        private static double StageSumP95(FrameProfiler profiler)
        {
            var sum = 0.0;
            for (var i = 0; i < FrameProfiler.StageCount; i++) sum += profiler.P95Ms(i);
            return sum;
        }
        // 把相机渲到指定目标 frames 次，返回中位耗时（ms）。用于分辨像素量 / 分辨率 / 固定开销。
        private static double MeasureRenderMs(PresentationLayer pres, RenderTexture target, int frames)
        {
            pres.MainCamera.targetTexture = target;
            var samples = new double[frames];
            for (var i = 0; i < frames; i++)
            {
                var t0 = NowMs();
                pres.MainCamera.Render();
                samples[i] = NowMs() - t0;
            }
            Array.Sort(samples);
            return samples[frames / 2];
        }
        private static int CountLiveRenderTextures()
        {
            var all = Resources.FindObjectsOfTypeAll<RenderTexture>();
            return all == null ? -1 : all.Length;
        }
        private static double DriverMemoryMb()
        {
            return UnityEngine.Profiling.Profiler.GetAllocatedMemoryForGraphicsDriver() / (1024.0 * 1024.0);
        }
        private static double Quantile(double[] sorted, double q)
        {
            if (sorted.Length == 0) return 0.0;
            // C14 §5 测量规则 3 冻结口径 = ceil(q*n)-1（FrameProfiler 与备份仓库 bench-sim.mjs 同口径）。
            var idx = (int)Math.Ceiling(q * sorted.Length) - 1;
            if (idx < 0) idx = 0;
            if (idx >= sorted.Length) idx = sorted.Length - 1;
            return sorted[idx];
        }
        private static string Arg(string[] args, string name, string fallback)
        {
            for (var i = 0; i < args.Length - 1; i++) if (args[i] == name) return args[i + 1];
            return fallback;
        }
        private static int ArgInt(string[] args, string name, int fallback)
        {
            int parsed;
            var raw = Arg(args, name, null);
            return raw != null && int.TryParse(raw, NumberStyles.Integer, CultureInfo.InvariantCulture, out parsed) ? parsed : fallback;
        }
        private static string Safe(Func<string> f, string fallback) { try { var s = f(); return string.IsNullOrEmpty(s) ? fallback : s; } catch { return fallback; } }
        // 从 .git 目录直接读 HEAD，避免在编辑器里起进程。
        private static string GitCommit()
        {
            var dir = new DirectoryInfo(Directory.GetCurrentDirectory());
            while (dir != null && !Directory.Exists(Path.Combine(dir.FullName, ".git"))) dir = dir.Parent;
            if (dir == null) return "unknown";
            var gitDir = Path.Combine(dir.FullName, ".git");
            var head = Path.Combine(gitDir, "HEAD");
            if (!File.Exists(head)) return "unknown";
            var text = File.ReadAllText(head).Trim();
            const string prefix = "ref: ";
            if (text.StartsWith(prefix, StringComparison.Ordinal))
            {
                var refFile = Path.Combine(gitDir, text.Substring(prefix.Length).Replace('/', Path.DirectorySeparatorChar));
                if (File.Exists(refFile)) return File.ReadAllText(refFile).Trim().Substring(0, 7);
                return "unknown";
            }
            return text.Length >= 7 ? text.Substring(0, 7) : text;
        }
        private static void Meta(StringBuilder sb, string key, string value) { Meta(sb, 2, key, value); }
        private static void Meta(StringBuilder sb, int indent, string key, string value)
        {
            sb.Append(' ', indent).Append("\"").Append(key).Append("\": \"").Append((value ?? "unknown").Replace("\\", "/").Replace("\"", "'")).Append("\",\n");
        }
        private static void Num(StringBuilder sb, string key, double value) { Num(sb, 2, key, value); }
        private static void Num(StringBuilder sb, int indent, string key, double value)
        {
            var safe = double.IsNaN(value) || double.IsInfinity(value) ? -1.0 : value;
            sb.Append(' ', indent).Append('"').Append(key).Append("\": ").Append(safe.ToString("R", CultureInfo.InvariantCulture)).Append(",\n");
        }
        // 每条 Num 都以 ",\n" 收尾；拼最后一项时要把它连同后面的缩进一起收掉（空对象时不动）。
        private static void TrimLastComma(StringBuilder sb)
        {
            var i = sb.Length - 1;
            while (i >= 0 && char.IsWhiteSpace(sb[i])) i--;
            if (i >= 0 && sb[i] == ',') { sb.Remove(i, sb.Length - i); sb.Append('\n'); }
        }
    }
}
