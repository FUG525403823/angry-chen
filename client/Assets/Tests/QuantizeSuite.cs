using System;
using Ac.Boot;
using Ac.Core;
using Ac.Sim;

namespace Ac.Tests
{
    // C02 §5.3/§5.4/§5.6：量化锚点、取整语义、角度封装、往返误差与共享角度表的原始锚点。
    internal static class QuantizeSuite
    {
        // §7：角度往返误差上界 0.0055°。
        private const double AngleToleranceRad = 0.0055 * TrigTable.Pi / 180.0;

        public static void Register()
        {
            SelfTest.Add("quantize.edge", ChecksEdges);
            SelfTest.Add("quantize.rounding", ChecksRoundHalfUp);
            SelfTest.Add("quantize.trig_table_packaged", ChecksPackagedTrigTable);
        }

        // §5.4 的取整是「四舍五入、.5 朝 +∞」（不是对称的 away-from-zero）：负半值向 0 收。
        private static void ChecksRoundHalfUp()
        {
            SelfTest.BitEqual(1.0, Quantize.RoundHalfUp(0.5));
            SelfTest.BitEqual(3.0, Quantize.RoundHalfUp(2.5));
            SelfTest.BitEqual(0.0, Quantize.RoundHalfUp(-0.5));
            SelfTest.BitEqual(-2.0, Quantize.RoundHalfUp(-2.5));
            SelfTest.BitEqual(-1.0, Quantize.RoundHalfUp(-1.5));
            SelfTest.BitEqual(0.0, Quantize.RoundHalfUp(-0.0));

            // 量化入口与 RoundHalfUp 绑在同一口径上：0.005 m = 0.5 cm 进位，-0.005 m 同规则落在 0。
            SelfTest.Equal(1, Quantize.QuantizePosition(0.005));
            SelfTest.Equal(0, Quantize.QuantizePosition(-0.005));
            SelfTest.Equal(1, Quantize.QuantizeRatio(0.5 / 255.0));
            SelfTest.Equal(0, Quantize.QuantizeRatio(0.49 / 255.0));
        }

