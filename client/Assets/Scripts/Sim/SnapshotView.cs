using System;

namespace Ac.Sim
{
    // C04 §5.2 的实体镜像记录：字段与 S03 §5.3 / SnapshotCodec.EntityRecord 的 15 字节记录一一对应
    // （kindFlags 位序见 EntityRecord）。这里在 Ac.Sim 侧自持一份，因为 Ac.Sim 不能引用 Ac.Net。
    public struct FrameEntity
    {
        public ushort Id;
        public byte KindFlags;
        public short XCm;
        public short YCm;
        public short ZCm;
        public ushort YawUnits;
        public ushort PitchUnits;
        public byte HpRatioUnits;
        public byte State;
    }

    // 一帧快照的镜像（调用方或环形槽位持有；数组容量固定，避免运行期分配）。
    public struct SnapshotFrame
    {
        public uint Tick;
        public uint ServerTimeMs;
        public ushort LastAckedSeq;
        public uint BaselineTick;
        public int EntityCount;
        public int RemovedCount;
        public FrameEntity[] Entities;
        public ushort[] RemovedIds;
    }

    // C03 §3 / C04 §3：只读快照镜像。
    //  · tick 单调过滤（§5.2）：tick <= appliedTick 整帧丢弃，不改变任何状态；
    //  · baselineTick == 0 是全量帧，先清空镜像；baselineTick 超过本端已知 tick 记 baselineMismatch 并丢弃；
    //  · 对外只有遍历与查询，没有任何写回模拟状态的入口（§6 第 4 条）。
    public sealed class SnapshotView
    {
        public const int HistoryFrames = 6;          // §5.1 SnapshotHistory
        public const int MaxRecordsPerFrame = 128;   // §5.2 count 上限（与 SnapshotCodec 同值）
        public const int MaxRemovedPerFrame = 255;   // §5.2 removedCount 上限
        public const int MaxEntities = Quantize.MaxEntityId;

        public delegate void EntityVisitor(ushort id, in FrameEntity entity);

        private readonly FrameEntity[][] _ringEntities = new FrameEntity[HistoryFrames][];
        private readonly ushort[][] _ringRemoved = new ushort[HistoryFrames][];
        private readonly uint[] _ringTick = new uint[HistoryFrames];
        private readonly uint[] _ringServerTimeMs = new uint[HistoryFrames];
        private readonly ushort[] _ringLastAckedSeq = new ushort[HistoryFrames];
        private readonly uint[] _ringBaselineTick = new uint[HistoryFrames];
        private readonly int[] _ringEntityCount = new int[HistoryFrames];
        private readonly int[] _ringRemovedCount = new int[HistoryFrames];

        private readonly FrameEntity[] _entities = new FrameEntity[MaxEntities + 1];
        private readonly bool[] _present = new bool[MaxEntities + 1];
        private readonly ushort[] _visibleIds = new ushort[MaxEntities + 1];
        private int _visibleCount;

        public SnapshotView()
        {
            for (var i = 0; i < HistoryFrames; i++)
            {
                _ringEntities[i] = new FrameEntity[MaxRecordsPerFrame];
                _ringRemoved[i] = new ushort[MaxRemovedPerFrame];
            }
        }

        public uint AppliedTick { get; private set; }
        public ushort LocalPlayerId { get; private set; }
        private uint _serverTimeMs;
        public int AppliedFrames { get; private set; }
        public int BaselineMismatch { get; private set; }
        public int InvalidSnapshots { get; private set; }
        public int FrameCount { get; private set; }
        public int VisibleCount { get { return _visibleCount; } }

        public uint GetServerTimeMs() { return _serverTimeMs; }

        public void SetLocalPlayer(ushort id) { LocalPlayerId = id; }

        // §5.2：解码失败（或帧内字段越界）→ 丢弃并计 invalidSnapshots，已应用状态原样保留。
        public void NoteDecodeFailure() { InvalidSnapshots += 1; }

        public bool ApplyFrame(SnapshotFrame frame)
        {
            // 单调与基线判定必须排在计数校验之前：§5.2 要求旧帧「不更新任何状态」，
            // 旧帧哪怕是坏帧也不该把 invalidSnapshots 推上去。
            if (frame.Tick <= AppliedTick) return false;                       // 旧帧与重复帧：整帧丢弃，不改状态
            if (frame.BaselineTick > frame.Tick) { BaselineMismatch += 1; return false; }
            if (frame.BaselineTick != 0 && frame.BaselineTick > AppliedTick) { BaselineMismatch += 1; return false; }
            if (frame.Entities == null || frame.RemovedIds == null) { InvalidSnapshots += 1; return false; }
            if (frame.EntityCount < 0 || frame.EntityCount > MaxRecordsPerFrame) { InvalidSnapshots += 1; return false; }
            if (frame.RemovedCount < 0 || frame.RemovedCount > MaxRemovedPerFrame) { InvalidSnapshots += 1; return false; }

            if (frame.BaselineTick == 0) ClearMirror();                        // 全量帧

            for (var i = 0; i < frame.RemovedCount; i++) RemoveEntity(frame.RemovedIds[i]);
            for (var i = 0; i < frame.EntityCount; i++) StoreEntity(frame.Entities[i]);

            PushRing(frame);
            AppliedTick = frame.Tick;
            _serverTimeMs = frame.ServerTimeMs;
            AppliedFrames += 1;
            return true;
        }

