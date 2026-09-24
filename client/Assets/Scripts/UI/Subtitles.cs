using System.Collections.Generic;

namespace Ac.UI
{
    public enum SubtitleCue : byte { ChargeWarn = 0, WaveStart = 1, Downed = 2, Victory = 3, Defeat = 4 }

    // C11 §5：字幕 2600ms / 最多 3 行；同文本重复出现只刷新计时。
    public sealed class Subtitles
    {
        public const float LineMs = 2600f;
        public const int MaxLines = 3;

        private readonly string[] _lines = new string[MaxLines];
        private readonly float[] _remaining = new float[MaxLines];
        private readonly List<string> _visible = new List<string>(MaxLines);

        public int PushCount { get; private set; }
        public int DedupedCount { get; private set; }
        public int OverflowCount { get; private set; }
        public IReadOnlyList<string> Lines { get { return _visible; } }
        public int LineCount { get { return _visible.Count; } }

        public int Push(SubtitleCue cue, int wave = 0) { return Push(TextOf(cue, wave)); }

        public int Push(string text)
        {
            for (var i = 0; i < MaxLines; i++)
            {
                if (_remaining[i] <= 0f) continue;
                if (_lines[i] != text) continue;
                _remaining[i] = LineMs;      // 同文本重复只刷新计时
                DedupedCount += 1;
                Rebuild();
                return i;
            }
            for (var i = 0; i < MaxLines; i++)
            {
                if (_remaining[i] > 0f) continue;
                _lines[i] = text;
                _remaining[i] = LineMs;
                PushCount += 1;
                Rebuild();
                return i;
            }
            // 满 3 行：顶掉剩余最短的一条
            var oldest = 0;
            for (var i = 1; i < MaxLines; i++) if (_remaining[i] < _remaining[oldest]) oldest = i;
            _lines[oldest] = text;
            _remaining[oldest] = LineMs;
            OverflowCount += 1;
            Rebuild();
            return oldest;
        }

        public void Tick(float dtMs)
        {
            for (var i = 0; i < MaxLines; i++)
            {
                if (_remaining[i] <= 0f) continue;
                _remaining[i] = _remaining[i] > dtMs ? _remaining[i] - dtMs : 0f;
            }
            Rebuild();
        }

        public float RemainingOf(int index) { return _remaining[index]; }

        public static string TextOf(SubtitleCue cue, int wave)
        {
            if (cue == SubtitleCue.ChargeWarn) return "冲锋预警：侧移躲避";
            if (cue == SubtitleCue.WaveStart) return "第 " + wave + " 波开始";
            if (cue == SubtitleCue.Downed) return "你已倒地，等待救援";
            if (cue == SubtitleCue.Victory) return "胜利：守住牧场";
            return "失败：羊群获胜";
        }

        private void Rebuild()
        {
            _visible.Clear();
            for (var i = 0; i < MaxLines; i++) if (_remaining[i] > 0f) _visible.Add(_lines[i]);
        }
    }
}
