using System;
using Ac.Core;
using Ac.View;

namespace Ac.Tests
{
    // C14 §5：预算表、240 帧环、分位下标口径、分段累加、稳态零分配、超阈判定。
    public static class PerfSuite
    {
        public static void Register()
        {
            SelfTest.Add("perf.constants", ChecksConstants);
            SelfTest.Add("perf.ring_wrap", ChecksRingWrap);
            SelfTest.Add("perf.percentile_index", ChecksPercentileIndex);
            SelfTest.Add("perf.stage_accumulate", ChecksStageAccumulate);
            SelfTest.Add("perf.zero_alloc_steady", ChecksZeroAlloc);
            SelfTest.Add("perf.budget_flags", ChecksBudgetFlags);
            SelfTest.Add("perf.stage_names", ChecksStageNames);
            SelfTest.Add("perf.panel_budget_single_source", ChecksPanelBudgetSingleSource);
            SelfTest.Add("batch.script_budget_single_source", ChecksBenchScriptBudget);
            SelfTest.Add("batch.decide", ChecksBatchDecide);
            SelfTest.Add("batch.quality_tier", ChecksBatchQualityTier);
            SelfTest.Add("batch.cull", ChecksBatchCull);
            SelfTest.Add("batch.pool_reuse", ChecksPoolReuse);
            SelfTest.Add("batch.pool_zero_alloc", ChecksPoolZeroAlloc);
            SelfTest.Add("batch.frozen_api", ChecksFrozenApi);
        }

        // C14 标准轴必改项 1：面板的 20/33ms 告警阈值曾经是第二份硬编码。
        // C14 §9 冻结接口：SetQualityTier / DrawCallLimit / CullDistanceMeters(int kind)。
        // C14 标准轴必改项：ps1 里那张预算表是 FrameBudget 之外的第二份。这条用例直接读脚本，
        // 把两份钉在一起——改了一边不改另一边就红。
        private static void ChecksBenchScriptBudget()
        {
            // 编辑器进程的工作目录是工程目录（client/），相对路径会指错地方：两个候选都试。
            var path = System.IO.Path.Combine("client", "tools", "frame-bench.ps1");
            if (!System.IO.File.Exists(path)) path = System.IO.Path.Combine("..", "client", "tools", "frame-bench.ps1");
            if (!System.IO.File.Exists(path)) { SelfTest.True(false, "帧基准脚本必须存在", System.IO.Path.GetFullPath(path)); return; }
            var text = System.IO.File.ReadAllText(path);
            SelfTest.True(text.Contains("frameP95Ms = " + FrameBudget.FrameP95BudgetMs.ToString("R", System.Globalization.CultureInfo.InvariantCulture)), "ps1 的 P95 预算与 FrameBudget 同源", "不一致");
            SelfTest.True(text.Contains("frameP99Ms = " + FrameBudget.FrameP99BudgetMs.ToString("R", System.Globalization.CultureInfo.InvariantCulture)), "ps1 的 P99 预算与 FrameBudget 同源", "不一致");
            SelfTest.True(text.Contains("drawCalls = " + FrameBudget.DrawCallBudget), "ps1 的 drawCalls 预算与 FrameBudget 同源", "不一致");
            SelfTest.True(text.Contains("triangles = " + FrameBudget.TriangleBudget), "ps1 的 triangles 预算与 FrameBudget 同源", "不一致");
            SelfTest.True(text.Contains("particles = " + FrameBudget.ParticleBudget), "ps1 的 particles 预算与 FrameBudget 同源", "不一致");
            SelfTest.True(text.Contains("materials = " + FrameBudget.MaterialBudget), "ps1 的 materials 预算与 FrameBudget 同源", "不一致");
        }

