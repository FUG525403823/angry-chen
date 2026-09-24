using System;

namespace Ac.Sim
{
    // C02 §5.3/§5.4、S02 §5.3：ADR-009 冻结的量化与反量化。
    // 舍入一律 floor(x + 0.5)（RoundHalfUp）：半值恒向 +∞，而 Math.Round 默认是银行家舍入（ToEven），
    // 两者在 2.5 / 3.5 这类半值上给出相反结果（这里得 3 / 4，Math.Round 得 2 / 4）；负数半值同理（-2.5 → -2）。
    // 非有限输入（NaN/Inf）一律归 0，避免整数转换的未定义行为。
    public static class Quantize
    {
        public const int CentimeterPerMeter = 100;
        public const int AngleUnits = TrigTable.UnitsPerTurn;
        public const int HpUnits = 255;
        public const int MoveAxisScale = 127;
        public const int MinPositionCm = -32768;
        public const int MaxPositionCm = 32767;
        public const int MaxEntityId = 1024;
        public const double PitchLimitRad = 1.5707963267948966;  // π/2

        public static double RoundHalfUp(double value)
        {
            return Math.Floor(value + 0.5);
        }

        // 归一到 (-π, π]；非有限输入返回 0。与 server/src/core/math.hpp 的 wrapAngle 逐位同义，
        // 包含「负的 2π 整数倍保留 -0.0 符号位」这条细节。
        public static double WrapAngle(double radians)
        {
            if (double.IsNaN(radians) || double.IsInfinity(radians)) return 0.0;
            var turns = Math.Floor(radians / TrigTable.TwoPi + 0.5);
            var a = radians - TrigTable.TwoPi * turns;
            if (a > TrigTable.Pi)
            {
                a -= TrigTable.TwoPi;
            }
            else if (a <= -TrigTable.Pi)
            {
                a += TrigTable.TwoPi;
            }
            if (a == 0.0 && radians < 0.0) a = -0.0;
            return a;
        }

        public static short QuantizePosition(double meters)
        {
            if (double.IsNaN(meters) || double.IsInfinity(meters)) return 0;
            var centimeters = Math.Floor(meters * CentimeterPerMeter + 0.5);
            return (short)Clamp(centimeters, MinPositionCm, MaxPositionCm);
        }

        public static double DequantizePosition(short centimeters)
        {
            return centimeters / 100.0;
        }

        // 弧度 -> 角度单位：wrap 后按每圈 65536 单位取整，再按 C++ 截断取模的语义折到 0..65535。
        public static ushort QuantizeAngle(double radians)
        {
            var wrapped = WrapAngle(radians);
            var units = (long)Math.Floor(wrapped / TrigTable.TwoPi * AngleUnits + 0.5);
            var a = units % AngleUnits;
            if (a < 0) a += AngleUnits;
            return (ushort)a;
        }

        public static double DequantizeAngle(ushort units)
        {
            return WrapAngle(TrigTable.RadiansFromUnits(units));
        }

        public static sbyte QuantizeAxis(double axis)
        {
            if (double.IsNaN(axis) || double.IsInfinity(axis)) return 0;
            var steps = Math.Floor(axis * MoveAxisScale + 0.5);
            return (sbyte)Clamp(steps, -MoveAxisScale, MoveAxisScale);
        }

        public static double DequantizeAxis(sbyte value)
        {
            return value / 127.0;
        }

        // 钳制的是取整后的步数（不是先钳制输入比例），与服务端 quantizeRatio 一致。
        public static byte QuantizeRatio(double ratio)
        {
            if (double.IsNaN(ratio) || double.IsInfinity(ratio)) return 0;
            var steps = Math.Floor(ratio * HpUnits + 0.5);
            return (byte)Clamp(steps, 0, HpUnits);
        }

        public static double DequantizeRatio(byte units)
        {
            return units / 255.0;
        }

        private static double Clamp(double value, double lo, double hi)
        {
            if (value < lo) return lo;
            if (value > hi) return hi;
            return value;
        }
    }
}
