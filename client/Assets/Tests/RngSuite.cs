using System;
using Ac.Core;
using Ac.Sim;

namespace Ac.Tests
{
    // C02 §5.5 / S02 §5.5 的位级锚点：三流派生种子、前 3 个 double 的 IEEE754 原始位、
    // v1 packages/shared/src/rng.ts 在 seed=42 下前 8 个值的原始位、rngInt 序列与区间语义。
    internal static class RngSuite
    {
        public static void Register()
        {
            SelfTest.Add("rng.streams", ChecksStreams);
        }

        private static void ChecksStreams()
        {
            SelfTest.Equal(0xA7689733L, Rng.Create(1234u, RngStream.Ai).State);
            SelfTest.Equal(0xA7689734L, Rng.Create(1234u, RngStream.Spawn).State);
            SelfTest.Equal(0xA7689735L, Rng.Create(1234u, RngStream.Fx).State);

            ChecksBits(1234u, RngStream.Ai, new[] { 0x3FE136AC1DA00000L, 0x3FE621AEFEC00000L, 0x3FEA98B75AC00000L });
            ChecksBits(1234u, RngStream.Spawn, new[] { 0x3FE034F2D0E00000L, 0x3FE9A52769400000L, 0x3FC03B8F8F000000L });
            ChecksBits(1234u, RngStream.Fx, new[] { 0x3FE6611F0D400000L, 0x3FE348F073800000L, 0x3FC777A9BA000000L });

            // v1 packages/shared/src/rng.ts 在 seed=42 下前 8 个值的 IEEE754 原始位（Node DataView 实测抄入）。
            ChecksBits(42u, RngStream.Ai, new[]
            {
                4600873940225622016L, 4598354085454807040L, 4603902952688582656L, 4603805973316894720L,
                4604850366268309504L, 4602727690928128000L, 4584317529039568896L, 4604520284960063488L,
            });
            ChecksBits(42u, RngStream.Spawn, new[]
            {
                4604852540234792960L, 4605858087784415232L, 4598773495126556672L, 4606070906098286592L,
                4602870196372242432L, 4598272466903629824L, 4595309654837297152L, 4603220378116947968L,
            });
            ChecksBits(42u, RngStream.Fx, new[]
            {
                4607055430563135488L, 4596364709443141632L, 4601109479876984832L, 4593282867244564480L,
                4599499154664718336L, 4604320367914254336L, 4598909721338970112L, 4596748569838354432L,
            });

            // rngInt(0, 99) 前 6 次与同一 ai 流的前 6 个 double 同源。
            var ranged = Rng.Create(1234u, RngStream.Ai);
            var expectedInts = new[] { 53, 69, 83, 62, 24, 82 };
            foreach (var expected in expectedInts)
            {
                SelfTest.Equal(expected, ranged.Int(0, 99));
            }

            // 倒置区间返回下界且不消耗随机数。
            var inverted = Rng.Create(7u, RngStream.Spawn);
            var before = inverted.State;
            SelfTest.Equal(5, inverted.Int(5, 4));
            SelfTest.Equal(before, inverted.State);

            // 流名冻结与三流互不干扰：读 fx 不改变 ai 序列。
            SelfTest.Equal("ai", Rng.NameOf(RngStream.Ai));
            SelfTest.Equal("spawn", Rng.NameOf(RngStream.Spawn));
            SelfTest.Equal("fx", Rng.NameOf(RngStream.Fx));

            // 一边消费 fx（表现层用），一边抽 ai，得到的 ai 序列必须与干净跑的 ai 序列逐位相同。
            var interleaved = Rng.Create(9u, RngStream.Ai);
            var fxConsumer = Rng.Create(9u, RngStream.Fx);
            var observed = new long[8];
            for (var i = 0; i < observed.Length; i++)
            {
                observed[i] = BitConverter.DoubleToInt64Bits(interleaved.NextDouble());
                fxConsumer.NextDouble();
            }
            var clean = Rng.Create(9u, RngStream.Ai);
            for (var i = 0; i < observed.Length; i++)
            {
                SelfTest.Equal(observed[i], BitConverter.DoubleToInt64Bits(clean.NextDouble()));
            }

            // 值域与确定性：同一 seed/stream 两次得到相同序列。
            var left = Rng.Create(2024u, RngStream.Spawn);
            var right = Rng.Create(2024u, RngStream.Spawn);
            for (var i = 0; i < 32; i++)
            {
                var value = left.NextDouble();
                SelfTest.True(value >= 0.0 && value < 1.0, "[0, 1)", value.ToString("R"));
                SelfTest.BitEqual(right.NextDouble(), value);
            }
        }

        private static void ChecksBits(uint seed, RngStream stream, long[] expectedBits)
        {
            var rng = Rng.Create(seed, stream);
            foreach (var expected in expectedBits)
            {
                SelfTest.Equal(expected, BitConverter.DoubleToInt64Bits(rng.NextDouble()));
            }
        }

    }
}
