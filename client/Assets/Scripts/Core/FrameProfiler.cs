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
        private readonly float[] _frame = new float[StageCount];
        private readonly float[] _lastFrame = new float[StageCount];

        // 分位排序是"被问到时才算"的惰性结果：8 段 + 帧级各一份排好序的副本，标记它对应哪一次 End。
        // 之前每次查询都重排 240 个样本，面板一帧问两次 P95/P99 就是两次 O(n log n) 排序——而面板
        // 关着的时候这次计算纯粹是白烧（PresentationLayer 现在连 Sample() 都不会调，这里是第二道闸）。
        private readonly float[][] _stageSorted = new float[StageCount][];
        private readonly float[] _frameSorted = new float[WindowFrames];
        private readonly int[] _stageSortedAt = new int[StageCount];
        private int _frameSortedAt;
        private const int NeverSorted = -1;

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

        public FrameProfiler() : this(null) { }

        public FrameProfiler(Func<long> ticksSource)
        {
            _ticksSource = ticksSource;
            for (var i = 0; i < StageCount; i++)
            {
                _stageSorted[i] = new float[WindowFrames];
                _stageSortedAt[i] = NeverSorted;
            }
            _frameSortedAt = NeverSorted;
        }

        private long NowTicks() { return _ticksSource == null ? Stopwatch.GetTimestamp() : _ticksSource(); }

        // 一毫秒对应多少个 tick。测试与 frame-bench 之流用它把"毫秒"翻译成时基刻度，
        // 免得在 Ac.Tests 里再碰一次 Stopwatch（那边没引 System.Diagnostics）。
        public static double TicksPerMs { get { return Stopwatch.Frequency / 1000.0; } }

        public void Reset()
        {
            Array.Clear(_samples, 0, _samples.Length);
            Array.Clear(_frameMs, 0, _frameMs.Length);
            for (var i = 0; i < StageCount; i++) { _frame[i] = 0f; _stageSortedAt[i] = NeverSorted; }
            _frameSortedAt = NeverSorted;
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

        // 排序次数（不是查询次数）：惰性缓存的判据——同一批样本重复问 P95/P99 只排一次，
        // 没人问就一次都不排。用例用它证明"面板关着时统计一次都没算"。
        public int SortsComputed { get; private set; }

        private float PercentileMs(int stageIndex, double q)
        {
            if (stageIndex < 0 || stageIndex >= StageCount) return 0f;
            var count = FilledFrames;
            if (count <= 0) return 0f;
            return SortedStage(stageIndex, count)[PercentileIndex(q, count)];
        }

        // §5 测量方法 3 的口径不变（排序后取 ceil(q*n)-1）；变的只是"排一次、缓存到下一次 End"。
        private float[] SortedStage(int stageIndex, int count)
        {
            var sorted = _stageSorted[stageIndex];
            if (_stageSortedAt[stageIndex] == TotalFrames) return sorted;
            var offset = stageIndex * WindowFrames;
            for (var i = 0; i < count; i++) sorted[i] = _samples[offset + i];
            SortAscending(sorted, count);
            _stageSortedAt[stageIndex] = TotalFrames;
            SortsComputed += 1;
            return sorted;
        }

        public float FrameP95Ms() { return FramePercentile(0.95); }
        public float FrameP99Ms() { return FramePercentile(0.99); }

        private float FramePercentile(double q)
        {
            var count = FilledFrames;
            if (count <= 0) return 0f;
            return SortedFrame(count)[PercentileIndex(q, count)];
        }

        private float[] SortedFrame(int count)
        {
            if (_frameSortedAt == TotalFrames) return _frameSorted;
            for (var i = 0; i < count; i++) _frameSorted[i] = _frameMs[i];
            SortAscending(_frameSorted, count);
            _frameSortedAt = TotalFrames;
            SortsComputed += 1;
            return _frameSorted;
        }

        // 有序副本用**自写的原地排序**，不用 Array.Sort：本机（Tuanjie 2022.3.62t16 / Mono）实测
        // Array.Sort(float[], int, int) 每次调用都分配 128 B（连排同一个数组也一样，8 次 = 1024 B）。
        // 面板开着时一次统计要把 8 段 + 帧级共 9 个副本重排 ⇒ 每次刷新 1152 B 的帧内分配，直接违反
        // C14 §5 的 0 B/帧——这条以前看不见：GC.GetAllocatedBytesForCurrentThread() 在本机恒为 0，
        // 而帧内分配计数器（GC Allocated In Frame）一看就露。比较语义与 float.CompareTo 对齐（NaN
        // 排最前），所以排出来的值序列与 Array.Sort 相同；分位口径（排序后取 ceil(q*n)-1）一个字没动。
        private static void SortAscending(float[] values, int count)
        {
            if (count > 1) SortRange(values, 0, count - 1);
        }

        // 小区间走插入排序，其余走 Hoare 分区；先递归小的一侧、大的一侧用循环续跑，
        // 递归深度被压在 log2(n) 以内。全在预分配数组上原地做，不分配。
        private static void SortRange(float[] a, int lo, int hi)
        {
            while (lo < hi)
            {
                if (hi - lo < 12) { InsertionSort(a, lo, hi); return; }
                var pivot = a[lo + (hi - lo) / 2];
                var i = lo;
                var j = hi;
                while (i <= j)
                {
                    while (Less(a[i], pivot)) i++;
                    while (Less(pivot, a[j])) j--;
                    if (i <= j) { var swap = a[i]; a[i] = a[j]; a[j] = swap; i++; j--; }
                }
                if (j - lo < hi - i)
                {
                    if (lo < j) SortRange(a, lo, j);
                    lo = i;
                }
                else
                {
                    if (i < hi) SortRange(a, i, hi);
                    hi = j;
                }
            }
        }

        private static void InsertionSort(float[] a, int lo, int hi)
        {
            for (var i = lo + 1; i <= hi; i++)
            {
                var value = a[i];
                var j = i - 1;
                while (j >= lo && Less(value, a[j])) { a[j + 1] = a[j]; j--; }
                a[j + 1] = value;
            }
        }

        // 与 float.CompareTo 同序：NaN 比任何数都小，±0 相等。
        private static bool Less(float x, float y)
        {
            if (x < y) return true;
            if (x > y) return false;
            if (x == y) return false;
            return float.IsNaN(x) && !float.IsNaN(y);
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
