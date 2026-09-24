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
            var warmup = ArgInt(args, "-frameBenchWarmup", 120);
            var sample = ArgInt(args, "-frameBenchSample", 600);
            var tier = ArgInt(args, "-frameBenchQuality", -1);
            if (tier >= 0) Batching.SetQualityTier(tier);
            var entityCount = 64;                    // 4 玩家 + 60 羊
            if (scene != null && scene.IndexOf("4p60sheep", StringComparison.Ordinal) < 0) entityCount = 64;

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
            var allocBefore = GC.GetAllocatedBytesForCurrentThread();
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
            var allocPerFrame = (GC.GetAllocatedBytesForCurrentThread() - allocBefore) / (measureFrames == 0 ? 1 : measureFrames);
            var gc0Delta = GC.CollectionCount(0) - gc0Before;

            Array.Sort(frameMs);
            var p95 = Quantile(frameMs, 0.95);
            var p99 = Quantile(frameMs, 0.99);
            var cpuPass = p95 <= FrameBudget.FrameP95BudgetMs && p99 <= FrameBudget.FrameP99BudgetMs && allocPerFrame == 0 && gc0Delta == 0;

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
            Meta(sb, "runs", "1");
            Meta(sb, "commit", Safe(delegate { return GitCommit(); }, "unknown"));
            Meta(sb, "verdict", cpuPass ? "cpu-pass" : "fail");
Meta(sb, "verdictNote", "headless: CPU frame path only (sync/predict/view/HUD); drawCalls/triangles/particles/materials need a graphics device, reported as -1 and judged FAIL");
            Num(sb, "frameP95Ms", p95);
            Num(sb, "frameP99Ms", p99);
            Num(sb, "managedAllocBytesPerFrame", allocPerFrame);
            Num(sb, "gc0Delta", gc0Delta);
            Num(sb, "drawCalls", -1);
            Num(sb, "triangles", -1);
            Num(sb, "particles", -1);
            Num(sb, "materials", -1);
            // 只报**真的被 Mark 过**的段：fx/audio/overlay/draw 目前没有任何装配（没有渲染器与音频场景），
            // 报 0 会让 8 段预算里的 4 段永远"通过"。缺段 ⇒ ps1 直接判 FAIL，这是刻意的。
            sb.Append("  \"stageP95\": {\n");
            Stage(sb, "input", profiler, FrameStage.Input);
            Stage(sb, "sync", profiler, FrameStage.Sync);
            Stage(sb, "predict", profiler, FrameStage.Predict);
            Stage(sb, "hud", profiler, FrameStage.Hud);
            if (sb[sb.Length - 2] == ',') sb.Remove(sb.Length - 2, 2);
            sb.Append("  },\n");
            Num(sb, "frames", loop.Frames);
            Num(sb, "snapshotsApplied", loop.SnapshotsApplied);
            Num(sb, "bootFrames", loop.Frames);
            if (sb[sb.Length - 2] == ',') sb.Remove(sb.Length - 2, 2);
            sb.Append("\n}\n");

            if (!string.IsNullOrEmpty(outPath))
            {
                var dir = Path.GetDirectoryName(outPath);
                if (!string.IsNullOrEmpty(dir)) Directory.CreateDirectory(dir);
                File.WriteAllText(outPath, sb.ToString());
            }
            Console.Out.WriteLine("FRAMEBENCH " + (cpuPass ? "PASS" : "FAIL")
                + " p95=" + p95.ToString("R") + " p99=" + p99.ToString("R")
                + " alloc=" + allocPerFrame + " gc0=" + gc0Delta
                + " frames=" + loop.Frames + " out=" + (outPath ?? "(none)"));
            Console.Out.Flush();   // Exit 会立刻终止进程，缓冲不刷就什么都没了
            EditorApplication.Exit(cpuPass ? 0 : 1);
        }

        private static void Stage(StringBuilder sb, string name, FrameProfiler profiler, FrameStage stage)
        {
            Num(sb, name, profiler.P95Ms(stage));
        }

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
        private static void Num(StringBuilder sb, string key, double value)
        {
            sb.Append("  \"").Append(key).Append("\": ").Append(value.ToString("R", CultureInfo.InvariantCulture)).Append(",\n");
        }
    }
}
