using System;
using System.Collections.Generic;
using System.Text;

namespace Ac.UI
{
    // C12 §5 聊天规则：64 字节截断（不切断多字节字符）、空文本提示、500ms 间隔、5 秒窗口 5 条、最多 5 行。
    // v2 收尾补上**输入缓冲**（此前只有规则与限流，没有任何输入通路，键位表第 11 条 `chat` 是死的）。
    public sealed class Chat
    {
        public const int ChatMaxBytes = 64;
        public const int ChatMaxLines = 5;
        public const float ChatMinIntervalMs = 500f;
        public const float ChatWindowMs = 5000f;
        public const int ChatWindowMax = 5;
        // 缓冲区按 UTF-16 码元算，与 ChatMaxBytes 同宽：截断口径只由 TruncateUtf8 一处负责（超过 64 字节
        // 的内容照收，发送时截断并提示"过长已截断"），这里只拦"缓冲区真的放不下"的键入 —— 与
        // Ac.UI.NameInput / Lobby.SanitizeName 的"键入不丢、出口截断"约定一致。
        public const int InputMaxChars = ChatMaxBytes;

        public const string HintEmpty = "请输入内容";
        public const string HintTooFast = "发送过快";
        public const string HintTooOften = "过于频繁";
        public const string HintTruncated = "过长已截断";

        private readonly List<string> _lines = new List<string>(ChatMaxLines);
        private readonly float[] _sentAt = new float[ChatWindowMax];

        // ---- 输入缓冲（局内聊天）----
        private readonly char[] _buffer = new char[InputMaxChars];
        private int _length;
        private string _bufferText = string.Empty;
        private bool _pendingOpen;       // 缓冲刚开、那一帧的换行不算"发送"

        public float ClockMs { get; private set; }
        public int DroppedCount { get; private set; }
        public string LastHint { get; private set; }
        public bool Visible { get; private set; }
        public IReadOnlyList<string> Lines { get { return _lines; } }
        public int LineCount { get { return _lines.Count; } }
        public int SentCount { get; private set; }
        // 输入缓冲开着 = 键鼠正在打字（不由 InputSampler 驱动移动/开火），也把聊天行画出来。
        public bool Focused { get; private set; }
        public string BufferingText { get { return _bufferText; } }
        public int BufferLength { get { return _length; } }
        public int BufferChangeCount { get; private set; }
        public int BufferDroppedCount { get; private set; }   // 缓冲区满（≥InputMaxChars 码元）被丢掉的字符
        public event Action<bool> OnFocusChanged;

        public Chat() { LastHint = string.Empty; }

        // 开/关输入缓冲。开着时把光标交还出去（解锁指针由 Driver 负责，本类不碰 UnityEngine）。
        public void Focus(bool focused)
        {
            if (Focused == focused) return;
            Focused = focused;
            _pendingOpen = focused;
            if (!focused) ClearBuffer();
            var handler = OnFocusChanged;
            if (handler != null) handler(focused);
        }

        public void ToggleFocus() { Focus(!Focused); }

        // 喂本帧键入的字符（`Input.inputString`，含 '\b'）。与其他键位的**时序**一致：'chat' 键按下的
        // 那一帧，`inputString` 已经带上了 '\n'，所以"回车开关"必须排在"喂字符"之后，否则同一次回车
        // 会先把缓冲关掉、再把 '\n' 漏出去。开户那一帧残留的换行只吃**那一帧**（见 _pendingOpen）。
        //
        // 热路径纪律（ManagedAllocBudgetBytes = 0）：输入缓冲没开 ⇒ 不碰字符串、立刻返回；开着但本帧没有
        // 输入 ⇒ 退格/换行那些分支不分配；只有真的改了才重建一次 Text —— 不是每帧拼串。
        // 返回"本帧的输入是不是该按打字处理"（驱动据此决定要不要把同一个键当开关）。
        public bool Apply(string typedThisFrame, bool escapePressed, double dtMs)
        {
            ClockMs += (float)dtMs;
            if (!Focused) return false;
            if (escapePressed) { Focus(false); return false; }   // Esc 只关缓冲，不发送
            // 打开缓冲的那一帧：把触发它的那个 '\n' 吃掉。**只限这一帧** —— 之后每一帧的回车都是"发送"。
            Feed(_pendingOpen ? DropLeadingNewlines(typedThisFrame) : typedThisFrame);
            _pendingOpen = false;
            return Focused;                                     // 回车发送后已经关了 ⇒ 不算打字
        }

        // 回车在 Feed 里是"发送"，但 Input.inputString 在**打开缓冲的那一帧**就已经带上了那个 '\n'。
        // 没有这一步的话，回车会变成"开→立刻发出一条空消息→关"。
        private static string DropLeadingNewlines(string typed)
        {
            if (string.IsNullOrEmpty(typed)) return typed;
            var i = 0;
            while (i < typed.Length && (typed[i] == '\n' || typed[i] == '\r')) i += 1;
            return i == 0 ? typed : typed.Substring(i);
        }

        // 喂本帧键入的字符。返回"缓冲内容有没有变"。
        public bool Feed(string typed)
        {
            if (string.IsNullOrEmpty(typed)) return false;
            var changed = false;
            for (var i = 0; i < typed.Length; i++)
            {
                var c = typed[i];
                if (c == '\n' || c == '\r')
                {
                    Submit();
                    return true;                       // 回车 = 发送并关闭，后面的字符属于下一帧
                }
                if (c == '\b')
                {
                    if (_length == 0) continue;
                    _length -= 1;
                    // 退格删的是"一个字符"：代理对要整个删，否则缓冲区里留下孤立代理，
                    // 截断层再处理就会产出非法 UTF-8。
                    if (_length > 0 && char.IsLowSurrogate(_buffer[_length]) && char.IsHighSurrogate(_buffer[_length - 1])) _length -= 1;
                    changed = true;
                    continue;
                }
                if (c == '\t') continue;               // 制表不进聊天（清洗层也会剥离）
                if (_length == InputMaxChars) { BufferDroppedCount += 1; continue; }
                _buffer[_length] = c;
                _length += 1;
                changed = true;
            }
            if (!changed) return true;
            RebuildBufferText();
            return true;
        }

        private void RebuildBufferText()
        {
            _bufferText = new string(_buffer, 0, _length);   // 只在真的有输入的这一帧重建
            BufferChangeCount += 1;
        }

        public void ClearBuffer()
        {
            if (_length == 0 && _bufferText.Length == 0) return;
            _length = 0;
            _bufferText = string.Empty;
            BufferChangeCount += 1;
        }

        // 回车 = 发送并关闭缓冲；被限流/空文本拒收时提示留在 LastHint，缓冲照关（提示由界面读）。
        private void Submit()
        {
            string hint;
            TrySend(_bufferText, out hint);
            ClearBuffer();
            Focus(false);
        }

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
