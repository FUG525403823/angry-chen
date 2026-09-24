using System;

namespace Ac.Net
{
    // C03 §5.1 的写侧：与 PacketHeader.Read 用同一份偏移规则（通用包头 8B -> 可靠扩展头 12B -> 分片头 4B -> 载荷）。
    // 编码是最内层路径，容量不足或头部非法属于程序错误，直接抛异常——网络输入的错误一律由读侧返回 DecodeFailure。
    public static class PacketWriter
    {
        // 通用包头里 type 的偏移：积压队列要按类型挑出快照，读侧与写侧共用同一个常量。
        public const int TypeOffset = 1;

        public static int Size(in PacketHeader header)
        {
            var size = PacketHeader.CommonHeaderSize;
            if (header.IsReliable) size += PacketHeader.ReliableHeaderSize;
            if (header.IsMoreFragments) size += PacketHeader.FragmentHeaderSize;
            return size;
        }

        public static int Write(byte[] buffer, in PacketHeader header, byte[] payload, int offset, int count)
        {
            if (buffer == null) throw new ArgumentNullException("buffer");
            var size = Size(header);
            var total = size + count;
            if (total > buffer.Length) throw new ArgumentException("数据报缓冲不足：" + total + " > " + buffer.Length);
            if (header.Version != PacketHeader.ProtocolVersion) throw new ArgumentException("协议版本必须为 1");
            if (!PacketHeader.IsFlagsValidForType(header.Type, (ushort)header.Flags))
                throw new ArgumentException("flags 与 type 不符：" + header.Type + "/" + header.Flags);

            var position = 0;
            buffer[position++] = header.Version;
            buffer[position++] = (byte)header.Type;
            PutU16(buffer, ref position, (ushort)header.Flags);
            PutU16(buffer, ref position, header.Session);
            PutU16(buffer, ref position, header.Seq);

            if (header.IsReliable)
            {
                PutU32(buffer, ref position, header.MsgId);
                PutU32(buffer, ref position, header.AckBase);
                PutU32(buffer, ref position, header.AckBits);
            }
            if (header.IsMoreFragments)
            {
                PutU16(buffer, ref position, header.FragId);
                buffer[position++] = header.FragIndex;
                buffer[position++] = header.FragCount;
            }
            if (count > 0)
            {
                Array.Copy(payload, offset, buffer, position, count);
                position += count;
            }
            return position;
        }

        // 便捷重载：一次分配正好够用的缓冲。
        public static byte[] Build(in PacketHeader header, byte[] payload, int offset, int count)
        {
            var buffer = new byte[Size(header) + count];
            Write(buffer, header, payload, offset, count);
            return buffer;
        }

        public static byte[] Build(in PacketHeader header)
        {
            return Build(header, null, 0, 0);
        }

        private static void PutU16(byte[] buffer, ref int position, ushort value)
        {
            buffer[position++] = (byte)(value & 0xFF);
            buffer[position++] = (byte)(value >> 8);
        }

        internal static void PutU32(byte[] buffer, ref int position, uint value)
        {
            buffer[position++] = (byte)(value & 0xFF);
            buffer[position++] = (byte)((value >> 8) & 0xFF);
            buffer[position++] = (byte)((value >> 16) & 0xFF);
            buffer[position++] = (byte)((value >> 24) & 0xFF);
        }
    }
}
