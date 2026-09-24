using System;
using System.Diagnostics;

namespace Ac.Core
{
    public enum FrameStage : byte
    {
        Input = 0, Sync = 1, Predict = 2, Fx = 3, Audio = 4, Draw = 5, Overlay = 6, Hud = 7,
    }

    // C14 §5 预算表（冻结）
    public static class FrameBudget
    {
        public const float FrameP95BudgetMs = 20.0f;
        public const float FrameP99BudgetMs = 33.0f;
        public const long ManagedAllocBudgetBytes = 0;
        public const int Gc0DeltaBudget = 0;
        public const int WarmupFrames = 120;
        public const int SampleFrames = 600;
        public const int Runs = 3;
        public const int DrawCallBudget = 120;
        public const int TriangleBudget = 180000;
        public const int ParticleBudget = 256;
        public const int MaterialBudget = 24;

        // 下标即 FrameStage
        public static readonly float[] StageBudgetMs = { 0.5f, 1.5f, 1.0f, 4.0f, 1.0f, 10.0f, 1.0f, 1.0f };

        public static readonly string[] StageNames =
        {
            "input", "sync", "predict", "fx", "audio", "draw", "overlay", "hud",
        };
    }

    // C14 §4 任务 1：Begin/Mark/End 三段式、8 段各一条 240 帧环、P95/P99、Summary()。
    // 热路径零分配：Begin/Mark/End 只写预分配的数组，不做字符串拼接、不装箱、不产生闭包。
    public sealed class FrameProfiler
    {
        public const int StageCount = 8;
        public const int WindowFrames = 240;
        public const int DefaultStageIndex = (int)FrameStage.Hud;

        private readonly float[] _samples = new float[StageCount * WindowFrames];   // 列主序：stage * WindowFrames + frame
        private readonly float[] _frameMs = new float[WindowFrames];
        private readonly float[] _scratch = new float[WindowFrames];
        private readonly float[] _frame = new float[StageCount];
        private readonly float[] _lastFrame = new float[StageCount];

        private long _frameStartTicks;
        private long _markTicks;

        public int Cursor { get; private set; }
        public int FilledFrames { get; private set; }
        public int TotalFrames { get; private set; }
        public float LastFrameMs { get; private set; }
        public float LastFrameTotalMs { get; private set; }

        private static readonly double MsPerTick = 1000.0 / Stopwatch.Frequency;

        // 时基缝：默认走真实 Stopwatch；测试可以喂一条**已知**的时间序列，否则分位口径
        // （排序后取哪个下标）根本没有办法被验证——C14 标准轴说的"P95 <= P99 恒真"就是这么来的。
        private readonly Func<long> _ticksSource;

        public FrameProfiler() { }
        public FrameProfiler(Func<long> ticksSource) { _ticksSource = ticksSource; }

        private long NowTicks() { return _ticksSource == null ? Stopwatch.GetTimestamp() : _ticksSource(); }

        // 一毫秒对应多少个 tick。测试与 frame-bench 之流用它把"毫秒"翻译成时基刻度，
        // 免得在 Ac.Tests 里再碰一次 Stopwatch（那边没引 System.Diagnostics）。
        public static double TicksPerMs { get { return Stopwatch.Frequency / 1000.0; } }

        public void Reset()
        {
            Array.Clear(_samples, 0, _samples.Length);
            Array.Clear(_frameMs, 0, _frameMs.Length);
            for (var i = 0; i < StageCount; i++) _frame[i] = 0f;
            Cursor = 0;
            FilledFrames = 0;
            TotalFrames = 0;
            LastFrameMs = 0f;
            LastFrameTotalMs = 0f;
        }

        public void Begin()
        {
            for (var i = 0; i < StageCount; i++) _frame[i] = 0f;
            _markTicks = NowTicks();
            _frameStartTicks = _markTicks;
        }

