namespace Ac.UI
{
    public struct KillEntry
    {
        public int KillerId;
        public int VictimId;
        public bool Headshot;
        public int Wave;
    }

    // C10 §5(e)：击杀记录 3000ms、上限 6、渐隐 600ms，爆头单独样式。
    public sealed class KillFeed
    {
        public const float EntryMs = 3000f;
        public const int Capacity = 6;
        public const float FadeMs = 600f;

        private readonly KillEntry[] _entries = new KillEntry[Capacity];
        private readonly float[] _remaining = new float[Capacity];
        private int _count;

        public int Count { get { return _count; } }
        public int OverflowCount { get; private set; }

        public int Push(in KillEntry entry)
        {
            for (var i = 0; i < Capacity; i++)
            {
                if (_remaining[i] > 0f) continue;
                _entries[i] = entry;
                _remaining[i] = EntryMs;
                _count += 1;
                return i;
            }
            // 满了顶掉最旧（剩余时间最短）的一条，交火密集时玩家更想看最近的
            var oldest = 0;
            for (var i = 1; i < Capacity; i++) if (_remaining[i] < _remaining[oldest]) oldest = i;
            _entries[oldest] = entry;
            _remaining[oldest] = EntryMs;
            OverflowCount += 1;
            return oldest;
        }

        public void Tick(float dtMs)
        {
            var live = 0;
            for (var i = 0; i < Capacity; i++)
            {
                if (_remaining[i] <= 0f) continue;
                _remaining[i] = _remaining[i] > dtMs ? _remaining[i] - dtMs : 0f;
                if (_remaining[i] > 0f) live += 1;
            }
            _count = live;
        }

        public float AlphaOf(int index) { return Alpha(_remaining[index]); }
        public bool IsHeadshot(int index) { return _entries[index].Headshot; }
        public int ColorOf(int index) { return IsHeadshot(index) ? Hud.ColorTarget : Hud.ColorNormal; }

        public static float Alpha(float remainingMs)
        {
            if (remainingMs <= 0f) return 0f;
            if (remainingMs >= FadeMs) return 1f;
            return remainingMs / FadeMs;
        }
    }
}
