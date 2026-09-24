namespace Ac.UI
{
    // C10 §5(b)：数值类 ≤10Hz 节流。窗口由调用方给的 Tick 推进；脏检查由调用方按显示精度取整后比较。
    public sealed class UiThrottle
    {
        public const float NumericRefreshMs = 100f;
        public const float StatsRefreshMs = 250f;

        private float _accumMs = NumericRefreshMs;
        private float _statsAccumMs = StatsRefreshMs;
        private int _last = int.MinValue;
        private int _statsLast = int.MinValue;

        public int NumericWrites { get; private set; }
        public int StatsWrites { get; private set; }
        public int EventWrites { get; private set; }
        public int SkippedWrites { get; private set; }

        public void Tick(float dtMs) { _accumMs += dtMs; _statsAccumMs += dtMs; }

        public bool ShouldWrite(int roundedValue)
        {
            if (_accumMs < NumericRefreshMs) { SkippedWrites += 1; return false; }
            _accumMs = 0f;
            if (roundedValue == _last) { SkippedWrites += 1; return false; }
            _last = roundedValue;
            NumericWrites += 1;
            return true;
        }

        // C10 §9 / §5(b)：统计行是**独立**的 250ms 窗口，不复用数值窗口 ——
        // 共用一份累加器时，统计行的 250ms 会把数值类的 100ms 窗口一起吃掉。
        public bool ShouldWriteStats(int roundedValue)
        {
            if (_statsAccumMs < StatsRefreshMs) { SkippedWrites += 1; return false; }
            _statsAccumMs = 0f;
            if (roundedValue == _statsLast) { SkippedWrites += 1; return false; }
            _statsLast = roundedValue;
            StatsWrites += 1;
            return true;
        }

        // 状态类事件立即刷新：只记事件写，不动数值窗口（不然事件一密数值就永远刷不到）
        public void NoteEventWrite() { EventWrites += 1; }
    }
}
