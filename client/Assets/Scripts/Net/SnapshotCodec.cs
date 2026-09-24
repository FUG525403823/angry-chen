using Ac.Sim;

namespace Ac.Net
{
    // C02 §5.2 的 15 字节实体记录。kindFlags 的位序：
    //   bit0-1 kind（player=0 sheep=1 projectile=2 pickup=3）
    //   bit2 downed / bit3 rageMode / bit4 reloading / bit5 charging / bit6 fading / bit7 idle
    public struct EntityRecord
    {
        public const byte KindMask = 0x03;
        public const byte DownedFlag = 1 << 2;
        public const byte RageModeFlag = 1 << 3;
        public const byte ReloadingFlag = 1 << 4;
        public const byte ChargingFlag = 1 << 5;
        public const byte FadingFlag = 1 << 6;
        public const byte IdleFlag = 1 << 7;

        public ushort Id;
        public byte KindFlags;
        public short XCm;
        public short YCm;
        public short ZCm;
        public ushort YawUnits;
        public ushort PitchUnits;
        public byte HpRatioUnits;
        public byte State;

        public byte Kind { get { return (byte)(KindFlags & KindMask); } }
        public bool IsDowned { get { return (KindFlags & DownedFlag) != 0; } }
        public bool IsRageMode { get { return (KindFlags & RageModeFlag) != 0; } }
        public bool IsReloading { get { return (KindFlags & ReloadingFlag) != 0; } }
        public bool IsCharging { get { return (KindFlags & ChargingFlag) != 0; } }
        public bool IsFading { get { return (KindFlags & FadingFlag) != 0; } }
        public bool IsIdle { get { return (KindFlags & IdleFlag) != 0; } }
    }

    public struct SnapshotPayload
    {
        public uint Tick;
        public uint ServerTimeMs;
        public ushort LastAckedSeq;
        public uint BaselineTick;
        public EntityRecord[] Records;
        public ushort[] RemovedIds;
        public EventEntry[] Events;
        public int DroppedDuplicates;

        // baselineTick == 0 表示全量快照：接收侧据此清空镜像。
        public bool IsFull { get { return BaselineTick == 0; } }
    }

    // C02 §5.2 / S03 §5.3 差分快照（type=5）。
    public static class SnapshotCodec
    {
        public const int MaxRecordsPerFrame = 128;
        public const int RecordSize = 15;
        public const int PayloadHeaderSize = 15;

        public static DecodeFailure Decode(byte[] payload, out SnapshotPayload snapshot)
        {
            return Decode(payload, null, out snapshot);
        }

        // 内嵌事件块与 type=6 共用幂等键，tracker 由接收侧跨帧持有（S03 §5.4）。
        public static DecodeFailure Decode(byte[] payload, EventIdTracker tracker, out SnapshotPayload snapshot)
        {
            snapshot = default(SnapshotPayload);
            var reader = new PacketReader(payload);
            if (!reader.TryReadU32(out snapshot.Tick)) return DecodeFailure.Truncated;
            if (!reader.TryReadU32(out snapshot.ServerTimeMs)) return DecodeFailure.Truncated;
            if (!reader.TryReadU16(out snapshot.LastAckedSeq)) return DecodeFailure.Truncated;
            if (!reader.TryReadU32(out snapshot.BaselineTick)) return DecodeFailure.Truncated;

            byte recordCount;
            if (!reader.TryReadU8(out recordCount)) return DecodeFailure.Truncated;
            if (recordCount > MaxRecordsPerFrame) return DecodeFailure.BadValue;

            var records = new EntityRecord[recordCount];
            for (var i = 0; i < recordCount; i++)
            {
                EntityRecord record;
                if (!reader.TryReadU16(out record.Id)) return DecodeFailure.Truncated;
                if (record.Id < 1 || record.Id > Quantize.MaxEntityId) return DecodeFailure.BadValue;  // 上限只有一份（§5.3）
                if (!reader.TryReadU8(out record.KindFlags)) return DecodeFailure.Truncated;
                if (!reader.TryReadI16(out record.XCm)) return DecodeFailure.Truncated;
                if (!reader.TryReadI16(out record.YCm)) return DecodeFailure.Truncated;
                if (!reader.TryReadI16(out record.ZCm)) return DecodeFailure.Truncated;
                if (!reader.TryReadU16(out record.YawUnits)) return DecodeFailure.Truncated;
                if (!reader.TryReadU16(out record.PitchUnits)) return DecodeFailure.Truncated;
                if (!reader.TryReadU8(out record.HpRatioUnits)) return DecodeFailure.Truncated;
                if (!reader.TryReadU8(out record.State)) return DecodeFailure.Truncated;
                records[i] = record;
            }

            byte removedCount;
            if (!reader.TryReadU8(out removedCount)) return DecodeFailure.Truncated;
            var removed = new ushort[removedCount];
            for (var i = 0; i < removedCount; i++)
            {
                if (!reader.TryReadU16(out removed[i])) return DecodeFailure.Truncated;
                if (removed[i] < 1 || removed[i] > Quantize.MaxEntityId) return DecodeFailure.BadValue;
                if (i > 0 && removed[i] <= removed[i - 1]) return DecodeFailure.BadValue;  // 升序且不重复
            }

            byte eventCount;
            if (!reader.TryReadU8(out eventCount)) return DecodeFailure.Truncated;
            if (eventCount > EventCodec.MaxEventsPerFrame) return DecodeFailure.BadValue;

            EventEntry[] events;
            int dropped;
            var failure = EventCodec.DecodeEntries(reader, eventCount, tracker, out events, out dropped);
            if (failure != DecodeFailure.Ok) return failure;
            if (reader.Remaining != 0) return DecodeFailure.BadLength;  // 事件块恒在载荷末尾

            snapshot.Records = records;
            snapshot.RemovedIds = removed;
            snapshot.Events = events;
            snapshot.DroppedDuplicates = dropped;
            return DecodeFailure.Ok;
        }
    }
}