        private static void ChecksFrozenApi()
        {
            Batching.SetQualityTier(2);
            SelfTest.Equal(2, (long)Batching.QualityTier);
            Batching.SetQualityTier(99);
            SelfTest.Equal(2, (long)Batching.QualityTier);            // 越界夹取到最高档
            Batching.SetQualityTier(-1);
            SelfTest.Equal(0, (long)Batching.QualityTier);            // 以及最低档

            SelfTest.True(Batching.DrawCallLimit == FrameBudget.DrawCallBudget, "DrawCallLimit 与预算表同源", Batching.DrawCallLimit.ToString());
            SelfTest.True(Batching.CullDistanceMeters(Batching.CullKindSheep) == 60f, "羊 60m", Batching.CullDistanceMeters(0).ToString("R"));
            SelfTest.True(Batching.CullDistanceMeters(Batching.CullKindArena) == 80f, "场地 80m", Batching.CullDistanceMeters(1).ToString("R"));
            // 恰好等于阈值必须仍在（边界不许被剔除）
            SelfTest.True(Batching.WithinCullDistanceMeters(60f, Batching.CullKindSheep), "恰等于阈值仍可见", "被剔除");
            SelfTest.True(!Batching.WithinCullDistanceMeters(60.01f, Batching.CullKindSheep), "超一点就剔除", "仍在");
            Batching.SetQualityTier(0);                               // 静态状态复位，避免污染后续用例
        }

        private static void ChecksPanelBudgetSingleSource()
        {
            SelfTest.True(Ac.UI.DebugPanel.P95WarnMs == FrameBudget.FrameP95BudgetMs, "面板 P95 阈值同源", Ac.UI.DebugPanel.P95WarnMs.ToString("R"));
            SelfTest.True(Ac.UI.DebugPanel.MaxWarnMs == FrameBudget.FrameP99BudgetMs, "面板 Max 阈值同源", Ac.UI.DebugPanel.MaxWarnMs.ToString("R"));
            SelfTest.True(FrameBudget.FrameP95BudgetMs == 20f && FrameBudget.FrameP99BudgetMs == 33f, "预算表本身是 20/33", FrameBudget.FrameP95BudgetMs.ToString("R"));
        }

        private static void ChecksConstants()
        {
            SelfTest.True(FrameBudget.FrameP95BudgetMs == 20.0f, "帧 P95 预算 20ms", FrameBudget.FrameP95BudgetMs.ToString("R"));
            SelfTest.True(FrameBudget.FrameP99BudgetMs == 33.0f, "帧 P99 预算 33ms", FrameBudget.FrameP99BudgetMs.ToString("R"));
            SelfTest.Equal(0, FrameBudget.ManagedAllocBudgetBytes);
            SelfTest.Equal(0, (long)FrameBudget.Gc0DeltaBudget);
            SelfTest.Equal(120, (long)FrameBudget.WarmupFrames);
            SelfTest.Equal(600, (long)FrameBudget.SampleFrames);
            SelfTest.Equal(3, (long)FrameBudget.Runs);
            SelfTest.Equal(120, (long)FrameBudget.DrawCallBudget);
            SelfTest.Equal(180000, (long)FrameBudget.TriangleBudget);
            SelfTest.Equal(256, (long)FrameBudget.ParticleBudget);
            SelfTest.Equal(24, (long)FrameBudget.MaterialBudget);
            SelfTest.Equal(8, (long)FrameBudget.StageBudgetMs.Length);
            SelfTest.True(FrameBudget.StageBudgetMs[0] == 0.5f && FrameBudget.StageBudgetMs[1] == 1.5f && FrameBudget.StageBudgetMs[2] == 1.0f, "input/sync/predict 预算", FrameBudget.StageBudgetMs[2].ToString("R"));
            SelfTest.True(FrameBudget.StageBudgetMs[3] == 4.0f && FrameBudget.StageBudgetMs[4] == 1.0f && FrameBudget.StageBudgetMs[5] == 10.0f, "fx/audio/draw 预算", FrameBudget.StageBudgetMs[5].ToString("R"));
            SelfTest.True(FrameBudget.StageBudgetMs[6] == 1.0f && FrameBudget.StageBudgetMs[7] == 1.0f, "overlay/hud 预算", FrameBudget.StageBudgetMs[7].ToString("R"));
            SelfTest.Equal(240, (long)FrameProfiler.WindowFrames);
            SelfTest.Equal(8, (long)FrameProfiler.StageCount);
        }

