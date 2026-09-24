namespace Ac.UI
{
    // C10 §5(e)：波次横幅 4500ms / 队列 3；波次刻度 10 段，Boss 波间隔 5；波间倒计时只读权威值。
    public sealed class WaveBanner
    {
        public const float BannerMs = 4500f;
        public const int QueueCapacity = 3;
        public const int TickSegments = 10;
        public const int BossInterval = 5;

        private readonly int[] _queue = new int[QueueCapacity];
        private int _queued;
        private int _cursor;

        public float RemainingMs { get; private set; }
        public int CurrentWave { get; private set; }
        public int IntermissionMs { get; private set; }
        public int OverflowCount { get; private set; }
        public int QueuedCount { get { return _queued; } }

        public static bool IsBossWave(int wave) { return wave > 0 && wave % BossInterval == 0; }
        public static int TickIndex(int wave) { return wave <= 0 ? 0 : (wave - 1) % TickSegments; }

        public void ShowWave(int wave)
        {
            if (RemainingMs > 0f)
            {
                if (_queued >= QueueCapacity) { OverflowCount += 1; return; }
                _queue[_cursor] = wave;
                _cursor = (_cursor + 1) % QueueCapacity;
                _queued += 1;
                return;
            }
            CurrentWave = wave;
            RemainingMs = BannerMs;
        }

        public void SetIntermission(int ms) { IntermissionMs = ms < 0 ? 0 : ms; }

        public void Tick(float dtMs)
        {
            if (RemainingMs > 0f)
            {
                RemainingMs = RemainingMs > dtMs ? RemainingMs - dtMs : 0f;
                return;
            }
            if (_queued > 0)
            {
                var head = _cursor - _queued;
                if (head < 0) head += QueueCapacity;
                CurrentWave = _queue[head];
                _queued -= 1;
                RemainingMs = BannerMs;
            }
        }
    }
}
