using System;

namespace Ac.Sim
{
    // C02 §5.5 / S02 §5.2：mulberry32 与三流派生，逐位复刻 v1 packages/shared/src/rng.ts。
    // 全部状态推进走 uint32 回绕（unchecked），禁止 System.Random 与 UnityEngine.Random。
    public enum RngStream
    {
        Ai = 1,
        Spawn = 2,
        Fx = 3,
    }

    // 引用语义而非结构体：复制一个 Rng 会静默分叉随机序列，那是确定性的隐患。
    public sealed class Rng
    {
        private const uint MulberryStep = 0x6D2B79F5u;
        private const uint SeedMultiplier = 0x9E3779B1u;  // 2654435761
        private const double UnitDivisor = 4294967296.0;  // 2^32

        private uint _a;

        private Rng(uint state)
        {
            _a = state;
        }

        // 派生种子：a0 = (seed * 2654435761 + streamId) mod 2^32。
        public static Rng Create(uint seed, RngStream stream)
        {
            unchecked
            {
                return new Rng(seed * SeedMultiplier + (uint)stream);
            }
        }

        // C02 §9 冻结的流名。
        public static string NameOf(RngStream stream)
        {
            switch (stream)
            {
                case RngStream.Ai: return "ai";
                case RngStream.Spawn: return "spawn";
                case RngStream.Fx: return "fx";
                default: throw new ArgumentOutOfRangeException("stream");
            }
        }

        // 供对拍的派生种子锚点（S02 §5.5 的实测值）。
        public uint State { get { return _a; } }

        public uint NextU32()
        {
            unchecked
            {
                _a = _a + MulberryStep;
                var t = _a;
                t = (t ^ (t >> 15)) * (t | 1u);
                t ^= t + (t ^ (t >> 7)) * (t | 61u);
                return t ^ (t >> 14);
            }
        }

        public double NextDouble()
        {
            return NextU32() / UnitDivisor;
        }

        public double Range(double lo, double hi)
        {
            return lo + (hi - lo) * NextDouble();
        }

        public int Int(int loInclusive, int hiInclusive)
        {
            if (hiInclusive < loInclusive) return loInclusive;  // 不消耗随机数
            var span = (double)hiInclusive - loInclusive + 1.0;
            return loInclusive + (int)Math.Floor(NextDouble() * span);
        }
    }
}
