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

        // ---- C14 §5 计划场景（editor 机制 = 诊断口径）----
        //
        // 测量核心全在 Ac.Boot.PlanBench（运行期程序集；player 机制引用的是同一份）：装配走 GameBootstrap /
        // PresentationLayer，合成来件走 ApplySnapshot/OnPacket，统计与 JSON 也是同一套。这里只做三件事：
        //   ① 以 editor 机制驱动它（ManualRender = true：-batchmode 下引擎不做任何渲染，必须手动 Camera.Render()）；
        //   ② 补只有 editor 机制才有的对照实验（手动渲染的地板定标 / 内建管线对照 / 驱动显存）；
        //   ③ 退出码：exit 0 = PASS，1 = FAIL，2 = 环境不可用。
        private static void RunPlanScene(string scene, string outPath, int warmup, int sample, int runs)
        {
            var options = FrameBenchPlanOptions.FromArgs(Environment.GetCommandLineArgs(), PlanBench.MechanismEditor);
            options.Scene = scene;
            options.OutPath = outPath;
            options.Warmup = warmup;
            options.Sample = sample;
            options.Runs = runs;
            var bench = new PlanBench(options);
            bench.ManualRender = true;                  // editor 机制：batchmode 引擎不渲染，只能手动渲
            if (!bench.Prepare())
            {
                Console.Out.WriteLine("FRAMEBENCH ENV mechanism=" + PlanBench.MechanismEditor + " " + bench.EnvError);
                Console.Out.Flush();
                EditorApplication.Exit(2);
                return;
            }

            // 同步循环：一次 Tick = 合成来件 + GameLoop.Frame(16.67ms) + 手动 Camera.Render()。
            while (!bench.SampleComplete) bench.Tick();

            // ⑤ 渲染证据：真渲到一张小 RT 上读回像素。读回不可用时记 -1（不拿它判 PASS，但覆盖率为 0
            //    说明这一帧什么都没画出来 —— 那时图形指标一律记 -1，判 UNVERIFIED）。
            try
            {
                bench.PixelCoverage = MeasurePixelCoverage(bench.Camera, 480, 270);
                bench.PixelNote = "480x270 RenderTexture readback, pixels differing from the top-left pixel by >24";
            }
            catch (Exception ex)
            {
                bench.PixelCoverage = -1.0;
                bench.PixelNote = "readback failed: " + ex.GetType().Name;
            }

            // ⑧⑨ 渲染成本定标 + 对照组（只有"手动渲染"这条机制才需要）：同一相机在"复用 1920x1080 /
            //    640x480 / 全新 1920x1080"三种目标上各渲 DiagnosticsFrames 帧取中位；再报 RenderTexture
            //    活跃数与驱动侧显存（排除"越跑越漏"）。空场与内建管线两个对照组说明这块地板既不是像素量
            //    也不是分辨率，而是"编辑器独立渲染这条路本身"。
            var pres = bench.Presentation;
            var renderTarget = bench.RenderTarget;
            var rtCountBefore = BenchJson.LiveRenderTextureCount();
            var driverMemBefore = BenchJson.DriverMemoryMb();
            var bigReuseMs = MeasureRenderMs(pres.MainCamera, renderTarget, DiagnosticsFrames);
            var smallRt = new RenderTexture(640, 480, 24, RenderTextureFormat.ARGB32);
            smallRt.Create();
            var smallMs = MeasureRenderMs(pres.MainCamera, smallRt, DiagnosticsFrames);
            var freshRt = new RenderTexture(1920, 1080, 24, RenderTextureFormat.ARGB32);
            freshRt.Create();
            var freshMs = MeasureRenderMs(pres.MainCamera, freshRt, DiagnosticsFrames);
            pres.MainCamera.targetTexture = renderTarget;
            var rootWasActive = pres.Root == null || pres.Root.activeSelf;
            if (pres.Root != null) pres.Root.SetActive(false);
            var emptyMs = MeasureRenderMs(pres.MainCamera, renderTarget, DiagnosticsFrames);
            if (pres.Root != null) pres.Root.SetActive(rootWasActive);
            var urpAsset = UnityEngine.Rendering.GraphicsSettings.renderPipelineAsset;
            UnityEngine.Rendering.GraphicsSettings.renderPipelineAsset = null;
            var builtinMs = MeasureRenderMs(pres.MainCamera, renderTarget, DiagnosticsFrames);
            UnityEngine.Rendering.GraphicsSettings.renderPipelineAsset = urpAsset;
            var urpAgainMs = MeasureRenderMs(pres.MainCamera, renderTarget, DiagnosticsFrames);
            var rtCountAfter = BenchJson.LiveRenderTextureCount();
            var driverMemAfter = BenchJson.DriverMemoryMb();
            smallRt.Release();
            freshRt.Release();

            bench.ExtraJson.Add(delegate(StringBuilder sb)
            {
                sb.Append("  \"render\": {\n");
                BenchJson.Num(sb, 4, "vSyncBefore", bench.VSyncBefore);
                BenchJson.Num(sb, 4, "vSyncDuring", 0);
                BenchJson.Num(sb, 4, "targetFrameRate", -1);
                BenchJson.Num(sb, 4, "bigReuseMedianMs", bigReuseMs);
                BenchJson.Num(sb, 4, "small640MedianMs", smallMs);
                BenchJson.Num(sb, 4, "freshBigMedianMs", freshMs);
                BenchJson.Num(sb, 4, "emptySceneUrpMedianMs", emptyMs);
                BenchJson.Num(sb, 4, "builtinPipelineMedianMs", builtinMs);
                BenchJson.Num(sb, 4, "urpAgainMedianMs", urpAgainMs);
                BenchJson.Num(sb, 4, "rtCountBefore", rtCountBefore);
                BenchJson.Num(sb, 4, "rtCountAfter", rtCountAfter);
                BenchJson.Num(sb, 4, "driverMemBeforeMb", driverMemBefore);
                BenchJson.Num(sb, 4, "driverMemAfterMb", driverMemAfter);
                BenchJson.Num(sb, 4, "driverMemDeltaMb", driverMemAfter - driverMemBefore);
                BenchJson.Num(sb, 4, "diagnosticFrames", DiagnosticsFrames);
                BenchJson.TrimLastComma(sb);
                sb.Append("  },\n");
            });

            bench.Finish();
            EditorApplication.Exit(bench.ExitCode);
        }

        // 渲染证据：渲到一张小 RT 上读回像素（计数口径与 player 机制的窗口截图完全一致）。
        private static double MeasurePixelCoverage(Camera camera, int width, int height)
        {
            var rt = RenderTexture.GetTemporary(width, height, 24, RenderTextureFormat.ARGB32);
            var previous = camera.targetTexture;
            camera.targetTexture = rt;
            camera.Render();
            RenderTexture.active = rt;
            var texture = new Texture2D(width, height, TextureFormat.RGBA32, false);
            texture.ReadPixels(new Rect(0, 0, width, height), 0, 0, false);
            texture.Apply(false);
            RenderTexture.active = null;
            camera.targetTexture = previous;
            var pixels = texture.GetPixels32();
            UnityEngine.Object.DestroyImmediate(texture);
            RenderTexture.ReleaseTemporary(rt);
            return BenchJson.PixelCoverage(pixels);
        }

        private static bool locationWarn(bool bad) { return bad; }

        private static void WriteOut(string outPath, string json) { BenchJson.WriteOut(outPath, json); }


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

        private static double NowMs() { return BenchJson.NowMs(); }
        // 把相机渲到指定目标 frames 次，返回中位耗时（ms）。用于分辨像素量 / 分辨率 / 固定开销。
        private static double MeasureRenderMs(Camera camera, RenderTexture target, int frames)
        {
            camera.targetTexture = target;
            var samples = new double[frames];
            for (var i = 0; i < frames; i++)
            {
                var t0 = NowMs();
                camera.Render();
                samples[i] = NowMs() - t0;
            }
            Array.Sort(samples);
            return samples[frames / 2];
        }
        // 下面这些小工具与 Ac.Boot.BenchJson 是同一份实现（计划场景基准两条机制共用），这里只做转发。
        private static double Quantile(double[] sorted, double q) { return BenchJson.Quantile(sorted, q); }
        private static string Arg(string[] args, string name, string fallback) { return BenchJson.Arg(args, name, fallback); }
        private static int ArgInt(string[] args, string name, int fallback) { return BenchJson.ArgInt(args, name, fallback); }
        private static string Safe(Func<string> f, string fallback) { return BenchJson.Safe(f, fallback); }
        private static string GitCommit() { return BenchJson.GitCommit(); }
        private static void Meta(StringBuilder sb, string key, string value) { BenchJson.Meta(sb, key, value); }
        private static void Meta(StringBuilder sb, int indent, string key, string value) { BenchJson.Meta(sb, indent, key, value); }
        private static void Num(StringBuilder sb, string key, double value) { BenchJson.Num(sb, key, value); }
        private static void Num(StringBuilder sb, int indent, string key, double value) { BenchJson.Num(sb, indent, key, value); }
        private static void TrimLastComma(StringBuilder sb) { BenchJson.TrimLastComma(sb); }
    }
}
