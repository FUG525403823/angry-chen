using System.Collections.Generic;

namespace Ac.Net
{
    // C02 §5.2 / S03 §5.4 的事件类型码表（数值由该表冻结，不得改号）。
    public enum EventType : byte
    {
        PlayerHit = 1,
        SheepKilled = 2,
        WaveStart = 3,
        WaveClear = 4,
        PlayerDowned = 5,
        ReviveProgress = 6,
        ReviveDone = 7,
        RageActivated = 8,
        MatchEnded = 9,
        PhaseChange = 10,
    }

    // 各字段只在对应 type 下有效（见 EventCodec.EntrySize 的注释表）。
    public struct EventEntry
    {
        public uint EventId;
        public EventType Type;
        public ushort SubjectId;
        public ushort TargetId;
        public ushort Value;
        public byte Flags;
        public short HitX;
        public short HitY;
        public short HitZ;
        public byte Kind;
        public byte Wave;
        public ushort Budget;
        public uint ElapsedMs;
        public ushort DurationMs;
        public ushort Ratio255;
        public byte Reason;
        public byte Phase;
        public ushort IntermissionMs;
    }

    public struct EventFrame
    {
        public uint Tick;
        public EventEntry[] Entries;
        public int DroppedDuplicates;
    }

    public static class EventCodec
    {
        public const int MaxEventsPerFrame = 64;

        // §5.2 冻结的条目长度（18/10/8/10/7/11/9/9/11/9）由服务端与共享 fixture 守着，
        // 解码侧只需知道类型是否在冻结的 1..10 之内（字节数写在这里只会与服务端漂移）。
        public static bool IsKnownEvent(EventType type)
        {
            return type >= EventType.PlayerHit && type <= EventType.PhaseChange;
        }

        // type=6 事件帧载荷：tick u32 | count u8 | 条目块（块恒在末尾）。
        public static DecodeFailure DecodeFrame(byte[] payload, out EventFrame frame)
        {
            return DecodeFrame(payload, null, out frame);
        }

        // tracker 供接收侧跨帧共用（S03 §5.4 的全局幂等键）；为空时只保证本次调用内幂等。
        public static DecodeFailure DecodeFrame(byte[] payload, EventIdTracker tracker, out EventFrame frame)
        {
            frame = default(EventFrame);
            var reader = new PacketReader(payload);
            uint tick;
            if (!reader.TryReadU32(out tick)) return DecodeFailure.Truncated;
            byte count;
            if (!reader.TryReadU8(out count)) return DecodeFailure.Truncated;
            if (count > MaxEventsPerFrame) return DecodeFailure.BadValue;

            EventEntry[] entries;
            int dropped;
            var failure = DecodeEntries(reader, count, tracker, out entries, out dropped);
            if (failure != DecodeFailure.Ok) return failure;
            if (reader.Remaining != 0) return DecodeFailure.BadLength;

            frame.Tick = tick;
            frame.Entries = entries;
            frame.DroppedDuplicates = dropped;
            return DecodeFailure.Ok;
        }

        // 快照载荷末尾的事件块复用同一批条目编码（tick 由快照自身承载）。
        public static DecodeFailure DecodeEntries(PacketReader reader, int count, EventIdTracker tracker,
            out EventEntry[] entries, out int droppedDuplicates)
        {
            entries = null;
            droppedDuplicates = 0;
            var ids = tracker ?? new EventIdTracker();
            var kept = new List<EventEntry>(count);
            for (var i = 0; i < count; i++)
            {
                var entry = default(EventEntry);
                if (!reader.TryReadU32(out entry.EventId)) return DecodeFailure.Truncated;
                byte type;
                if (!reader.TryReadU8(out type)) return DecodeFailure.Truncated;
                if (!IsKnownEvent((EventType)type)) return DecodeFailure.UnknownEvent;
                entry.Type = (EventType)type;

                var payloadFailure = ReadPayload(reader, ref entry);
                if (payloadFailure != DecodeFailure.Ok) return payloadFailure;

                // 重复或回退的 eventId 静默丢弃（幂等键，S03 §5.4）：字节已经消费掉，条目不入结果。
                if (!ids.IsNew(entry.EventId))
                {
                    droppedDuplicates++;
                    continue;
                }
                kept.Add(entry);
            }

            entries = kept.ToArray();
            return DecodeFailure.Ok;
        }

