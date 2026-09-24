using System;
using System.Collections.Generic;

namespace Ac.Net
{
    // C03 §5.2 / S04 §5.5 冻结的 Disconnect 原因（1..7，两侧同序同义）。
    public enum DisconnectReason : byte
    {
        VersionMismatch = 1,
        TokenInvalid = 2,
        Timeout = 3,
        ServerShutdown = 4,
        MalformedPacket = 5,
        RateLimited = 6,
        SlowConsumer = 7,
    }

    public struct HelloPayload
    {
        public uint ClientNonce;
        public uint ReconnectToken;
    }

    public struct HelloAckPayload
    {
        public uint ServerTick;
        public uint Salt;
    }

    // C03 §5.2 的握手载荷编解码。令牌的值是 u32，线上是 8 位小写十六进制 ASCII
    //（S04 §5.5 的实现与 C03 §5.2 一致，所以 Hello 载荷 12B、Resume 载荷 8B）。
    public static class HandshakeCodec
    {
        public const int TokenBytes = 8;
        public const int HelloPayloadBytes = 12;   // clientNonce u32 + reconnectToken(ASCII)
        public const int HelloAckPayloadBytes = 8; // serverTick u32 + salt u32
        public const int ResumePayloadBytes = 8;   // reconnectToken(ASCII)
        public const int DisconnectPayloadBytes = 1;

        // §5.5：令牌 = salt ^ clientNonce（客户端本地算，服务端按会话记录复算比对）。
        public static uint ReconnectToken(uint clientNonce, uint salt)
        {
            return salt ^ clientNonce;
        }

        public static void WriteToken(byte[] buffer, int offset, uint token)
        {
            const string digits = "0123456789abcdef";
            for (var i = 0; i < TokenBytes; i++)
            {
                buffer[offset + i] = (byte)digits[(int)((token >> (4 * (TokenBytes - 1 - i))) & 0xF)];
            }
        }

        public static byte[] EncodeToken(uint token)
        {
            var bytes = new byte[TokenBytes];
            WriteToken(bytes, 0, token);
            return bytes;
        }

        // 非十六进制字符 -> BadValue；不足 8 字节 -> Truncated（与服务端 readToken 同判据）。
        public static bool TryReadToken(PacketReader reader, out uint token, out DecodeFailure failure)
        {
            byte[] raw;
            token = 0;
            if (!reader.TryReadBytes(TokenBytes, out raw))
            {
                failure = DecodeFailure.Truncated;
                return false;
            }
            for (var i = 0; i < TokenBytes; i++)
            {
                var digit = HexDigit(raw[i]);
                if (digit < 0)
                {
                    failure = DecodeFailure.BadValue;
                    return false;
                }
                token = (token << 4) | (uint)digit;
            }
            failure = DecodeFailure.Ok;
            return true;
        }

        private static int HexDigit(byte value)
        {
            if (value >= (byte)'0' && value <= (byte)'9') return value - (byte)'0';
            if (value >= (byte)'a' && value <= (byte)'f') return value - (byte)'a' + 10;
            return -1;   // 大写与其它字符都拒绝：线上只允许小写十六进制
        }

        public static byte[] EncodeHello(uint clientNonce, uint reconnectToken)
        {
            var payload = new byte[HelloPayloadBytes];
            PutU32(payload, 0, clientNonce);
            WriteToken(payload, 4, reconnectToken);
            return payload;
        }

        public static DecodeFailure DecodeHello(byte[] payload, out HelloPayload hello)
        {
            hello = default(HelloPayload);
            var reader = new PacketReader(payload);
            if (reader.Remaining != HelloPayloadBytes) return reader.Remaining < HelloPayloadBytes ? DecodeFailure.Truncated : DecodeFailure.BadLength;
            if (!reader.TryReadU32(out hello.ClientNonce)) return DecodeFailure.Truncated;
            DecodeFailure failure;
            if (!TryReadToken(reader, out hello.ReconnectToken, out failure)) return failure;
            return DecodeFailure.Ok;
        }

        public static byte[] EncodeHelloAck(uint serverTick, uint salt)
        {
            var payload = new byte[HelloAckPayloadBytes];
            PutU32(payload, 0, serverTick);
            PutU32(payload, 4, salt);
            return payload;
        }

