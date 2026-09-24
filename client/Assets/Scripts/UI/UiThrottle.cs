namespace Ac.UI
{
    // C10 §5(b)：数值类 ≤10Hz 节流。窗口由调用方给的 Tick 推进；脏检查由调用方按显示精度取整后比较。
    public sealed class UiThrottle
    {
        public const float NumericRefreshMs = 100f;
        public const float StatsRefreshMs = 250f;

        private float _accumMs = NumericRefreshMs;
        private int _last = int.MinValue;

        public int NumericWrites { get; private set; }
        public int EventWrites { get; private set; }
        public int SkippedWrites { get; private set; }

        public void Tick(float dtMs) { _accumMs += dtMs; }

        public bool ShouldWrite(int roundedValue)
        {
            if (_accumMs < NumericRefreshMs) { SkippedWrites += 1; return false; }
            _accumMs = 0f;
            if (roundedValue == _last) { SkippedWrites += 1; return false; }
            _last = roundedValue;
            NumericWrites += 1;
            return true;
        }

        // 状态类事件立即刷新：只记事件写，不动数值窗口（不然事件一密数值就永远刷不到）
        public void NoteEventWrite() { EventWrites += 1; }
    }
}
