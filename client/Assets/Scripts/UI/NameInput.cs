using System;

namespace Ac.UI
{
    // 大厅昵称的键入捕获（C12 §4：昵称由玩家自己在大厅输入）。
    // 本仓没有 HUD 渲染器（全仓 OnGUI/Canvas/DrawTexture/Blit 命中 0），所以捕获只有一条来源：
    // 引擎的 Input.inputString（本帧键入的字符，含 '\b'），由 GameLoopDriver 交进来。
    //
    // 热路径纪律（FrameProfiler.ManagedAllocBudgetBytes = 0）：本帧没有输入 ⇒ Feed 立刻返回，
    // 不碰字符串、不分配；只有真的改了才重建一次 Text —— 不是每帧拼串。
    public sealed class NameInput
    {
        // 缓冲区按 UTF-16 码元算，比 12 字节的 wire 上限宽：清洗与按字节截断由 Lobby.SanitizeName 负责
        //（12 字节最多 12 个 ASCII 或 4 个汉字），这里只保证"键入不丢字符、退格不出半个代理对"。
        public const int MaxChars = 24;

        private readonly char[] _buffer = new char[MaxChars];
        private int _length;
        private string _text = string.Empty;

        public string Text { get { return _text; } }
        public int Length { get { return _length; } }
        public int ChangeCount { get; private set; }
        public int DroppedCount { get; private set; }   // 缓冲区满（≥24 码元）被丢掉的字符
        public event Action<string> OnChanged;

        // 喂本帧键入的字符。返回 Text 是否真的变了；空输入不产生任何分配。
        public bool Feed(string typed)
        {
            if (string.IsNullOrEmpty(typed)) return false;
            var changed = false;
            for (var i = 0; i < typed.Length; i++)
            {
                var c = typed[i];
                if (c == '\b')
                {
                    if (_length == 0) continue;
                    _length -= 1;
                    // 退格删的是"一个字符"：代理对要整个删，否则缓冲区里留下孤立代理，
                    // 清洗层再截断就会产出非法 UTF-8。
                    if (_length > 0 && char.IsLowSurrogate(_buffer[_length]) && char.IsHighSurrogate(_buffer[_length - 1])) _length -= 1;
                    changed = true;
                    continue;
                }
                if (c == '\n' || c == '\r' || c == '\t') continue;   // 回车/制表不进昵称（清洗层也会剥离）
                if (_length == MaxChars) { DroppedCount += 1; continue; }
                _buffer[_length] = c;
                _length += 1;
                changed = true;
            }
            if (!changed) return false;
            _text = new string(_buffer, 0, _length);    // 只在真的有输入的这一帧重建
            ChangeCount += 1;
            var handler = OnChanged;
            if (handler != null) handler(_text);
            return true;
        }

        public void Clear()
        {
            if (_length == 0 && _text.Length == 0) return;
            _length = 0;
            _text = string.Empty;
            ChangeCount += 1;
            var handler = OnChanged;
            if (handler != null) handler(_text);
        }
    }
}