        private static void ChecksEdges()
        {
            // S02 §5.3 冻结锚点（逐一取自服务端表）。
            SelfTest.Equal(123, Quantize.QuantizePosition(1.2345));
            SelfTest.Equal(0, Quantize.QuantizePosition(-0.005));
            SelfTest.Equal(32767, Quantize.QuantizePosition(500.0));
            SelfTest.Equal(-32768, Quantize.QuantizePosition(-500.0));
            SelfTest.Equal(0, Quantize.QuantizePosition(double.NaN));
            SelfTest.Equal(0, Quantize.QuantizePosition(double.PositiveInfinity));
            SelfTest.Equal(-32768, Quantize.QuantizePosition(-327.68));
            SelfTest.Equal(32767, Quantize.QuantizePosition(327.675));

            SelfTest.Equal(64, Quantize.QuantizeAxis(0.5));
            SelfTest.Equal(-1, Quantize.QuantizeAxis(-0.004));
            SelfTest.Equal(127, Quantize.QuantizeAxis(1.0));
            SelfTest.Equal(-127, Quantize.QuantizeAxis(-1.0));
            SelfTest.Equal(127, Quantize.QuantizeAxis(2.0));
            SelfTest.Equal(0, Quantize.QuantizeAxis(0.0));
            SelfTest.Equal(0, Quantize.QuantizeAxis(double.NaN));

            SelfTest.Equal(128, Quantize.QuantizeRatio(0.5));
            SelfTest.Equal(1, Quantize.QuantizeRatio(0.004));
            SelfTest.Equal(0, Quantize.QuantizeRatio(0.0));
            SelfTest.Equal(0, Quantize.QuantizeRatio(-1.0));
            SelfTest.Equal(255, Quantize.QuantizeRatio(1.0));
            SelfTest.Equal(255, Quantize.QuantizeRatio(2.0));
            SelfTest.Equal(0, Quantize.QuantizeRatio(double.NaN));

            SelfTest.Equal(0, Quantize.QuantizeAngle(0.0));
            SelfTest.Equal(10430, Quantize.QuantizeAngle(1.0));
            SelfTest.Equal(29030, Quantize.QuantizeAngle(-3.5));
            SelfTest.Equal(32768, Quantize.QuantizeAngle(TrigTable.Pi));
            SelfTest.Equal(0, Quantize.QuantizeAngle(double.NaN));
            SelfTest.Equal(0, Quantize.QuantizeAngle(TrigTable.TwoPi));

            // 取整语义：floor(x + 0.5)，禁止 Math.Round 的银行家舍入。
            SelfTest.BitEqual(1.0, Quantize.RoundHalfUp(0.5));
            SelfTest.BitEqual(0.0, Quantize.RoundHalfUp(-0.5));
            SelfTest.BitEqual(2.0, Quantize.RoundHalfUp(1.5));
            SelfTest.BitEqual(-1.0, Quantize.RoundHalfUp(-1.5));

            // 角度封装：(-π, π]，非有限归 0，负的整圈保留 -0.0 符号位。
            SelfTest.BitEqual(0.0, Quantize.WrapAngle(TrigTable.TwoPi));
            SelfTest.BitEqual(-0.0, Quantize.WrapAngle(-TrigTable.TwoPi));
            SelfTest.BitEqual(0.0, Quantize.WrapAngle(double.NaN));
            SelfTest.BitEqual(TrigTable.Pi, Quantize.WrapAngle(TrigTable.Pi));
            SelfTest.True(Math.Abs(Quantize.WrapAngle(3.0 * TrigTable.Pi)) <= TrigTable.Pi, "wrap 落在 (-π, π]",
                Quantize.WrapAngle(3.0 * TrigTable.Pi).ToString("R"));

            // 往返：位置误差 ≤ 0.5cm；角度与血量的量化幂等。
            var positions = new[] { 0.0, 0.001, -0.001, 1.2345, -1.2345, 327.0, -327.0, 327.67, -327.68 };
            foreach (var position in positions)
            {
                var back = Quantize.DequantizePosition(Quantize.QuantizePosition(position));
                SelfTest.True(Math.Abs(back - position) <= 0.005, "位置往返误差 ≤ 0.5cm", Math.Abs(back - position).ToString("R"));
                SelfTest.BitEqual(1.5, Quantize.DequantizePosition(150));
            }

            var angles = new[] { 0.0, 0.001, -0.001, 1.0, -3.5, 3.0, -3.0, TrigTable.Pi, -TrigTable.Pi + 0.001 };
            foreach (var angle in angles)
            {
                var units = Quantize.QuantizeAngle(angle);
                SelfTest.Equal(units, Quantize.QuantizeAngle(Quantize.DequantizeAngle(units)));
                SelfTest.True(Math.Abs(Quantize.WrapAngle(Quantize.DequantizeAngle(units) - angle)) <= AngleToleranceRad,
                    "角度往返误差 ≤ 0.0055°", "越界");
            }

            var ratios = new[] { 0.0, 0.004, 0.5, 0.999, 1.0 };
            foreach (var ratio in ratios)
            {
                var units = Quantize.QuantizeRatio(ratio);
                SelfTest.Equal(units, Quantize.QuantizeRatio(Quantize.DequantizeRatio(units)));
                var back = Quantize.DequantizeRatio(units);
                SelfTest.True(Math.Abs(back - ratio) <= 1.0 / 255.0, "血量往返误差 ≤ 1/255",
                    Math.Abs(back - ratio).ToString("R"));
            }
            SelfTest.Equal(0, Quantize.QuantizeAxis(Quantize.DequantizeAxis(0)));
            SelfTest.Equal(64, Quantize.QuantizeAxis(Quantize.DequantizeAxis(64)));

            ChecksTrigTable();
            ChecksTableFailures();
        }

        // C15 打包（ADR-017）：出包 player 里**没有仓库根**，角度表必须随包，否则预测步进每帧抛
        // `TrigTableException`（实跑 player 日志实测）。这条用例钉三件事：① 资产在（`Resources/trig-table`）；
        // ② 它与仓库那份 `docs/evidence/fixtures/trig-table.json` **逐字节相同**（镜像不许漂移）；
        // ③ 装配入口真的把它装进 `Ac.Sim.TrigTable`（Reset 后从"未装载"起步，装不上就红）。
        private static void ChecksPackagedTrigTable()
        {
            var asset = UnityEngine.Resources.Load<UnityEngine.TextAsset>(TrigTable.ResourceName);
            SelfTest.True(asset != null,
                "Resources/" + TrigTable.ResourceName + " 必须随包（player 里没有仓库根）",
                asset == null ? "资产缺失" : "在");
            if (asset == null) return;
            var repoPath = TrigTable.DefaultPath();
            SelfTest.True(System.IO.File.Exists(repoPath), "仓库那份角度表要能定位到（自检在仓库里跑）", repoPath);
            var repoText = System.IO.File.Exists(repoPath) ? System.IO.File.ReadAllText(repoPath) : null;
            SelfTest.True(repoText != null && repoText == asset.text,
                "随包资产必须与 docs/evidence/fixtures/trig-table.json 逐字节相同（改一份就要重跑镜像）",
                repoText == null ? "仓库文件缺失"
                    : ("资产 " + asset.text.Length + " 字符 / 仓库 " + repoText.Length + " 字符"));

            TrigTable.Reset();
            SelfTest.True(!TrigTable.IsInstalled, "Reset 之后回到未装载", "仍是装载态");
            GameBootstrap.InstallTrigTable();
            SelfTest.True(TrigTable.IsInstalled, "装配入口必须把随包角度表装上（Assets/Resources）", "没装上");
            var table = TrigTable.Shared;
            SelfTest.Equal(1073741824, table.SinRaw(16384));
            SelfTest.Equal(-1073741824, table.SinRaw(49152));
            SelfTest.Equal(8192, table.AtanUnitsRaw(1024));
            SelfTest.Equal(16384, table.AsinUnitsRaw(1024));
        }

