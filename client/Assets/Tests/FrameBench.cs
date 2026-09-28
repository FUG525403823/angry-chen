using System;
using System.Globalization;
using System.IO;
using System.Text;
using Ac.Boot;
using Ac.Core;
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
    // 它驱动的是**运行期同一条** GameLoop（Ac.Boot），不是另写一套基准回路。
    public static class FrameBench
    {
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
            var scene = Arg(args, "-frameBenchScene", "bench-4p60sheep");
            var outPath = Arg(args, "-frameBenchOut", "");
            var warmup = ArgInt(args, "-frameBenchWarmup", FrameBudget.WarmupFrames);
            var sample = ArgInt(args, "-frameBenchSample", FrameBudget.SampleFrames);
            var runs = ArgInt(args, "-frameBenchRuns", FrameBudget.Runs);
            var tier = ArgInt(args, "-frameBenchQuality", -1);
            if (tier >= 0) Batching.SetQualityTier(tier);
            // 只认自己真的能造出来的负载；未知场景一律拒绝（原来的 if 两个分支同值是死代码）。
            if (scene == null || scene.IndexOf("4p60sheep", StringComparison.Ordinal) < 0)
            {
                Console.Out.WriteLine("ENV: scene '" + scene + "' cannot be built by this harness");
                Console.Out.Flush();
                EditorApplication.Exit(2);
                return;
            }
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
            // 图形指标在无图形设备时不可测：恒 -1（本入口就是这种环境）。
            var drawCalls = -1;
            var triangles = -1;
            var particles = -1;
            var materials = -1;
            var graphicsMeasured = drawCalls >= 0 && triangles >= 0 && particles >= 0 && materials >= 0;

            // 8 段里没有样本的段：没 Mark 过就是没测（P95 恒 0），不能当成"预算内"。
            var stageMissing = 0;
            for (var i = 0; i < FrameProfiler.StageCount; i++) if (!(profiler.P95Ms(i) > 0f)) stageMissing++;

            var overBudget = p95 > FrameBudget.FrameP95BudgetMs || p99 > FrameBudget.FrameP99BudgetMs
                || (allocMeasured && allocPerFrame > FrameBudget.ManagedAllocBudgetBytes) || gc0Delta > FrameBudget.Gc0DeltaBudget;
            for (var i = 0; i < FrameProfiler.StageCount; i++) if (profiler.P95Ms(i) > FrameBudget.StageBudgetMs[i]) overBudget = true;

            // 词表收敛成三值：PASS 只留给"图形指标齐 + 8 段齐 + 分配可测 + 全部已测指标未超预算"。
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
            // 分配数字的来源必须自报：谁读这张表都要知道它是哪个计数器量的。
            Meta(sb, "allocMetric", allocMeasured ? AllocMeter.MarkerCategory + "/" + AllocMeter.MarkerName
                : "UNMEASURED (" + allocUnavailable + ")");
            Meta(sb, "verdict", verdict);
            Meta(sb, "verdictNote", "sceneKind=synthetic-cpu: this entry point drives the runtime GameLoop with a synthetic CPU load (64 entities), not the planned scene; "
                + "drawCalls/triangles/particles/materials need a graphics device (reported -1 here) and fx/audio/draw/overlay are never marked, so this machine cannot reach PASS; "
                + "managedAllocBytesPerFrame/gc0Delta come from the Ac.Tests.AllocMeter counter (GC.GetAllocatedBytesForCurrentThread is dead on this machine)");
            // 预算表随样本一起落盘：唯一来源是 FrameBudget，ps1 只读这里的数字，不再另存一份。
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
            // 只报**真的被 Mark 过**的段：fx/audio/overlay/draw 目前没有任何装配（没有渲染器与音频场景），
            // 报 0 会让 8 段预算里的 4 段永远"通过"。缺段 ⇒ 本入口自己判 UNVERIFIED，这是刻意的。
            sb.Append("  \"stageP95\": {\n");
            foreach (var stage in MarkedStages) Num(sb, 4, FrameBudget.StageNames[(int)stage], profiler.P95Ms(stage));
            TrimLastComma(sb);
            sb.Append("  },\n");
            Num(sb, "frames", loop.Frames);
            Num(sb, "snapshotsApplied", loop.SnapshotsApplied);
            Num(sb, "bootFrames", loop.Frames);
            TrimLastComma(sb);
            sb.Append("\n}\n");

            if (!string.IsNullOrEmpty(outPath))
            {
                var dir = Path.GetDirectoryName(outPath);
                if (!string.IsNullOrEmpty(dir)) Directory.CreateDirectory(dir);
                File.WriteAllText(outPath, sb.ToString());
            }
            Console.Out.WriteLine("FRAMEBENCH " + verdict
                + " p95=" + p95.ToString("R") + " p99=" + p99.ToString("R")
                + " alloc=" + (allocMeasured ? allocPerFrame.ToString("R") : "UNMEASURED") + " gc0=" + gc0Delta
                + " frames=" + loop.Frames + " out=" + (outPath ?? "(none)"));
            Console.Out.Flush();   // Exit 会立刻终止进程，缓冲不刷就什么都没了
            // UNVERIFIED 也是红：拿不到数字就不许绿。
            EditorApplication.Exit(verdict == "PASS" ? 0 : 1);
        }

        // 这个基准真的会 Mark 的段（GameLoop 的装配）；其余四段没有装配，报 0 等于凭空给它们发"通过"。
        private static readonly FrameStage[] MarkedStages =
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
        private static double Quantile(double[] sorted, double q)
        {
            if (sorted.Length == 0) return 0.0;
            // C14 §5 冻结口径 = ceil(q*n)-1（FrameProfiler 用的就是它）。原先这里是 floor(q*(n-1))，
            // 等于另起一份口径，报告出来的数字不可跨次比较。
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
        private static void Meta(StringBuilder sb, string key, string value)
        {
            sb.Append("  \"").Append(key).Append("\": \"").Append((value ?? "unknown").Replace("\\", "/").Replace("\"", "'")).Append("\",\n");
        }
        private static void Num(StringBuilder sb, string key, double value) { Num(sb, 2, key, value); }
        private static void Num(StringBuilder sb, int indent, string key, double value)
        {
            sb.Append(' ', indent).Append('"').Append(key).Append("\": ").Append(value.ToString("R", CultureInfo.InvariantCulture)).Append(",\n");
        }
        // 每条 Num 都以 ",\n" 收尾；拼最后一项时要把它收掉（空对象时不动）。
        private static void TrimLastComma(StringBuilder sb)
        {
            if (sb.Length >= 2 && sb[sb.Length - 2] == ',') sb.Remove(sb.Length - 2, 2);
        }
    }
}
