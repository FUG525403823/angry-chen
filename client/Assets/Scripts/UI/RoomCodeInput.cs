using System;
using System.Text;
using Ac.Net;

namespace Ac.UI
{
    public enum RoomCodeError : byte { None = 0, Empty = 1, WrongLength = 2, Sentinel = 3 }

    // C12 §5：房间码输入规则（1 转大写 → 2 丢弃表外字符 → 3 截断 4 位 → 4 加入判定）。
    // 字母表与服务端冻结值逐字一致：31 字符，排除 I L O 0 1。
    public sealed class RoomCodeInput
    {
        public const int RoomCodeLength = 4;
        public const string RoomCodeAlphabet = "ABCDEFGHJKMNPQRSTUVWXYZ23456789";
        public const string NewRoomCode = "0000";      // 新建房间哨兵，不是合法的加入码

        public string Raw { get; private set; }
        public string Code { get; private set; }
        public RoomCodeError Error { get; private set; }
        public string ErrorText { get { return ErrorTextOf(Error); } }

        public RoomCodeInput() { Raw = string.Empty; Code = string.Empty; Error = RoomCodeError.Empty; }

        public void SetRaw(string raw)
        {
            Raw = raw ?? string.Empty;
            Code = Normalize(Raw);
            Error = Validate(Code);
        }

        public static string Normalize(string raw)
        {
            if (string.IsNullOrEmpty(raw)) return string.Empty;
            var upper = raw.ToUpperInvariant();
            var builder = new StringBuilder(RoomCodeLength);
            for (var i = 0; i < upper.Length && builder.Length < RoomCodeLength; i++)
            {
                if (RoomCodeAlphabet.IndexOf(upper[i]) >= 0) builder.Append(upper[i]);
            }
            return builder.ToString();
        }

        public static bool IsJoinable(string code)
        {
            return Validate(code) == RoomCodeError.None;
        }

        public static RoomCodeError Validate(string code)
        {
            if (string.IsNullOrEmpty(code)) return RoomCodeError.Empty;
            if (code == NewRoomCode) return RoomCodeError.Sentinel;
            if (code.Length != RoomCodeLength) return RoomCodeError.WrongLength;
            for (var i = 0; i < code.Length; i++) if (RoomCodeAlphabet.IndexOf(code[i]) < 0) return RoomCodeError.WrongLength;
            return RoomCodeError.None;
        }

        public static string ErrorTextOf(RoomCodeError error)
        {
            if (error == RoomCodeError.Empty) return "请输入房间码";
            if (error == RoomCodeError.WrongLength) return "房间码为 4 位，允许的字符：" + RoomCodeAlphabet;
            if (error == RoomCodeError.Sentinel) return "0000 是新建房间，请点「新建房间」";
            return string.Empty;
        }

        // 大厅用：把当前输入交给服务器前的最后一道自检
        public bool CanJoin { get { return Error == RoomCodeError.None; } }
        public bool CanCreate { get { return true; } }
    }
}