        // age 0 = 最新帧；越界返回 false。
        public bool TryGetFrame(int age, out SnapshotFrame frame)
        {
            frame = default(SnapshotFrame);
            if (age < 0 || age >= FrameCount) return false;
            var slot = RingSlot(age);
            frame.Tick = _ringTick[slot];
            frame.ServerTimeMs = _ringServerTimeMs[slot];
            frame.LastAckedSeq = _ringLastAckedSeq[slot];
            frame.BaselineTick = _ringBaselineTick[slot];
            frame.EntityCount = _ringEntityCount[slot];
            frame.RemovedCount = _ringRemovedCount[slot];
            frame.Entities = _ringEntities[slot];
            frame.RemovedIds = _ringRemoved[slot];
            return true;
        }

        // 升序时间表（索引 0 = 最旧），写入调用方缓冲；返回条数。供 §5.3 的 FindBracket 使用。
        public int CopyServerTimes(uint[] buffer)
        {
            var count = FrameCount;
            if (buffer == null) return count;
            if (buffer.Length < count) count = buffer.Length;
            for (var i = 0; i < count; i++) buffer[i] = _ringServerTimeMs[RingSlot(FrameCount - 1 - i)];
            return count;
        }

        // 镜像才是"当前权威状态"：差分帧只带**变化过的**记录，站着不动的实体在最新帧里根本没有条目，
        // 而走这条查询的正是"本地玩家权威姿态"（每次和解都要用）。只查最新帧会让 Reconciler.cs:61 的
        // authority.Found 早退：ack 裁剪、命令重放、误差平滑全部静默停摆，直到本机状态再次变化时
        // 一次性重放 ~40 条命令（硬纠正与橡皮筋的来源）。逐帧历史语义仍归 TryGetEntityInFrame(age, ...)。
        public bool TryGetEntity(ushort id, out FrameEntity entity)
        {
            entity = default(FrameEntity);
            if (id > MaxEntities || !_present[id]) return false;
            entity = _entities[id];
            return true;
        }

        public bool TryGetEntityInFrame(int age, ushort id, out FrameEntity entity)
        {
            entity = default(FrameEntity);
            if (age < 0 || age >= FrameCount) return false;
            var slot = RingSlot(age);
            var count = _ringEntityCount[slot];
            var entities = _ringEntities[slot];
            for (var i = 0; i < count; i++)
            {
                if (entities[i].Id != id) continue;
                entity = entities[i];
                return true;
            }
            return false;
        }

        // §9 的本地权威姿态：最新帧里本地玩家的记录（无预测值时的兜底来源）。
        public bool TryGetLocalAuthority(out FrameEntity entity)
        {
            entity = default(FrameEntity);
            if (LocalPlayerId == 0) return false;
            return TryGetEntity(LocalPlayerId, out entity);
        }

        // 只读遍历最新帧的可见实体（不含移除列表命中的 id——它们已从镜像删除）。
        public void ForEachVisible(EntityVisitor visit)
        {
            if (visit == null) return;
            for (var i = 0; i < _visibleCount; i++)
            {
                var id = _visibleIds[i];
                visit(id, in _entities[id]);
            }
        }

        private int RingSlot(int age) { return (_newestSlot - age + HistoryFrames * 2) % HistoryFrames; }

        private int _newestSlot;

        private void PushRing(SnapshotFrame frame)
        {
            _newestSlot = (_newestSlot + 1) % HistoryFrames;
            if (FrameCount < HistoryFrames) FrameCount += 1;
            var slot = _newestSlot;
            _ringTick[slot] = frame.Tick;
            _ringServerTimeMs[slot] = frame.ServerTimeMs;
            _ringLastAckedSeq[slot] = frame.LastAckedSeq;
            _ringBaselineTick[slot] = frame.BaselineTick;
            _ringEntityCount[slot] = frame.EntityCount;
            _ringRemovedCount[slot] = frame.RemovedCount;
            Array.Copy(frame.Entities, _ringEntities[slot], frame.EntityCount);
            Array.Copy(frame.RemovedIds, _ringRemoved[slot], frame.RemovedCount);
        }

        private void StoreEntity(in FrameEntity entity)
        {
            if (entity.Id < 1 || entity.Id > MaxEntities) return;   // 越界 id 已在解码侧拦掉，这里只做防御
            if (!_present[entity.Id])
            {
                _present[entity.Id] = true;
                _visibleIds[_visibleCount] = entity.Id;
                _visibleCount += 1;
            }
            _entities[entity.Id] = entity;
        }

        private void RemoveEntity(ushort id)
        {
            if (id < 1 || id > MaxEntities) return;
            if (!_present[id]) return;
            _present[id] = false;
            for (var i = 0; i < _visibleCount; i++)
            {
                if (_visibleIds[i] != id) continue;
                _visibleCount -= 1;
                _visibleIds[i] = _visibleIds[_visibleCount];
                break;
            }
        }

        private void ClearMirror()
        {
            for (var i = 0; i < _visibleCount; i++) _present[_visibleIds[i]] = false;
            _visibleCount = 0;
        }
    }
}