        // §5.6：共享角度表的形状（scale / units / 三个数组长度）任一不符都必须抛错，
        // 读错幅值比读不到更危险——表是两侧唯一的对拍产物。
        private static void ChecksTableFailures()
        {
            SelfTest.True(Throws(delegate { TrigTable.Load("{\"scale\":1,\"units\":65536,\"sin\":[],\"atanUnits\":[],\"asinUnits\":[]}"); }),
                "scale 不符即抛错", "未抛错");
            SelfTest.True(Throws(delegate { TrigTable.Load("{\"scale\":1073741824,\"units\":1,\"sin\":[],\"atanUnits\":[],\"asinUnits\":[]}"); }),
                "units 不符即抛错", "未抛错");
            SelfTest.True(Throws(delegate { TrigTable.Load("{\"scale\":1073741824,\"units\":65536,\"sin\":[0],\"atanUnits\":[],\"asinUnits\":[]}"); }),
                "sin 长度不符即抛错", "未抛错");
            SelfTest.True(Throws(delegate { TrigTable.LoadFromFile("docs/evidence/fixtures/no-such-table.json"); }),
                "表文件缺失即抛错", "未抛错");
        }

        private static bool Throws(Action action)
        {
            try
            {
                action();
                return false;
            }
            catch (TrigTableException)
            {
                return true;
            }
        }

        private static void ChecksTrigTable()
        {
            var table = TrigTable.Shared;  // 表缺失即抛错，等于自检失败（§5.6）

            SelfTest.Equal(0, table.SinRaw(0));
            SelfTest.Equal(759250125, table.SinRaw(8192));
            SelfTest.Equal(1073741824, table.SinRaw(16384));
            SelfTest.Equal(0, table.SinRaw(32768));
            SelfTest.Equal(-1073741824, table.SinRaw(49152));
            SelfTest.Equal(0, table.AtanUnitsRaw(0));
            SelfTest.Equal(4836, table.AtanUnitsRaw(512));
            SelfTest.Equal(8192, table.AtanUnitsRaw(1024));
            SelfTest.Equal(0, table.AsinUnitsRaw(0));
            SelfTest.Equal(5461, table.AsinUnitsRaw(512));
            SelfTest.Equal(16384, table.AsinUnitsRaw(1024));

            SelfTest.BitEqual(0.0, table.Sin(0));
            SelfTest.BitEqual(1.0, table.Sin(16384));
            SelfTest.BitEqual(1.0, table.Cos(0));
            SelfTest.BitEqual(table.Sin(16383), table.Cos(65535));  // Cos(u) = Sin(u + 16384) 折回
            SelfTest.BitEqual(table.Sin(0), table.Cos(49152));
            SelfTest.BitEqual(TrigTable.TwoPi / 65536.0, TrigTable.RadiansFromUnits(1));
            SelfTest.Equal(65535, Quantize.QuantizeAngle(TrigTable.RadiansFromUnits(65535)));

            // 八分圆算法：+Z 为 0，+X 为 π/2，-Z 为 π，-X 为 3π/2。
            SelfTest.Equal(0, table.AngleUnitsFromVector(0.0, 1.0));
            SelfTest.Equal(16384, table.AngleUnitsFromVector(1.0, 0.0));
            SelfTest.Equal(32768, table.AngleUnitsFromVector(0.0, -1.0));
            SelfTest.Equal(49152, table.AngleUnitsFromVector(-1.0, 0.0));
            SelfTest.Equal(8192, table.AngleUnitsFromVector(1.0, 1.0));
            SelfTest.Equal(0, table.AngleUnitsFromVector(0.0, 0.0));
            SelfTest.Equal(0, table.AngleUnitsFromVector(double.NaN, 1.0));

            SelfTest.Equal(0, table.AngleUnitsFromRatio(0.0));
            SelfTest.Equal(5461, table.AngleUnitsFromRatio(0.5));
            SelfTest.Equal(16384, table.AngleUnitsFromRatio(1.0));
            SelfTest.Equal(49152, table.AngleUnitsFromRatio(-1.0));
            SelfTest.Equal(0, table.AngleUnitsFromRatio(double.PositiveInfinity));
        }
    }
}
