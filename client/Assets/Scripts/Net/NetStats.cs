using System;

namespace Ac.Net
{
    // C03 §5.4 的只读快照：面板、调试命令与自检的唯一数据面。
    public struct NetStatsSnapshot
    {
        public double RttMs;
        public double RttP99Ms;
        public int PacketLossPermille;
        public double BytesInPerSec;
        public double BytesOutPerSec;
        public int Retransmits;
        public int Duplicates;
        public int DroppedSnapshots;
        public int FragmentsReassembled;
        public int InvalidPackets;
        public double RtoMs;
        public ConnectionState State;
    }

    // C03 §5.4：1s 窗口、512 槽环形缓冲、p99 用就地插入排序取分位。
    public sealed class NetStats
    {
        public const int WindowMs = 1000;
        public const int P99Slots = 512;

        private readonly double[] _rttRing = new double[P99Slots];
        private readonly ushort[] _seqMaxIn = new ushort[(int)PacketType.MatchState + 1];
        private readonly ushort[] _seqBaseIn = new ushort[(int)PacketType.MatchState + 1];

        private int _rttCount;
        private int _rttNext;
        private readonly bool[] _seqSeen = new bool[(int)PacketType.MatchState + 1];
        private int _expectedInWindow;
        private int _receivedInWindow;
        private int _bytesInWindow;
        private int _bytesOutWindow;
        private double _windowStartMs;
        private bool _windowOpen;

        public double RttMs { get; private set; }
        public int PacketLossPermille { get; private set; }
        public double BytesInPerSec { get; private set; }
        public double BytesOutPerSec { get; private set; }
        public double RtoMs { get; private set; }
        public ConnectionState State { get; private set; }

        public int Retransmits { get; private set; }
        public int Duplicates { get; private set; }
        public int DroppedSnapshots { get; private set; }
        public int FragmentsReassembled { get; private set; }
        public int InvalidPackets { get; private set; }

        public void Reset(double nowMs)
        {
            Array.Clear(_rttRing, 0, _rttRing.Length);
            Array.Clear(_seqMaxIn, 0, _seqMaxIn.Length);
            Array.Clear(_seqBaseIn, 0, _seqBaseIn.Length);
            Array.Clear(_seqSeen, 0, _seqSeen.Length);
            _expectedInWindow = 0;
            _rttCount = 0;
            _rttNext = 0;
            _receivedInWindow = 0;
            _bytesInWindow = 0;
            _bytesOutWindow = 0;
            _windowStartMs = nowMs;
            _windowOpen = true;
            RttMs = 0.0;
            PacketLossPermille = 0;
            BytesInPerSec = 0.0;
            BytesOutPerSec = 0.0;
            RtoMs = 0.0;
            State = ConnectionState.Disconnected;
            Retransmits = 0;
            Duplicates = 0;
            DroppedSnapshots = 0;
            FragmentsReassembled = 0;
            InvalidPackets = 0;
        }

        // §5.4：rttMs 保留 0.01ms（两位小数）。
        public void OnRttSample(double rttMs)
        {
            RttMs = Math.Round(rttMs, 2);
            _rttRing[_rttNext] = RttMs;
            _rttNext = (_rttNext + 1) % P99Slots;
            if (_rttCount < P99Slots) _rttCount += 1;
        }

        public void OnInbound(PacketType type, ushort seq, int bytes)
        {
            var index = (int)type;
            if (index >= 0 && index < _seqMaxIn.Length)
            {
                // 期望包数 = 每通道相邻到达序号差之和：第一包只建立基准，不计入期望
                //（否则窗口首包会被当成丢包；序号回绕由 ushort 减法自然处理）。
                if (_seqSeen[index]) _expectedInWindow += (ushort)(seq - _seqBaseIn[index]);
                _seqSeen[index] = true;
                _seqBaseIn[index] = seq;
                _seqMaxIn[index] = seq;
            }
            _receivedInWindow += 1;
            _bytesInWindow += bytes;
        }

        public void OnOutbound(int bytes)
        {
            _bytesOutWindow += bytes;
        }

        public void OnRetransmit() { Retransmits += 1; }
        public void OnDuplicate() { Duplicates += 1; }
        public void OnDroppedSnapshot() { DroppedSnapshots += 1; }
        public void OnFragmentReassembled() { FragmentsReassembled += 1; }
        public void OnInvalidPacket() { InvalidPackets += 1; }

        public void SetRtoMs(double rtoMs) { RtoMs = rtoMs; }
        public void SetState(ConnectionState state) { State = state; }

        // 窗口滚动：重算丢包率与上下行字节速率。期望包数由每通道序号推定（§5.4）。
        public void Tick(double nowMs)
        {
            if (!_windowOpen)
            {
                _windowStartMs = nowMs;
                _windowOpen = true;
                return;
            }
            var elapsed = nowMs - _windowStartMs;
            if (elapsed < WindowMs) return;

            var expected = _expectedInWindow;
            _expectedInWindow = 0;
            if (expected > 0)
            {
                var lost = expected - _receivedInWindow;
                if (lost < 0) lost = 0;
                PacketLossPermille = (int)((long)lost * 1000 / expected);
            }
            else
            {
                PacketLossPermille = 0;
            }
            var seconds = elapsed / 1000.0;
            BytesInPerSec = Math.Round(_bytesInWindow / seconds, 1);
            BytesOutPerSec = Math.Round(_bytesOutWindow / seconds, 1);
            _receivedInWindow = 0;
            _bytesInWindow = 0;
            _bytesOutWindow = 0;
            _windowStartMs = nowMs;
        }

        // §5.4：就地插入排序后取索引 min(n-1, max(0, ceil(0.99 * n) - 1))，保留 2 位小数。
        public double RttP99Ms()
        {
            if (_rttCount == 0) return 0.0;
            var sorted = new double[_rttCount];
            Array.Copy(_rttRing, sorted, _rttCount);
            for (var i = 1; i < sorted.Length; i++)
            {
                var value = sorted[i];
                var j = i - 1;
                while (j >= 0 && sorted[j] > value)
                {
                    sorted[j + 1] = sorted[j];
                    j -= 1;
                }
                sorted[j + 1] = value;
            }
            var index = (int)Math.Ceiling(0.99 * sorted.Length) - 1;
            if (index < 0) index = 0;
            if (index > sorted.Length - 1) index = sorted.Length - 1;
            return Math.Round(sorted[index], 2);
        }

        public int RttSampleCount { get { return _rttCount; } }

        public NetStatsSnapshot Snapshot()
        {
            var snapshot = new NetStatsSnapshot();
            snapshot.RttMs = RttMs;
            snapshot.RttP99Ms = RttP99Ms();
            snapshot.PacketLossPermille = PacketLossPermille;
            snapshot.BytesInPerSec = BytesInPerSec;
            snapshot.BytesOutPerSec = BytesOutPerSec;
            snapshot.Retransmits = Retransmits;
            snapshot.Duplicates = Duplicates;
            snapshot.DroppedSnapshots = DroppedSnapshots;
            snapshot.FragmentsReassembled = FragmentsReassembled;
            snapshot.InvalidPackets = InvalidPackets;
            snapshot.RtoMs = RtoMs;
            snapshot.State = State;
            return snapshot;
        }
    }
}
