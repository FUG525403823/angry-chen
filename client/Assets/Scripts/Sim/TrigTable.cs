using System;
using System.Collections.Generic;
using System.IO;
using Ac.Core;

namespace Ac.Sim
{
    public sealed class TrigTableException : Exception
    {
        public TrigTableException(string message) : base(message) { }
    }

    // C02 §5.6 / S02 §5.4：共享角度表——ADR-010 下唯一允许的角度入口。
    // 表与 server/src/core/trig_table.hpp 同源，都来自 docs/evidence/fixtures/trig-table.json；
    // 运行期加载失败即抛错（禁止回落到引擎的超越函数），由调用方拒绝进入对局。
    public sealed class TrigTable
    {
        public const int UnitsPerTurn = 65536;
        public const int QuarterTurn = 16384;
        public const int RatioPoints = 1025;
        public const double SinScale = 1073741824.0;  // 2^30，表幅值
        public const double Pi = 3.14159265358979323846264338327950288;
        public const double TwoPi = 6.28318530717958647692528676655900577;

        private readonly int[] _sin;
        private readonly int[] _atanUnits;
        private readonly int[] _asinUnits;

        private static TrigTable _shared;

        private TrigTable(int[] sin, int[] atanUnits, int[] asinUnits)
        {
            _sin = sin;
            _atanUnits = atanUnits;
            _asinUnits = asinUnits;
        }

        public static TrigTable Shared
        {
            get
            {
                if (_shared == null) _shared = LoadFromFile(DefaultPath());
                return _shared;
            }
        }

        public static TrigTable Load(string json)
        {
            var root = MiniJson.Parse(json);
            if (root.Get("units").AsInt() != UnitsPerTurn)
            {
                throw new TrigTableException("units 必须是 " + UnitsPerTurn);
            }
            if (root.Get("scale").AsInt() != (int)SinScale)
            {
                throw new TrigTableException("scale 必须是 " + (int)SinScale);
            }
            return new TrigTable(
                ToIntArray(root.Get("sin"), UnitsPerTurn, "sin"),
                ToIntArray(root.Get("atanUnits"), RatioPoints, "atanUnits"),
                ToIntArray(root.Get("asinUnits"), RatioPoints, "asinUnits"));
        }

        public static TrigTable LoadFromFile(string path)
        {
            if (string.IsNullOrEmpty(path) || !File.Exists(path))
            {
                throw new TrigTableException("角度表缺失：" + path);
            }
            return Load(File.ReadAllText(path));
        }

        // 角度表是仓库级的对拍产物，编辑器里从工程根往上找；构建产物的打包由 C15 决定。
        public static string DefaultPath()
        {
            return RepoPaths.Locate(Path.Combine("docs", "evidence", "fixtures", "trig-table.json"));
        }

        public double Sin(ushort units)
        {
            return _sin[units] * (1.0 / SinScale);
        }

        public double Cos(ushort units)
        {
            var shifted = ((uint)units + QuarterTurn) & 0xFFFFu;
            return _sin[(int)shifted] * (1.0 / SinScale);
        }

        // 角度单位 -> 弧度。纯函数，不需要表数据。
        public static double RadiansFromUnits(ushort units)
        {
            return units * (TwoPi / 65536.0);
        }

        // 角度单位 -> atan2(dx, dz) 的整数实现（八分圆算法，只用一次除法与一次乘法）。
        public ushort AngleUnitsFromVector(double dx, double dz)
        {
            if (double.IsNaN(dx) || double.IsInfinity(dx) || double.IsNaN(dz) || double.IsInfinity(dz)) return 0;
            var ax = dx < 0.0 ? -dx : dx;
            var az = dz < 0.0 ? -dz : dz;
            var isSwapped = ax > az;
            var num = isSwapped ? az : ax;
            var den = isSwapped ? ax : az;
            var index = den == 0.0 ? 0 : RatioIndex(num / den);
            var a = isSwapped ? QuarterTurn - _atanUnits[index] : _atanUnits[index];
            if (dx < 0.0) a = 65536 - a;
            if (dz < 0.0) a = (32768 - a) & 0xFFFF;
            return (ushort)(a & 0xFFFF);
        }

        // 比例 -> 角度单位：asinUnits 表上取最近点（不插值），±1 取端点，非有限输入归 0。
        public ushort AngleUnitsFromRatio(double ratio)
        {
            if (double.IsNaN(ratio) || double.IsInfinity(ratio)) return 0;
            if (ratio <= -1.0) return (ushort)(65536 - QuarterTurn);
            if (ratio >= 1.0) return QuarterTurn;
            var isNegative = ratio < 0.0;
            var magnitude = isNegative ? -ratio : ratio;
            var units = _asinUnits[RatioIndex(magnitude)];
            var a = isNegative ? 65536 - units : units;
            return (ushort)(a & 0xFFFF);
        }

        // 仅供对拍锚点与 CRC 复算。
        public int SinRaw(ushort units) { return _sin[units]; }
        public int AtanUnitsRaw(int index) { return _atanUnits[index]; }
        public int AsinUnitsRaw(int index) { return _asinUnits[index]; }

        private static int RatioIndex(double ratio)
        {
            if (!(ratio > 0.0)) return 0;
            var scaled = Math.Floor(ratio * 1024.0 + 0.5);
            if (scaled >= 1024.0) return 1024;
            return (int)scaled;
        }

        private static int[] ToIntArray(JsonValue value, int expectedLength, string key)
        {
            if (value.Count != expectedLength)
            {
                throw new TrigTableException(key + " 长度必须是 " + expectedLength + "，实际 " + value.Count);
            }
            var array = new int[expectedLength];
            for (var i = 0; i < expectedLength; i++)
            {
                array[i] = value[i].AsInt();
            }
            return array;
        }
    }
}