        public static DecodeFailure DecodeHelloAck(byte[] payload, out HelloAckPayload ack)
        {
            ack = default(HelloAckPayload);
            var reader = new PacketReader(payload);
            if (reader.Remaining != HelloAckPayloadBytes) return reader.Remaining < HelloAckPayloadBytes ? DecodeFailure.Truncated : DecodeFailure.BadLength;
            if (!reader.TryReadU32(out ack.ServerTick)) return DecodeFailure.Truncated;
            if (!reader.TryReadU32(out ack.Salt)) return DecodeFailure.Truncated;
            return DecodeFailure.Ok;
        }

        public static byte[] EncodeResume(uint reconnectToken)
        {
            return EncodeToken(reconnectToken);
        }

        public static DecodeFailure DecodeResume(byte[] payload, out uint reconnectToken)
        {
            reconnectToken = 0;
            var reader = new PacketReader(payload);
            if (reader.Remaining != ResumePayloadBytes) return reader.Remaining < ResumePayloadBytes ? DecodeFailure.Truncated : DecodeFailure.BadLength;
            DecodeFailure failure;
            if (!TryReadToken(reader, out reconnectToken, out failure)) return failure;
            return DecodeFailure.Ok;
        }

        public static byte[] EncodeDisconnect(DisconnectReason reason)
        {
            return new[] { (byte)reason };
        }

        // 1..7 之外一律 BadValue（枚举是闭集，未知原因不允许静默通过）。
        public static DecodeFailure DecodeDisconnect(byte[] payload, out DisconnectReason reason)
        {
            reason = DisconnectReason.ServerShutdown;
            var reader = new PacketReader(payload);
            byte raw;
            if (!reader.TryReadU8(out raw)) return DecodeFailure.Truncated;
            if (reader.Remaining != 0) return DecodeFailure.BadLength;
            if (raw < (byte)DisconnectReason.VersionMismatch || raw > (byte)DisconnectReason.SlowConsumer) return DecodeFailure.BadValue;
            reason = (DisconnectReason)raw;
            return DecodeFailure.Ok;
        }

        private static void PutU32(byte[] buffer, int offset, uint value)
        {
            var position = offset;
            PacketWriter.PutU32(buffer, ref position, value);
        }

        // nonce 不消费 ai/spawn/fx 任何随机流：握手取随机数会让 FX 与生成的对拍结果不可复现（S04 §5.5 对 salt
        // 有同一条纪律）。用端点哈希 + 时钟派生，重发 Hello 复用同一个值——服务端按 nonce 在 5s 内去重。
        public static uint DeriveNonce(string endpoint, double nowMs)
        {
            var hash = 2166136261u;
            for (var i = 0; i < endpoint.Length; i++)
            {
                hash = (hash ^ endpoint[i]) * 16777619u;
            }
            return hash ^ (uint)(long)nowMs;
        }
    }

    // C03 §8 的令牌纪律：只存本地；宽限期内可复用；Resume 失败或宽限期到期即清空。
    public sealed class ReconnectTokenStore
    {
        private struct Entry
        {
            public uint Token;
            public double ExpiresMs;
        }

        private readonly Dictionary<string, Entry> _entries = new Dictionary<string, Entry>();

        // 令牌本身不带寿命：30s 宽限期是「从断线时刻起算」的会话属性（S04 §5.6），由状态机的
        // _graceStartMs + GracePeriodMs 判定。若在这里按 HelloAck + 30s 记过期，任何存活超过 30s 的
        // 正常会话一断线就会立刻放弃重连。
        public void Remember(string endpoint, uint token)
        {
            var entry = new Entry();
            entry.Token = token;
            _entries[endpoint] = entry;
        }

        public bool TryTake(string endpoint, out uint token)
        {
            token = 0;
            Entry entry;
            if (!_entries.TryGetValue(endpoint, out entry)) return false;
            token = entry.Token;
            return true;
        }

        public void Forget(string endpoint)
        {
            _entries.Remove(endpoint);
        }

        // 只回答「本端是否还留着这个端点的令牌」；30s 宽限期由 SessionStateMachine 的 _graceStartMs 判定。
        public bool HasValid(string endpoint)
        {
            return _entries.ContainsKey(endpoint);
        }

        internal int Count { get { return _entries.Count; } }
    }
}
