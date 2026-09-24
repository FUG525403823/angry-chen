using System;

namespace Ac.Net
{
    // C02 §5.7 冻结的拒绝原因（与 server/src/net/codec.hpp 的枚举同序）。
    public enum DecodeFailure
    {
        Ok = 0,
        Truncated = 1,
        BadVersion = 2,
        BadType = 3,
        BadLength = 4,
        BadValue = 5,
        BadSession = 6,
        UnknownEvent = 7,
        DuplicateEventId = 8,
    }

    // C02 §5.1 / S03 §5.1 的 type 码表。
    public enum PacketType
    {
        Hello = 1,
        HelloAck = 2,
        Resume = 3,
        Command = 4,
        Snapshot = 5,
        Event = 6,
        KeepAlive = 7,
        Disconnect = 8,
        Fragment = 9,
        MatchState = 10,
    }

    [Flags]
    public enum PacketFlags
    {
        None = 0,
        Reliable = 1,
        MoreFragments = 2,
        AckOnly = 4,
    }

    public struct PacketHeader
    {
        public byte Version;
        public PacketType Type;
        public PacketFlags Flags;
        public ushort Session;
        public ushort Seq;

        // 仅 Reliable 位置位时有效。
        public uint MsgId;
        public uint AckBase;
        public uint AckBits;

        // 仅 MoreFragments 位置位时有效。
        public ushort FragId;
        public byte FragIndex;
        public byte FragCount;

        public bool IsReliable { get { return (Flags & PacketFlags.Reliable) != 0; } }
        public bool IsMoreFragments { get { return (Flags & PacketFlags.MoreFragments) != 0; } }
        public bool IsAckOnly { get { return (Flags & PacketFlags.AckOnly) != 0; } }

        // 按 §5.1 的顺序读：通用包头 -> 可靠扩展头（恒在前）-> 分片头；越界即 Truncated。
        public static DecodeFailure Read(PacketReader reader, out PacketHeader header)
        {
            header = default(PacketHeader);
            byte version;
            byte type;
            ushort flags;
            ushort session;
            ushort seq;
            // 通用包头 8 字节先整块读完，再判 version / type / flags：与服务端 decodePacket 同序
            //（包头截断优先于取值非法），同一畸形帧两侧给同样的失败码。
            if (!reader.TryReadU8(out version)) return DecodeFailure.Truncated;
            if (!reader.TryReadU8(out type)) return DecodeFailure.Truncated;
            if (!reader.TryReadU16(out flags)) return DecodeFailure.Truncated;
            if (!reader.TryReadU16(out session)) return DecodeFailure.Truncated;
            if (!reader.TryReadU16(out seq)) return DecodeFailure.Truncated;

            header.Version = version;
            header.Type = (PacketType)type;
            header.Flags = (PacketFlags)flags;
            header.Session = session;
            header.Seq = seq;

            if (version != ProtocolVersion) return DecodeFailure.BadVersion;
            if (type < (byte)PacketType.Hello || type > (byte)PacketType.MatchState) return DecodeFailure.BadType;
            if (!IsFlagsValidForType(header.Type, flags)) return DecodeFailure.BadValue;

            if (header.IsReliable)
            {
                if (!reader.TryReadU32(out header.MsgId)) return DecodeFailure.Truncated;
                if (!reader.TryReadU32(out header.AckBase)) return DecodeFailure.Truncated;
                if (!reader.TryReadU32(out header.AckBits)) return DecodeFailure.Truncated;
            }

            if (header.IsMoreFragments)
            {
                if (!reader.TryReadU16(out header.FragId)) return DecodeFailure.Truncated;
                if (!reader.TryReadU8(out header.FragIndex)) return DecodeFailure.Truncated;
                if (!reader.TryReadU8(out header.FragCount)) return DecodeFailure.Truncated;
                if (header.FragCount == 0 || header.FragCount > MaxFragments) return DecodeFailure.BadValue;
                if (header.FragIndex >= header.FragCount) return DecodeFailure.BadValue;
            }

            return DecodeFailure.Ok;
        }

        // §5.1 通道与类型映射（与 wire.hpp 的 requiredFlags 同表）：Hello / Snapshot 无扩展头，
        // KeepAlive 恒为 reliable|ackOnly，其余可靠。
        public static ushort RequiredFlags(PacketType type)
        {
            switch (type)
            {
                case PacketType.Hello: return 0;
                case PacketType.Snapshot: return 0;
                case PacketType.KeepAlive: return (ushort)(PacketFlags.Reliable | PacketFlags.AckOnly);
                default: return (ushort)PacketFlags.Reliable;
            }
        }

        // 分片包（type 9）例外：必须置 moreFragments，reliable 由被分片的通道决定。
        public static bool IsFlagsValidForType(PacketType type, ushort flags)
        {
            if (type == PacketType.Fragment)
            {
                var fragmented = (ushort)PacketFlags.MoreFragments;
                var known = (ushort)(PacketFlags.Reliable | PacketFlags.MoreFragments);
                return (flags & fragmented) != 0 && (flags & ~known) == 0;
            }
            return flags == RequiredFlags(type);
        }

        public const byte ProtocolVersion = 1;
        public const int CommonHeaderSize = 8;
        public const int ReliableHeaderSize = 12;
        public const int FragmentHeaderSize = 4;
        public const int MaxFragments = 8;
    }

    // C02 §9：小端顺序读取器。任何越界立即返回失败，不补 0 后继续解析。
    public sealed class PacketReader
    {
        private static readonly byte[] _empty = new byte[0];

        private readonly byte[] _buffer;
        private int _position;

        // C02 §9：解码入口只返回失败码、不抛异常，null 载荷按空缓冲处理（后续读取一律 Truncated）。
        public PacketReader(byte[] buffer)
        {
            _buffer = buffer ?? _empty;
        }

        public int Position { get { return _position; } }
        public int Length { get { return _buffer.Length; } }
        public int Remaining { get { return _buffer.Length - _position; } }

        public bool TryReadU8(out byte value)
        {
            if (Remaining < 1) { value = 0; return false; }
            value = _buffer[_position];
            _position += 1;
            return true;
        }

        public bool TryReadI8(out sbyte value)
        {
            byte raw;
            if (!TryReadU8(out raw)) { value = 0; return false; }
            value = unchecked((sbyte)raw);
            return true;
        }

        public bool TryReadU16(out ushort value)
        {
            if (Remaining < 2) { value = 0; return false; }
            value = (ushort)(_buffer[_position] | (_buffer[_position + 1] << 8));
            _position += 2;
            return true;
        }

        public bool TryReadI16(out short value)
        {
            ushort raw;
            if (!TryReadU16(out raw)) { value = 0; return false; }
            value = unchecked((short)raw);
            return true;
        }

        public bool TryReadU32(out uint value)
        {
            if (Remaining < 4) { value = 0; return false; }
            value = (uint)(_buffer[_position] | (_buffer[_position + 1] << 8) | (_buffer[_position + 2] << 16) |
                (_buffer[_position + 3] << 24));
            _position += 4;
            return true;
        }

        public bool TrySkip(int count)
        {
            if (count < 0 || Remaining < count) return false;
            _position += count;
            return true;
        }

        public bool TryReadBytes(int count, out byte[] bytes)
        {
            if (count < 0 || Remaining < count) { bytes = null; return false; }
            bytes = new byte[count];
            Array.Copy(_buffer, _position, bytes, 0, count);
            _position += count;
            return true;
        }
    }
}
