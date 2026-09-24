using System;
using System.Collections.Generic;

namespace Ac.Net
{
    // C03 §5.3 / S04 §5.3 逐字同义的每通道可靠性状态：发送侧 sendMsgId + 重传表，接收侧 ackBase/ackBits + 去重。
    public sealed class ReliabilityChannel
    {
        public const int AckBitsWidth = 32;
        public const int SeqModulo = 65536;
        public const int MaxRetransmits = 5;

        // 200 * 1.5^(n-1) 取整；表长恒等于 MaxRetransmits（第 6 次重传不存在）。禁 pow（ADR-010），
        // 且与服务端 kRtoTableMs 逐字相同——两侧重传节奏不一致会表现为偶发掉线。
        public static readonly int[] RtoTableMs = { 200, 300, 450, 675, 1000 };

        private sealed class Outstanding
        {
            public uint MsgId;
            public byte[] Datagram;
            public int Attempts;
            public double DueMs;
        }

        private readonly List<Outstanding> _outstanding = new List<Outstanding>();

        public uint SendMsgId = 1;
        public uint AckBase;
        public uint AckBits;

        public int OutstandingCount { get { return _outstanding.Count; } }



        // §5.4 的 rtoMs：最老未确认消息下一次重传的等待间隔（无未确认消息时取首项）。
        public double CurrentRtoMs
        {
            get
            {
                if (_outstanding.Count == 0) return RtoTableMs[0];
                var attempts = _outstanding[0].Attempts;
                return attempts >= RtoTableMs.Length ? RtoTableMs[RtoTableMs.Length - 1] : RtoTableMs[attempts];
            }
        }

        // 收到 msgId = m（从 1 起）：按 S04 §5.3 的位图**意图**实现，但距离一律用有符号差
        //（uint 相减在 m < ackBase 时回绕成巨大正数，「更旧」的分支永远进不去，去重与位图都会失效）。
        public void NoteReceived(uint msgId)
        {
            if (msgId == 0) return;   // 0 不是合法 msgId（S04 的 ackOnReceive 同样先拒 0）：畸形包不得污染去重窗口
            var k = (long)msgId - AckBase;
            if (k > 0)
            {
                AckBase = msgId;
                if (k > AckBitsWidth)
                {
                    // 跳变超过窗口：整张位图作废，且不置位。C# 的 uint 移位按 &31 取模，
                    // 若照搬 S04 伪码，k=33 会落到 bit0、k=34 落到 bit1，凭空捏造「已收到」。
                    AckBits = 0u;
                    return;
                }
                AckBits = k == AckBitsWidth ? 0u : (AckBits << (int)k);
                AckBits |= 1u << (int)(k - 1);
                return;
            }
            var d = -k;
            if (d >= 1 && d <= AckBitsWidth) AckBits |= 1u << (int)(d - 1);
        }

        // 位图已置位即重复：调用方不得二次投递（§5.3）。窗口外（太旧）无法判定，按未见过处理。
        public bool IsDuplicate(uint msgId)
        {
            var k = (long)msgId - AckBase;
            if (k > 0) return false;
            if (k == 0) return AckBase != 0;   // ackBase 自身就是「已收到」；AckBase==0 表示一包未收
            var d = -k;
            if (d <= AckBitsWidth) return (AckBits & (1u << (int)(d - 1))) != 0;
            return false;   // 窗口外（太旧）无法判定，按未见过处理
        }

        public uint NextMsgId()
        {
            var id = SendMsgId;
            SendMsgId = SendMsgId == uint.MaxValue ? 1u : SendMsgId + 1u;
            return id;
        }

        public void Track(uint msgId, byte[] datagram, double nowMs)
        {
            var item = new Outstanding();
            item.MsgId = msgId;
            item.Datagram = datagram;
            item.Attempts = 0;
            item.DueMs = nowMs + RtoTableMs[0];
            _outstanding.Add(item);
        }

        // 对端 ack：bit i 表示 msgId == ackBase - 1 - i 已收到；ackOnly 包同样参与确认。
        public int Acknowledge(uint ackBase, uint ackBits)
        {
            var removed = 0;
            for (var i = _outstanding.Count - 1; i >= 0; i--)
            {
                if (IsAcked(_outstanding[i].MsgId, ackBase, ackBits))
                {
                    _outstanding.RemoveAt(i);
                    removed++;
                }
            }
            return removed;
        }

        // 供 RTT 采样复用：ackOnly 的 KeepAlive 不进重传表，只能靠对端 ack 位图确认往返。
        public static bool IsAcked(uint msgId, uint ackBase, uint ackBits)
        {
            var k = (long)msgId - ackBase;
            if (k == 0) return true;
            if (k > 0) return false;
            var d = -k;
            if (d <= AckBitsWidth) return (ackBits & (1u << (int)(d - 1))) != 0;
            return false;
        }

        // 到期重传：返回本次重传条数。累计重传已达 MaxRetransmits 时置 exhausted（第 6 次重传之前判失联）。
        public int Tick(double nowMs, Action<uint, byte[]> resend, out bool exhausted)
        {
            exhausted = false;
            var resent = 0;
            for (var i = 0; i < _outstanding.Count; i++)
            {
                var item = _outstanding[i];
                if (nowMs < item.DueMs) continue;
                if (item.Attempts >= MaxRetransmits)
                {
                    // 判失联的条目必须出表：留在表里会一直排在最前，把后面所有条目的重传都挡住。
                    _outstanding.RemoveAt(i);
                    i--;
                    exhausted = true;
                    continue;
                }
                if (resend != null) resend(item.MsgId, item.Datagram);
                item.Attempts += 1;
                // 第 5 次重传之后没有第 6 次：下一个到期点仍排在表尾，由下一次 Tick 判失联。
                item.DueMs = nowMs + RtoTableMs[item.Attempts < RtoTableMs.Length ? item.Attempts : RtoTableMs.Length - 1];
                resent++;
            }
            return resent;
        }

        // 重连时只丢未确认表：msgId 继续单调（对端仍按旧窗口去重），已发出的旧消息不再重试。
        public void DropOutstanding()
        {
            _outstanding.Clear();
        }

        public void Reset()
        {
            _outstanding.Clear();
            SendMsgId = 1;
            AckBase = 0;
            AckBits = 0;
        }
    }
}
