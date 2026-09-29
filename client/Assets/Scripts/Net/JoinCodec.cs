using System.Text;

namespace Ac.Net
{
    // ADR-009「握手时序」的 type 11 Join：昵称上报。载荷 = nameLen u8 + name[nameLen]，
    // 合计 2..13 字节。长度按 **UTF-8 字节数**算（与 MatchState 的 1..12 字节同源），不是字符数：
    // 一个汉字 3 字节，"牧羊人" 就是 9 字节，不是 3。
    //
    // 只做结构校验；"哪些码点允许"归服务端的净化器（setSessionName），客户端不重复一份规则——
    // 大厅键入的名字已经过 Lobby.SanitizeName，两侧口径同源。
    public static class JoinCodec
    {
        public const int MinNameBytes = 1;
        public const int MaxNameBytes = 12;
        public const int MinPayloadBytes = 1 + MinNameBytes;
        public const int MaxPayloadBytes = 1 + MaxNameBytes;

        private static readonly Encoding Utf8 = new UTF8Encoding(false, false);

        // 返回 false 表示"这个名字不该上线"（空 / 超 12 字节）⇒ 调用方不发送，而不是截断后发送。
        public static bool TryEncode(string name, out byte[] payload)
        {
            payload = null;
            if (string.IsNullOrEmpty(name)) return false;
            var byteCount = Utf8.GetByteCount(name);
            if (byteCount < MinNameBytes || byteCount > MaxNameBytes) return false;
            payload = new byte[1 + byteCount];
            payload[0] = (byte)byteCount;
            Utf8.GetBytes(name, 0, name.Length, payload, 1);
            return true;
        }
    }
}
