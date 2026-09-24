using System;

namespace Ac.View
{
    public enum SmoothingAction : byte
    {
        None = 0,     // 误差在忽略阈值内：不加偏移
        Smooth = 1,   // 交给指数衰减
        Snap = 2      // 立刻吸附到权威姿态
    }

    // C04 §5.5 的硬纠正表现语义：只写渲染变换的偏移量，从不写回 SnapshotView 或任何模拟状态。
    public sealed class ErrorSmoother
    {
        public const double EpsilonM = 0.001;          // 归零阈值（SmoothingEpsilonM）
        public const double SnapThresholdM = 1.0;      // 吸附阈值
        public const double TimeConstantSeconds = 0.12;

        public double OffsetX { get; private set; }
        public double OffsetY { get; private set; }
        public double OffsetZ { get; private set; }
        public int SnapCount { get; private set; }
        public int ResetCount { get; private set; }

        public double Magnitude { get { return Math.Sqrt(OffsetX * OffsetX + OffsetY * OffsetY + OffsetZ * OffsetZ); } }

        // §5.5 的阈值语义：<= 0.001m 忽略并归零；> 1.0m 立刻吸附并重置；其余进入平滑。
        public SmoothingAction Apply(double dx, double dy, double dz)
        {
            var magnitude = Math.Sqrt(dx * dx + dy * dy + dz * dz);
            if (magnitude <= EpsilonM)
            {
                Reset();
                return SmoothingAction.None;
            }
            if (magnitude > SnapThresholdM)
            {
                Reset();
                SnapCount += 1;
                return SmoothingAction.Snap;
            }
            Add(dx, dy, dz);
            return SmoothingAction.Smooth;
        }

        public void Add(double dx, double dy, double dz)
        {
            OffsetX += dx;
            OffsetY += dy;
            OffsetZ += dz;
        }

        // 一阶指数衰减：offset *= exp(-dt / 0.12s)；衰减到小于归零阈值即归零。
        public void Decay(double dtMs)
        {
            if (dtMs <= 0.0) return;
            if (Magnitude <= 0.0) return;
            var factor = Math.Exp(-(dtMs / 1000.0) / TimeConstantSeconds);
            OffsetX *= factor;
            OffsetY *= factor;
            OffsetZ *= factor;
            if (Magnitude < EpsilonM) Reset();
        }

        public void Reset()
        {
            OffsetX = 0.0;
            OffsetY = 0.0;
            OffsetZ = 0.0;
            ResetCount += 1;
        }
    }
}
