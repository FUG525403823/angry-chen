using System;
using System.Collections.Generic;

namespace Ac.Net
{
    // C03 §5.4 / S04 §5.4 的出站切分。载荷上限 = 1200 - 8 - 12 - 4 = 1176 字节，单片组最多 8 片（9408 字节）。
    public static class Fragmenter
    {
        public const int MaxDatagramBytes = 1200;
        public const int MaxFragmentPayload = MaxDatagramBytes - PacketHeader.CommonHeaderSize - PacketHeader.ReliableHeaderSize - PacketHeader.FragmentHeaderSize;
        public const int MaxFragments = PacketHeader.MaxFragments;
        public const int MaxLogicalMessageBytes = MaxFragmentPayload * MaxFragments;

        public static int FragmentCount(int payloadBytes)
        {
            if (payloadBytes <= 0) return 0;
            return (payloadBytes + MaxFragmentPayload - 1) / MaxFragmentPayload;
        }

        // 按模板头部（type/session/seq/msgId/ack 状态由调用方填好）切分，片序即 fragIndex。
        // 同一逻辑消息的所有片共用 msgId 与 seq：重组键是 (type, fragId)，不是 msgId。
        // 返回片数；0 表示超长（> 9408 字节）或片数为 0，调用方按发送失败处理。
        public static int Split(in PacketHeader template, ushort fragId, byte[] payload, List<byte[]> datagrams)
        {
            if (datagrams == null) throw new ArgumentNullException("datagrams");
            var length = payload == null ? 0 : payload.Length;
            var count = FragmentCount(length);
            if (count == 0 || count > MaxFragments) return 0;
            for (var index = 0; index < count; index++)
            {
                var offset = index * MaxFragmentPayload;
                var slice = Math.Min(MaxFragmentPayload, length - offset);
                var header = template;
                // §5.4：片头 type 恒为 Fragment(9)，flags = 被分片通道的 reliable 位 | moreFragments
                //（与 server/src/net/fragment.cpp 的 splitMessage 逐字相同）。逻辑类型因此只能由 reliable 位推回。
                header.Type = PacketType.Fragment;
                header.Flags = (PacketFlags)(((int)template.Flags & (int)PacketFlags.Reliable) | (int)PacketFlags.MoreFragments);
                header.FragId = fragId;
                header.FragIndex = (byte)index;
                header.FragCount = (byte)count;
                datagrams.Add(PacketWriter.Build(header, payload, offset, slice));
            }
            return count;
        }

        // 是否必须分片：通用包头 + 可靠/分片扩展 + 载荷 > 1200（§5.1）。传输层与回环用例共用同一判据。
        public static bool NeedsFragmentation(in PacketHeader header, int payloadBytes)
        {
            return PacketWriter.Size(header) + payloadBytes > MaxDatagramBytes;
        }
    }

    // 入站重组：会话在传输层已过滤，键 = (type, fragId)。60 tick（3s）未收齐整组丢弃并计 invalidPackets。
    public sealed class FragmentReassembler
    {
        public const int TimeoutTicks = 60;
        public const int ServerTickMs = 50;    // 服务端模拟步长：60 tick = 3s，与 kFragmentTimeoutTicks 同值
        public const int TimeoutMs = TimeoutTicks * ServerTickMs;
        public const int MaxActiveGroups = 16; // §8 的对策：同时活跃的分片组上限

        private sealed class Group
        {
            public bool Reliable;
            public ushort FragId;
            public byte FragCount;
            public byte[] Buffer;
            public bool[] Present;
            public int[] Lengths;
            public int PresentCount;
            public double DeadlineMs;
        }

        private readonly List<Group> _groups = new List<Group>();

        public int ActiveGroups { get { return _groups.Count; } }

        // Ok = 已收齐（message 交给上层按普通包解码）；Truncated = 还缺片；BadValue = 片头非法，整组作废。
        public DecodeFailure Accept(in PacketHeader header, byte[] payload, int offset, int count, double nowMs,
            out byte[] message)
        {
            message = null;
            if (!header.IsMoreFragments || header.Type != PacketType.Fragment) return DecodeFailure.BadValue;
            if (header.FragCount == 0 || header.FragCount > Fragmenter.MaxFragments) return DecodeFailure.BadValue;
            if (header.FragIndex >= header.FragCount) return DecodeFailure.BadValue;

            var index = IndexOf(header.IsReliable, header.FragId);
            if (index < 0)
            {
                if (_groups.Count >= MaxActiveGroups) return DecodeFailure.BadValue;
                var fresh = new Group();
                fresh.Reliable = header.IsReliable;
                fresh.FragId = header.FragId;
                fresh.FragCount = header.FragCount;
                fresh.Buffer = new byte[Fragmenter.MaxFragmentPayload * header.FragCount];
                fresh.Present = new bool[header.FragCount];
                fresh.Lengths = new int[header.FragCount];
                // §5.4：60 tick 从**组首片**起算（S04 的 Reassembler::add 用 firstTick）。若每片都刷新，
                // 对端只要 3s 内送一片就能让组永生，§8「重组内存吃满」的对策就落空了。
                fresh.DeadlineMs = nowMs + TimeoutMs;
                _groups.Add(fresh);
                index = _groups.Count - 1;
            }

            var group = _groups[index];
            if (group.FragCount != header.FragCount)
            {
                _groups.RemoveAt(index);   // 同组分片数不一致：整组作废，等重传
                return DecodeFailure.BadValue;
            }
            var position = header.FragIndex * Fragmenter.MaxFragmentPayload;
            if (position + count > group.Buffer.Length)
            {
                _groups.RemoveAt(index);
                return DecodeFailure.BadValue;
            }
            if (!group.Present[header.FragIndex])
            {
                if (count > 0) Array.Copy(payload, offset, group.Buffer, position, count);
                group.Present[header.FragIndex] = true;
                group.Lengths[header.FragIndex] = count;
                group.PresentCount += 1;
            }

            if (group.PresentCount < group.FragCount) return DecodeFailure.Truncated;

            var total = 0;
            for (var i = 0; i < group.FragCount; i++) total += group.Lengths[i];
            message = new byte[total];
            var written = 0;
            for (var i = 0; i < group.FragCount; i++)
            {
                if (group.Lengths[i] > 0) Array.Copy(group.Buffer, i * Fragmenter.MaxFragmentPayload, message, written, group.Lengths[i]);
                written += group.Lengths[i];
            }
            _groups.RemoveAt(index);
            return DecodeFailure.Ok;
        }

        // 超时清理：返回被丢弃的组数（调用方计 invalidPackets）。
        public int Expire(double nowMs)
        {
            var dropped = 0;
            for (var i = _groups.Count - 1; i >= 0; i--)
            {
                if (_groups[i].DeadlineMs > nowMs) continue;
                _groups.RemoveAt(i);
                dropped++;
            }
            return dropped;
        }

        public void Reset()
        {
            _groups.Clear();
        }

        private int IndexOf(bool reliable, ushort fragId)
        {
            for (var i = 0; i < _groups.Count; i++)
            {
                if (_groups[i].Reliable == reliable && _groups[i].FragId == fragId) return i;
            }
            return -1;
        }
    }
}