        private static void ChecksPercentileIndex()
        {
            // §5 测量方法 3：排序后取 ceil(q*n)-1
            SelfTest.Equal(227, (long)FrameProfiler.PercentileIndex(0.95, 240));
            SelfTest.Equal(237, (long)FrameProfiler.PercentileIndex(0.99, 240));
            SelfTest.Equal(0, (long)FrameProfiler.PercentileIndex(0.95, 1));
            SelfTest.Equal(-1, (long)FrameProfiler.PercentileIndex(0.95, 0));
            SelfTest.Equal(599, (long)FrameProfiler.PercentileIndex(1.0, 600));

            // 240 个样本：第 i 帧填 i+1 → P95 取第 228 小的值
            var profiler = new FrameProfiler();
            for (var i = 0; i < FrameProfiler.WindowFrames; i++)
            {
                profiler.Begin();
                profiler.End();
            }
            SelfTest.Equal(240, (long)profiler.FilledFrames);
            SelfTest.True(profiler.Steady(), "填满 240 帧算稳态", "没填满");
            // 上面那 240 帧是真实时钟填的，填不出规律值。要验证"排序后取哪个下标"这个口径，
            // 得有一条**已知**的时间序列：喂 235 帧 1ms + 最后 5 帧 1000ms，
            // 于是 P95（下标 227）落在 1ms 区、P99（下标 236）落在 1000ms 区。
            // 旧断言 P95 <= P99 在同一组样本上恒真，是自证式断言（C14 标准轴必改项）。
            var ticks = 0L;
            var synthetic = new FrameProfiler(delegate { return ticks; });
            var ms1000Ticks = (long)Math.Round(1000.0 * FrameProfiler.TicksPerMs);
            var ms1Ticks = (long)Math.Round(1.0 * FrameProfiler.TicksPerMs);
            for (var i = 0; i < FrameProfiler.WindowFrames; i++)
            {
                synthetic.Begin();
                ticks += i < FrameProfiler.WindowFrames - 5 ? ms1Ticks : ms1000Ticks;
                synthetic.End();
            }
            SelfTest.Equal(FrameProfiler.WindowFrames, (long)synthetic.FilledFrames);
            SelfTest.True(Math.Abs(synthetic.FrameP95Ms() - 1f) < 0.5f, "P95 落在 1ms 区", synthetic.FrameP95Ms().ToString("R"));
            SelfTest.True(Math.Abs(synthetic.FrameP99Ms() - 1000f) < 0.5f, "P99 落在 1000ms 区", synthetic.FrameP99Ms().ToString("R"));
        }

        private static void ChecksRingWrap()
        {
            var profiler = new FrameProfiler();
            for (var i = 0; i < FrameProfiler.WindowFrames + 60; i++) { profiler.Begin(); profiler.End(); }
            SelfTest.Equal(300, (long)profiler.TotalFrames);
            SelfTest.Equal(240, (long)profiler.FilledFrames);
            SelfTest.True(profiler.Cursor == 60, "环写指针回绕到 60", profiler.Cursor.ToString());
            // 回绕后再填 240 帧，仍是 240 帧填充、总帧数继续累加
            for (var i = 0; i < FrameProfiler.WindowFrames; i++) { profiler.Begin(); profiler.End(); }
            SelfTest.Equal(540, (long)profiler.TotalFrames);
            SelfTest.Equal(240, (long)profiler.FilledFrames);
            SelfTest.True(profiler.Cursor == 60, "再来一圈仍在 60", profiler.Cursor.ToString());
            profiler.Reset();
            SelfTest.Equal(0, (long)profiler.TotalFrames);
            SelfTest.Equal(0, (long)profiler.FilledFrames);
            SelfTest.True(!profiler.Steady(), "重置后不是稳态", "还是稳态");
            SelfTest.True(profiler.FrameP95Ms() == 0f, "空窗口分位为 0", profiler.FrameP95Ms().ToString("R"));
        }

