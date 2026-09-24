using Ac.Core;
using Ac.Sim;

namespace Ac.Tests
{
    // C02 §9 的 Vec3：全是静态纯函数，只用 + - * / sqrt，零长度归一化返回零向量（与 v1 math.ts 同语义）。
    internal static class Vec3Suite
    {
        public static void Register()
        {
            SelfTest.Add("vec3.ops", ChecksOps);
        }

        private static void ChecksOps()
        {
            var a = new Vec3(3.0, 4.0, 0.0);
            var b = new Vec3(-1.0, 2.0, 0.5);

            var sum = Vec3.Add(a, b);
            SelfTest.BitEqual(2.0, sum.X);
            SelfTest.BitEqual(6.0, sum.Y);
            SelfTest.BitEqual(0.5, sum.Z);

            var difference = Vec3.Sub(a, b);
            SelfTest.BitEqual(4.0, difference.X);
            SelfTest.BitEqual(2.0, difference.Y);
            SelfTest.BitEqual(-0.5, difference.Z);

            SelfTest.BitEqual(6.0, Vec3.Scale(a, 2.0).X);
            SelfTest.BitEqual(6.0, Vec3.AddScaled(a, b, -3.0).X);  // 3 + (-1 * -3)
            SelfTest.BitEqual(5.0, Vec3.Dot(a, b));                // -3 + 8 + 0

            SelfTest.BitEqual(25.0, Vec3.LengthSq(a));
            SelfTest.BitEqual(5.0, Vec3.Length(a));

            // 逐分量除法（不是乘 1/len）：结果必须逐位等于 3/5 与 4/5。
            var unit = Vec3.Normalize(a);
            SelfTest.BitEqual(3.0 / 5.0, unit.X);
            SelfTest.BitEqual(4.0 / 5.0, unit.Y);
            SelfTest.BitEqual(0.0, unit.Z);

            var zero = new Vec3(0.0, 0.0, 0.0);
            var zeroUnit = Vec3.Normalize(zero);
            SelfTest.BitEqual(0.0, zeroUnit.X);
            SelfTest.BitEqual(0.0, zeroUnit.Y);
            SelfTest.BitEqual(0.0, zeroUnit.Z);
            SelfTest.BitEqual(0.0, Vec3.Length(zero));
        }
    }
}