        // 把"距离上一次 Mark（或 Begin）"的时间累加到该段：同一段可以 Mark 多次
        public void Mark(FrameStage stage)
        {
            var now = NowTicks();
            _frame[(int)stage] += (float)((now - _markTicks) * MsPerTick);
            _markTicks = now;
        }

        public void End()
        {
            var now = NowTicks();
            LastFrameTotalMs = (float)((now - _frameStartTicks) * MsPerTick);
            LastFrameMs = 0f;
            var slot = Cursor;
            for (var i = 0; i < StageCount; i++)
            {
                _samples[i * WindowFrames + slot] = _frame[i];
                _lastFrame[i] = _frame[i];
                LastFrameMs += _frame[i];
            }
            _frameMs[slot] = LastFrameTotalMs;
            Cursor = slot + 1 >= WindowFrames ? 0 : slot + 1;
            if (FilledFrames < WindowFrames) FilledFrames += 1;
            TotalFrames += 1;
        }

        public float StageMs(FrameStage stage) { return _lastFrame[(int)stage]; }

        // §5 测量方法 3：排序后取下标 ceil(0.95 * n) - 1
        public static int PercentileIndex(double q, int count)
        {
            if (count <= 0) return -1;
            var index = (int)Math.Ceiling(q * count) - 1;
            if (index < 0) index = 0;
            if (index >= count) index = count - 1;
            return index;
        }

        public float P95Ms(int stageIndex) { return PercentileMs(stageIndex, 0.95); }
        public float P99Ms(int stageIndex) { return PercentileMs(stageIndex, 0.99); }
        public float P95Ms(FrameStage stage) { return PercentileMs((int)stage, 0.95); }
        public float P99Ms(FrameStage stage) { return PercentileMs((int)stage, 0.99); }

        private float PercentileMs(int stageIndex, double q)
        {
            if (stageIndex < 0 || stageIndex >= StageCount) return 0f;
            var count = FilledFrames;
            if (count <= 0) return 0f;
            var offset = stageIndex * WindowFrames;
            for (var i = 0; i < count; i++) _scratch[i] = _samples[offset + i];
            Array.Sort(_scratch, 0, count);
            return _scratch[PercentileIndex(q, count)];
        }

        public float FrameP95Ms() { return FramePercentile(0.95); }
        public float FrameP99Ms() { return FramePercentile(0.99); }

        private float FramePercentile(double q)
        {
            var count = FilledFrames;
            if (count <= 0) return 0f;
            for (var i = 0; i < count; i++) _scratch[i] = _frameMs[i];
            Array.Sort(_scratch, 0, count);
            return _scratch[PercentileIndex(q, count)];
        }

        public bool StageOverBudget(int stageIndex)
        {
            if (stageIndex < 0 || stageIndex >= StageCount) return false;
            return P95Ms(stageIndex) > FrameBudget.StageBudgetMs[stageIndex];
        }

        public bool FrameOverBudget()
        {
            return FilledFrames > 0 && (FrameP95Ms() > FrameBudget.FrameP95BudgetMs || FrameP99Ms() > FrameBudget.FrameP99BudgetMs);
        }

        public bool Steady() { return FilledFrames >= WindowFrames; }

        // 不在热路径上，允许分配
        public string Summary()
        {
            var text = new System.Text.StringBuilder(256);
            text.Append("frames=").Append(TotalFrames).Append(" filled=").Append(FilledFrames);
            text.Append(" frame p95=").Append(FrameP95Ms().ToString("0.00")).Append("ms p99=").Append(FrameP99Ms().ToString("0.00")).Append("ms");
            for (var i = 0; i < StageCount; i++)
            {
                text.Append(' ').Append(FrameBudget.StageNames[i]).Append('=').Append(P95Ms(i).ToString("0.00"));
                if (StageOverBudget(i)) text.Append('!');
            }
            return text.ToString();
        }

        public static string StageName(int stageIndex)
        {
            if (stageIndex < 0 || stageIndex >= StageCount) return "unknown";
            return FrameBudget.StageNames[stageIndex];
        }
    }
}