        private static void ChecksStageAccumulate()
        {
            var profiler = new FrameProfiler();
            profiler.Begin();
            var spin = 0d;
            for (var i = 0; i < 20000; i++) spin += i * 0.5;
            profiler.Mark(FrameStage.Input);
            for (var i = 0; i < 20000; i++) spin += i * 0.5;
            profiler.Mark(FrameStage.Input);          // 同一段 Mark 两次必须累加
            profiler.End();
            SelfTest.True(spin > 0d, "自旋有结果（防止被优化掉）", spin.ToString("0"));
            var input = profiler.StageMs(FrameStage.Input);
            SelfTest.True(input > 0f, "input 段有耗时", input.ToString("R"));
            SelfTest.True(profiler.StageMs(FrameStage.Draw) == 0f, "没 Mark 的段是 0", profiler.StageMs(FrameStage.Draw).ToString("R"));
            SelfTest.True(profiler.LastFrameMs >= input, "帧内合计不小于单段", profiler.LastFrameMs.ToString("R"));
            SelfTest.True(profiler.LastFrameTotalMs > 0f, "帧间隔已记录", profiler.LastFrameTotalMs.ToString("R"));
            SelfTest.True(profiler.P95Ms(FrameStage.Input) > 0f, "分位能看到这一段", profiler.P95Ms(FrameStage.Input).ToString("R"));
            SelfTest.True(profiler.P95Ms(99) == 0f, "越界段号返回 0", "非 0");
            SelfTest.True(profiler.P95Ms(-1) == 0f, "负段号返回 0", "非 0");
        }

        private static void ChecksZeroAlloc()
        {
            var profiler = new FrameProfiler();
            for (var i = 0; i < FrameProfiler.WindowFrames * 2; i++) { profiler.Begin(); profiler.Mark(FrameStage.Input); profiler.End(); }
            var before = GC.GetAllocatedBytesForCurrentThread();
            for (var i = 0; i < 2000; i++)
            {
                profiler.Begin();
                profiler.Mark(FrameStage.Input);
                profiler.Mark(FrameStage.Sync);
                profiler.Mark(FrameStage.Predict);
                profiler.Mark(FrameStage.Fx);
                profiler.Mark(FrameStage.Audio);
                profiler.Mark(FrameStage.Draw);
                profiler.Mark(FrameStage.Overlay);
                profiler.Mark(FrameStage.Hud);
                profiler.End();
            }
            var after = GC.GetAllocatedBytesForCurrentThread();
            SelfTest.Equal(0, after - before);
            SelfTest.True(profiler.Steady(), "零分配跑完仍是稳态", "不是稳态");
            // 分位查询用预分配 scratch，也不分配
            var beforeQuery = GC.GetAllocatedBytesForCurrentThread();
            var sum = 0f;
            for (var i = 0; i < 200; i++) sum += profiler.P95Ms(i % 8) + profiler.P99Ms(i % 8) + profiler.FrameP95Ms();
            var afterQuery = GC.GetAllocatedBytesForCurrentThread();
            SelfTest.Equal(0, afterQuery - beforeQuery);
            SelfTest.True(sum >= 0f, "分位查询有结果", sum.ToString("R"));
        }

        private static void ChecksBudgetFlags()
        {
            var profiler = new FrameProfiler();
            // 造一段长耗时：draw 段预算 10ms，这里自旋到明显超阈（否则用例没有判别性）
            for (var i = 0; i < 40; i++)
            {
                profiler.Begin();
                var start = System.Diagnostics.Stopwatch.GetTimestamp();
                while ((System.Diagnostics.Stopwatch.GetTimestamp() - start) * 1000.0 / System.Diagnostics.Stopwatch.Frequency < 22.0) { }
                profiler.Mark(FrameStage.Draw);
                profiler.End();
            }
            var drawP95 = profiler.P95Ms(FrameStage.Draw);
            SelfTest.True(drawP95 > FrameBudget.StageBudgetMs[(int)FrameStage.Draw], "超阈的段能被判出来", drawP95.ToString("R"));
            SelfTest.True(profiler.StageOverBudget((int)FrameStage.Draw), "draw 判超阈", "没判出来");
            SelfTest.True(!profiler.StageOverBudget((int)FrameStage.Input), "没跑的段不判超阈", "误判");
            SelfTest.True(profiler.FrameOverBudget(), "帧 P95 超 20ms 也能判出来", profiler.FrameP95Ms().ToString("R"));
            SelfTest.True(!profiler.StageOverBudget(99) && !profiler.StageOverBudget(-1), "越界段号不判超阈", "误判");
            // 空窗口不判超阈：没有样本就不能说超了
            var empty = new FrameProfiler();
            SelfTest.True(!empty.FrameOverBudget(), "空窗口不判失败", "判失败");
            var summary = profiler.Summary();
            SelfTest.True(summary.IndexOf("frame p95=", StringComparison.Ordinal) >= 0, "Summary 有帧分位", summary);
            SelfTest.True(summary.IndexOf("draw=", StringComparison.Ordinal) >= 0 && summary.IndexOf('!') >= 0, "Summary 标出超阈段", summary);
        }

