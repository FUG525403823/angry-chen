using System;

namespace Ac.Sim
{
    // C02 §3：双精度向量，只用 + - * / 与 sqrt（ADR-010 §2 允许的运算子集）。
    // 字段语义与 v1 packages/shared/src/math.ts、server/src/core/math.hpp 一致。
    public struct Vec3
    {
        public double X;
        public double Y;
        public double Z;

        public Vec3(double x, double y, double z)
        {
            X = x;
            Y = y;
            Z = z;
        }

        public static Vec3 Add(Vec3 a, Vec3 b)
        {
            return new Vec3(a.X + b.X, a.Y + b.Y, a.Z + b.Z);
        }

        public static Vec3 Sub(Vec3 a, Vec3 b)
        {
            return new Vec3(a.X - b.X, a.Y - b.Y, a.Z - b.Z);
        }

        public static Vec3 Scale(Vec3 a, double scale)
        {
            return new Vec3(a.X * scale, a.Y * scale, a.Z * scale);
        }

        public static Vec3 AddScaled(Vec3 origin, Vec3 direction, double scale)
        {
            return new Vec3(origin.X + direction.X * scale, origin.Y + direction.Y * scale,
                origin.Z + direction.Z * scale);
        }

        public static double Dot(Vec3 a, Vec3 b)
        {
            return a.X * b.X + a.Y * b.Y + a.Z * b.Z;
        }

        public static double LengthSq(Vec3 a)
        {
            return Dot(a, a);
        }

        public static double Length(Vec3 a)
        {
            return Math.Sqrt(LengthSq(a));
        }

        // 逐分量除以长度（不是乘 1/len）：与服务端 normalize 的舍入逐位一致。
        public static Vec3 Normalize(Vec3 a)
        {
            var len = Length(a);
            if (len == 0.0) return new Vec3(0.0, 0.0, 0.0);
            return new Vec3(a.X / len, a.Y / len, a.Z / len);
        }
    }
}
