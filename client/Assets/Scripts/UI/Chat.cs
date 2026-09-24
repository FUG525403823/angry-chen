using System.Collections.Generic;
using System.Text;

namespace Ac.UI
{
    // C12 §5 聊天规则：64 字节截断（不切断多字节字符）、空文本提示、500ms 间隔、5 秒窗口 5 条、最多 5 行。
    public sealed class Chat
    {
        public const int ChatMaxBytes = 64;
        public const int ChatMaxLines = 5;
        public const float ChatMinIntervalMs = 500f;
        public const float ChatWindowMs = 5000f;
        public const int ChatWindowMax = 5;

        public const string HintEmpty = "请输入内容";
        public const string HintTooFast = "发送过快";
        public const string HintTooOften = "过于频繁";
        public const string HintTruncated = "过长已截断";

        private readonly List<string> _lines = new List<string>(ChatMaxLines);
        private readonly float[] _sentAt = new float[ChatWindowMax];

        public float ClockMs { get; private set; }
        public int DroppedCount { get; private set; }
        public string LastHint { get; private set; }
        public bool Visible { get; private set; }
        public IReadOnlyList<string> Lines { get { return _lines; } }
        public int LineCount { get { return _lines.Count; } }
        public int SentCount { get; private set; }

        public Chat() { LastHint = string.Empty; }

        public void Tick(float dtMs) { ClockMs += dtMs; }

        public bool TrySend(string text, out string hint)
        {
            hint = string.Empty;
            var trimmed = (text ?? string.Empty).Trim();
            if (trimmed.Length == 0) { hint = HintEmpty; LastHint = hint; DroppedCount += 1; return false; }
            if (ClockMs - _sentAt[0] < ChatMinIntervalMs && SentCount > 0) { hint = HintTooFast; LastHint = hint; DroppedCount += 1; return false; }
            if (InWindowCount() >= ChatWindowMax) { hint = HintTooOften; LastHint = hint; DroppedCount += 1; return false; }

            var overlong = Utf8Bytes(trimmed) > ChatMaxBytes;
            var payload = TruncateUtf8(trimmed, ChatMaxBytes);
            hint = overlong ? HintTruncated : string.Empty;
            for (var i = ChatWindowMax - 1; i > 0; i--) _sentAt[i] = _sentAt[i - 1];
            _sentAt[0] = ClockMs;
            SentCount += 1;
            LastHint = hint;
            Push(0, payload);
            return true;
        }

        private int InWindowCount()
        {
            var count = 0;
            for (var i = 0; i < ChatWindowMax; i++) if (SentCount > i && ClockMs - _sentAt[i] <= ChatWindowMs) count += 1;
            return count;
        }

        public void Push(int pid, string text)
        {
            var line = text ?? string.Empty;
            if (_lines.Count >= ChatMaxLines) _lines.RemoveAt(0);
            _lines.Add(line);
        }

        public void SetVisible(bool visible) { Visible = visible; }

        // UTF-8 截断：不切断多字节字符（代理对按一个 4 字节单位处理）
        public static string TruncateUtf8(string text, int maxBytes)
        {
            if (string.IsNullOrEmpty(text)) return string.Empty;
            var bytes = 0;
            var builder = new StringBuilder(text.Length);
            for (var i = 0; i < text.Length; i++)
            {
                var c = text[i];
                int size;
                int units;
                if (char.IsHighSurrogate(c) && i + 1 < text.Length && char.IsLowSurrogate(text[i + 1])) { size = 4; units = 2; }
                else if (c < 0x80) { size = 1; units = 1; }
                else if (c < 0x800) { size = 2; units = 1; }
                else { size = 3; units = 1; }
                if (bytes + size > maxBytes) break;
                builder.Append(text, i, units);
                bytes += size;
                i += units - 1;
            }
            return builder.ToString();
        }

        public static int Utf8Bytes(string text)
        {
            if (string.IsNullOrEmpty(text)) return 0;
            var bytes = 0;
            for (var i = 0; i < text.Length; i++)
            {
                var c = text[i];
                if (char.IsHighSurrogate(c) && i + 1 < text.Length && char.IsLowSurrogate(text[i + 1])) { bytes += 4; i += 1; }
                else if (c < 0x80) bytes += 1;
                else if (c < 0x800) bytes += 2;
                else bytes += 3;
            }
            return bytes;
        }
    }
}