        private static void ChecksBatchDecide()
        {
            SelfTest.True(Batching.Decide(24) == DrawMode.Single, "24 只走单绘", Batching.Decide(24).ToString());
            SelfTest.True(Batching.Decide(25) == DrawMode.Instanced, "25 只走实例化（阈值边界）", Batching.Decide(25).ToString());
            SelfTest.True(Batching.Decide(0) == DrawMode.Single, "0 只走单绘", Batching.Decide(0).ToString());
            SelfTest.Equal(1023, (long)Batching.MaxInstancesPerBatch);
            SelfTest.True(!Batching.ShouldCombineStatic(7), "7 个静态 mesh 不合并", "合并了");
            SelfTest.True(Batching.ShouldCombineStatic(8), "8 个静态 mesh 合并", "没合并");
            // 1024 个实例：一批最多 1023 → 2 批（高画质档上限 1024 不额外收紧）
            SelfTest.Equal(2, (long)Batching.BatchCount(1024, 2));
            SelfTest.Equal(1, (long)Batching.BatchCount(1023, 2));
            SelfTest.Equal(0, (long)Batching.BatchCount(0, 2));
            SelfTest.Equal(5, (long)Batching.BatchCount(2500, 1));   // 中档上限 512 → 5 批
        }

        private static void ChecksBatchQualityTier()
        {
            SelfTest.True(Batching.ShadowDistanceFor(0) == 20f && Batching.ShadowDistanceFor(1) == 35f && Batching.ShadowDistanceFor(2) == 50f, "阴影距离 20/35/50", Batching.ShadowDistanceFor(2).ToString("R"));
            SelfTest.Equal(256, (long)Batching.VisibleInstances(4096, 0));
            SelfTest.Equal(512, (long)Batching.VisibleInstances(4096, 1));
            SelfTest.Equal(1024, (long)Batching.VisibleInstances(4096, 2));
            SelfTest.Equal(300, (long)Batching.VisibleInstances(300, 2));       // 没超上限就不动
            SelfTest.Equal(256, (long)Batching.VisibleInstances(300, -3));      // 越界档位按最低档
            SelfTest.Equal(1024, (long)Batching.VisibleInstances(4096, 99));    // 越界档位按最高档
            SelfTest.Equal(96, (long)Batching.ParticleCapFor(0));
            SelfTest.Equal(256, (long)Batching.ParticleCapFor(2));
            // 低画质档实例化上限 256 会把批次拆得更多（判别性：档位真的影响批数）
            SelfTest.Equal(4, (long)Batching.BatchCount(1024, 0));
            SelfTest.Equal(2, (long)Batching.BatchCount(1024, 2));
        }

        private static void ChecksBatchCull()
        {
            SelfTest.True(Batching.CullDistanceM(true) == 60f && Batching.CullDistanceM(false) == 80f, "剔除距离 羊 60 / 场地 80", Batching.CullDistanceM(true).ToString("R"));
            SelfTest.True(Batching.WithinCullDistance(59.9f, true), "59.9m 的羊在剔除距离内", "被判出界");
            SelfTest.True(Batching.WithinCullDistance(60f, true), "正好 60m 仍在距离内（含边界）", "被判出界");
            SelfTest.True(!Batching.WithinCullDistance(60.1f, true), "60.1m 的羊被剔除", "没剔除");
            SelfTest.True(Batching.WithinCullDistance(80f, false), "正好 80m 的场地仍在距离内", "被判出界");
            SelfTest.True(!Batching.WithinCullDistance(80.1f, false), "80.1m 的场地被剔除", "没剔除");
            SelfTest.True(!Batching.WithinCullDistance(float.NaN, true), "NaN 距离判为不可见", "判可见");
            SelfTest.True(!Batching.WithinCullDistance(-1f, false), "负距离判为不可见", "判可见");
        }