        private static DecodeFailure ReadPayload(PacketReader reader, ref EventEntry entry)
        {
            switch (entry.Type)
            {
                case EventType.PlayerHit:
                    if (!reader.TryReadU16(out entry.SubjectId)) return DecodeFailure.Truncated;
                    if (!reader.TryReadU16(out entry.TargetId)) return DecodeFailure.Truncated;
                    if (!reader.TryReadU16(out entry.Value)) return DecodeFailure.Truncated;
                    if (!reader.TryReadU8(out entry.Flags)) return DecodeFailure.Truncated;
                    if (!reader.TryReadI16(out entry.HitX)) return DecodeFailure.Truncated;
                    if (!reader.TryReadI16(out entry.HitY)) return DecodeFailure.Truncated;
                    if (!reader.TryReadI16(out entry.HitZ)) return DecodeFailure.Truncated;
                    return DecodeFailure.Ok;
                case EventType.SheepKilled:
                    if (!reader.TryReadU16(out entry.TargetId)) return DecodeFailure.Truncated;
                    if (!reader.TryReadU16(out entry.SubjectId)) return DecodeFailure.Truncated;
                    if (!reader.TryReadU8(out entry.Kind)) return DecodeFailure.Truncated;
                    return DecodeFailure.Ok;
                case EventType.WaveStart:
                    if (!reader.TryReadU8(out entry.Wave)) return DecodeFailure.Truncated;
                    if (!reader.TryReadU16(out entry.Budget)) return DecodeFailure.Truncated;
                    return DecodeFailure.Ok;
                case EventType.WaveClear:
                    if (!reader.TryReadU8(out entry.Wave)) return DecodeFailure.Truncated;
                    if (!reader.TryReadU32(out entry.ElapsedMs)) return DecodeFailure.Truncated;
                    return DecodeFailure.Ok;
                case EventType.PlayerDowned:
                    if (!reader.TryReadU16(out entry.SubjectId)) return DecodeFailure.Truncated;
                    return DecodeFailure.Ok;
                case EventType.ReviveProgress:
                    if (!reader.TryReadU16(out entry.TargetId)) return DecodeFailure.Truncated;
                    if (!reader.TryReadU16(out entry.SubjectId)) return DecodeFailure.Truncated;
                    if (!reader.TryReadU16(out entry.Ratio255)) return DecodeFailure.Truncated;
                    return DecodeFailure.Ok;
                case EventType.ReviveDone:
                    if (!reader.TryReadU16(out entry.TargetId)) return DecodeFailure.Truncated;
                    if (!reader.TryReadU16(out entry.SubjectId)) return DecodeFailure.Truncated;
                    return DecodeFailure.Ok;
                case EventType.RageActivated:
                    if (!reader.TryReadU16(out entry.SubjectId)) return DecodeFailure.Truncated;
                    if (!reader.TryReadU16(out entry.DurationMs)) return DecodeFailure.Truncated;
                    return DecodeFailure.Ok;
                case EventType.MatchEnded:
                    if (!reader.TryReadU8(out entry.Reason)) return DecodeFailure.Truncated;
                    if (!reader.TryReadU8(out entry.Wave)) return DecodeFailure.Truncated;
                    if (!reader.TryReadU32(out entry.ElapsedMs)) return DecodeFailure.Truncated;
                    return DecodeFailure.Ok;
                case EventType.PhaseChange:
                    if (!reader.TryReadU8(out entry.Phase)) return DecodeFailure.Truncated;
                    if (!reader.TryReadU8(out entry.Wave)) return DecodeFailure.Truncated;
                    if (!reader.TryReadU16(out entry.IntermissionMs)) return DecodeFailure.Truncated;
                    return DecodeFailure.Ok;
                default:
                    return DecodeFailure.UnknownEvent;
            }
        }

    }
}
