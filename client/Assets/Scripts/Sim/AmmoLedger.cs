namespace Ac.Sim
{
    // C06 §5(c)：弹药本地账。未确认开火用定长 FIFO 记录（容量见 MaxPending），零分配。
    public struct AmmoView
    {
        public int Mag;
        public int Reserve;
        public int Slot;
        public int GateMag;
    }

    public sealed class AmmoLedger
    {
        public const int AmmoSlotCount = 3;
        public const int AmmoAckGraceSeq = 20;
        public const int AmmoIdleHealMs = 500;
        private const int MaxPending = 64;

        private readonly ushort[] _pendingSeq = new ushort[MaxPending];
        private int _pendingCount;

        private bool _hasServer;
        private int _serverMag;
        private int _slot;
        private int _shown;
        private int _lastLocalShotMs;
        private bool _hasShotMs;
        private ushort _ackedSeq;

        public int RejectedTotal { get; private set; }
        public int PendingShots { get { return _pendingCount; } }

        public void NoteLocalShot(ushort seq, int slot, int nowMs)
        {
            if (_pendingCount == MaxPending) DropOldest();
            _pendingSeq[_pendingCount] = seq;
            _pendingCount += 1;
            _lastLocalShotMs = nowMs;
            _hasShotMs = true;
        }

        // 只推进：ack 序号只会变大（回绕由 CommandBuffer.SeqDiff 负责）
        public void NoteServerAck(ushort ackedSeq)
        {
            if (CommandBuffer.SeqDiff(ackedSeq, _ackedSeq) > 0) _ackedSeq = ackedSeq;
        }

        private int Optimistic(int serverMag)
        {
            var value = serverMag - _pendingCount;
            return value < 0 ? 0 : value;
        }

        public AmmoView Reconcile(int serverMag, int serverReserve, int slot, int nowMs)
        {
            if (serverMag < 0) serverMag = 0;
            if (!_hasServer)
            {
                _hasServer = true;
                _shown = Optimistic(serverMag);
            }
            else
            {
                // 消账：权威每下降 1 发消掉最早的一条未确认开火（FIFO）
                if (serverMag < _serverMag) ConsumeOldest(_serverMag - serverMag);
                ExpireStale();
                if (serverMag > _serverMag || slot != _slot)
                {
                    _shown = Optimistic(serverMag);   // 周期重置：换弹/补弹或换槽回到乐观值
                }
                else
                {
                    var lower = Optimistic(serverMag);
                    if (lower < _shown) _shown = lower;   // 只降不升
                    if (_pendingCount == 0 && _hasShotMs && nowMs - _lastLocalShotMs >= AmmoIdleHealMs) _shown = serverMag;
                }
            }

            _serverMag = serverMag;
            _slot = slot;

            // §5(c)：乐观值按消账/过期之后的未确认条数算（消账会立刻改变它）
            var optimistic = Optimistic(serverMag);

            var view = default(AmmoView);
            view.Mag = _shown;
            view.Reserve = serverReserve;
            view.Slot = slot;
            view.GateMag = optimistic < _shown ? optimistic : _shown;
            return view;
        }

        public void Reset()
        {
            _pendingCount = 0;
            _hasServer = false;
            _hasShotMs = false;
            _serverMag = 0;
            _shown = 0;
            _slot = 0;
            _ackedSeq = 0;
            RejectedTotal = 0;
        }

        // §5(c) 过期判定：AckedSeq - AckGraceSeq > seq 仍无对应权威下降 → 计入 RejectedTotal，不回弹显示值。
        private void ExpireStale()
        {
            var index = 0;
            while (index < _pendingCount)
            {
                if (CommandBuffer.SeqDiff(_ackedSeq, _pendingSeq[index]) > AmmoAckGraceSeq)
                {
                    RejectedTotal += 1;
                    RemoveAt(index);
                    continue;
                }
                index += 1;
            }
        }

        private void ConsumeOldest(int count)
        {
            for (var i = 0; i < count && _pendingCount > 0; i++) DropOldest();
        }

        private void DropOldest()
        {
            RemoveAt(0);
        }

        private void RemoveAt(int index)
        {
            for (var i = index; i < _pendingCount - 1; i++) _pendingSeq[i] = _pendingSeq[i + 1];
            _pendingCount -= 1;
        }
    }
}
