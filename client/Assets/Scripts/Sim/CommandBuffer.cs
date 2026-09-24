namespace Ac.Sim
{
    // C06 §5(a)：未确认命令环形缓冲。容量固定 128，构造期一次分配，Push/AckUpTo 零托管分配。
    public sealed class CommandBuffer
    {
        public const int Capacity = 128;

        private readonly StepCommand[] _slots = new StepCommand[Capacity];
        private int _head;    // 最旧一条
        private int _count;

        public int Size { get { return _count; } }
        public int OverflowCount { get; private set; }

        // §5(a)：ushort 回绕比较；> 0 表示 a 比 b 新。
        public static int SeqDiff(ushort a, ushort b)
        {
            var diff = (a - b) & 0xFFFF;
            return diff >= 32768 ? diff - 65536 : diff;
        }

        public void Push(in StepCommand command)
        {
            if (_count == Capacity)
            {
                _head = (_head + 1) % Capacity;   // 满时丢最旧
                _count -= 1;
                OverflowCount += 1;
            }
            _slots[(_head + _count) % Capacity] = command;
            _count += 1;
        }

        public StepCommand At(int index)
        {
            return _slots[(_head + index) % Capacity];
        }

        // §5(a)：丢弃所有满足 SeqDiff(seq, slot.Seq) >= 0 的最旧条目，返回丢弃条数。
        public int AckUpTo(ushort ackedSeq)
        {
            var dropped = 0;
            while (_count > 0 && SeqDiff(ackedSeq, _slots[_head].Seq) >= 0)
            {
                _head = (_head + 1) % Capacity;
                _count -= 1;
                dropped += 1;
            }
            return dropped;
        }

        public void Reset()
        {
            _head = 0;
            _count = 0;
        }
    }
}