        private static void ChecksPoolReuse()
        {
            var pool = new InstancePool();
            SelfTest.Equal(64, (long)pool.Capacity);
            var a = pool.Rent(1001);
            var b = pool.Rent(1002);
            SelfTest.Equal(0, (long)a);
            SelfTest.Equal(1, (long)b);
            SelfTest.Equal(2, (long)pool.Live);
            SelfTest.Equal(2, (long)pool.RentCount);
            SelfTest.True(pool.Return(a), "回收第一个槽", "没回收");
            SelfTest.Equal(1, (long)pool.Live);
            SelfTest.Equal(0, (long)pool.EntityAt(a));            // 归还后句柄失效
            SelfTest.Equal(1002, (long)pool.EntityAt(b));         // 另一个句柄不受影响
            SelfTest.True(!pool.Return(5), "没在用的句柄回收被拒", "接受了");
            SelfTest.True(!pool.Return(a), "同一句柄不能回收两次", "接受了");
            SelfTest.True(!pool.Return(-1), "负槽位回收被拒", "接受了");
            // 容量翻倍且不丢数据
            for (var i = 0; i < 200; i++) pool.Rent(2000 + i);
            SelfTest.True(pool.Capacity >= 201, "容量按需翻倍", pool.Capacity.ToString());
            SelfTest.Equal(2, (long)pool.GrowthCount);                // 64 → 128 → 256
            SelfTest.Equal(1002, (long)pool.EntityAt(b));          // 借用中的句柄改成 1002
            SelfTest.Equal(2000, (long)pool.EntityAt(0));          // 空闲句柄被复用（原来 1001）
            SelfTest.True(pool.PeakLive >= 201, "峰值被记录", pool.PeakLive.ToString());
            // 旧断言 Pooled == Capacity - Live 就是属性定义本身，恒真。改成真的把池借干：
            // 借干后 Pooled 必须归零、Live 必须等于容量（顺便覆盖"满了会扩容再复用"）。
            for (var i = 0; i < 4096 && pool.Pooled > 0; i++) pool.Rent(3000 + i);
            SelfTest.Equal(0, (long)pool.Pooled);
            SelfTest.Equal((long)pool.Capacity, (long)pool.Live);
            pool.Clear();
            SelfTest.Equal(0, (long)pool.Live);
            SelfTest.True(pool.Capacity >= 201, "Clear 不缩容", pool.Capacity.ToString());
        }

        private static void ChecksPoolZeroAlloc()
        {
            var pool = new InstancePool();
            var slots = new int[64];
            for (var i = 0; i < 64; i++) slots[i] = pool.Rent(i);
            for (var i = 0; i < 64; i++) pool.Return(slots[i]);
            for (var warm = 0; warm < 8; warm++)                       // 先跑热：把 JIT/OSR 的首次分配排除在测量之外
            {
                for (var i = 0; i < 64; i++) slots[i] = pool.Rent(-1 - i);
                for (var i = 0; i < 64; i++) pool.Return(slots[i]);
            }
            var before = GC.GetAllocatedBytesForCurrentThread();
            for (var round = 0; round < 500; round++)
            {
                for (var i = 0; i < 64; i++) slots[i] = pool.Rent(round * 100 + i);
                for (var i = 0; i < 64; i++) pool.Return(slots[i]);
            }
            var after = GC.GetAllocatedBytesForCurrentThread();
            SelfTest.Equal(0, after - before);
            SelfTest.Equal(0, (long)pool.Live);
            SelfTest.Equal((long)pool.RentCount, (long)pool.ReturnCount);        // 借还配平
            SelfTest.True(pool.RentCount >= 32000, "测量区间确实跑了 500 轮", pool.RentCount.ToString());
            SelfTest.Equal(0, (long)pool.GrowthCount);
        }

        private static void ChecksStageNames()
        {
            var expected = new[] { "input", "sync", "predict", "fx", "audio", "draw", "overlay", "hud" };
            for (var i = 0; i < expected.Length; i++) SelfTest.True(FrameProfiler.StageName(i) == expected[i], "段名 " + i, FrameProfiler.StageName(i));
            SelfTest.True(FrameProfiler.StageName(8) == "unknown", "越界段名", FrameProfiler.StageName(8));
            SelfTest.Equal(8, (long)FrameBudget.StageNames.Length);
            SelfTest.Equal((int)FrameStage.Input, 0);
            SelfTest.Equal((int)FrameStage.Draw, 5);
            SelfTest.Equal((int)FrameStage.Hud, 7);
        }
    }
}
