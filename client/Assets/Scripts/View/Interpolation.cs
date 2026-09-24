using System;
using Ac.Sim;

namespace Ac.View
{
    // C04 §5.1/§5.3 的渲染侧插值：纯函数 + 一个零分配的渲染时间估计器。
    // 本层允许使用超越函数，但插值本身只用四则运算与角度折返（Quantize.WrapAngle）。
    public static class Interpolation
    {
        public const int DelayMs = 100;               // §5.1 InterpolationDelayMs
        public const int DelayMaxMs = 250;            // §5.1 InterpolationDelayMaxMs
        public const int JitterAbsorbMs = 50;         // §5.1 JitterAbsorbMs：单帧时间估计修正量上下限
        public const int ArrivalSampleCount = 60;     // §5.1 动态延迟取最近 60 个到达间隔
        public const double ResetEstimateThresholdMs = 500.0;   // §5.3 偏差过大直接重置估计值

        private static double Clamp(double value, double min, double max)
        {
            if (value < min) return min;
            if (value > max) return max;
            return value;
        }

        private static double Clamp01(double value) { return Clamp(value, 0.0, 1.0); }

        public static double Lerp(double older, double newer, double alpha)
        {
            return older + (newer - older) * alpha;
        }

        // 最短弧：把 (to - from) 折到 [-π, π]（复用 Ac.Sim 的确定性折返，不引入超越函数）。
        public static double ShortestAngleDelta(double from, double to)
        {
            return Quantize.WrapAngle(to - from);
        }

        public static double LerpAngle(double from, double to, double alpha)
        {
            return Quantize.WrapAngle(from + ShortestAngleDelta(from, to) * alpha);
        }

        // §5.3：alpha 由渲染时间在关键帧区间里的位置决定，落在 [0, 1]。
        public static double InterpolationAlpha(double renderTimeMs, double olderMs, double newerMs)
        {
            if (newerMs <= olderMs) return 1.0;   // 同刻两帧：直接显示较新的一帧
            return Clamp01((renderTimeMs - olderMs) / (newerMs - olderMs));
        }

        // §5.1：插值延迟 = clamp(2 * 到达间隔中位数, 100, 250)。
        public static int DynamicDelayMs(int medianArrivalMs)
        {
            if (medianArrivalMs <= 0) return DelayMs;
            var delay = medianArrivalMs * 2;
            if (delay < DelayMs) return DelayMs;
            if (delay > DelayMaxMs) return DelayMaxMs;
            return delay;
        }

        // §5.3 的 FindBracket：times 升序（索引 0 最旧）。返回参与比较的帧数；
        // olderIndex/newerIndex 是同一张表里的下标，关键帧少于 2 时两者相等。
        public static int FindBracket(uint[] times, int count, double renderTimeMs, out int olderIndex, out int newerIndex)
        {
            olderIndex = -1;
            newerIndex = -1;
            if (times == null || count <= 0) return 0;
            if (count > times.Length) count = times.Length;
            if (count == 1 || renderTimeMs <= times[0])
            {
                olderIndex = 0;
                newerIndex = 0;
                return count;
            }
            if (renderTimeMs >= times[count - 1])
            {
                olderIndex = count - 1;
                newerIndex = count - 1;
                return count;
            }
            for (var i = 0; i < count - 1; i++)
            {
                if (renderTimeMs < times[i] || renderTimeMs >= times[i + 1]) continue;
                olderIndex = i;
                newerIndex = i + 1;
                return count;
            }
            olderIndex = count - 1;
            newerIndex = count - 1;
            return count;
        }

        // §5.3 的渲染时间估计：每帧向「最新快照 serverTimeMs + 自该帧到达以来的本地经过时间」修正，
        // 单帧修正量钳在 ±50ms；偏差绝对值超过 500ms（重连、长时间丢包）直接重置估计值。
        public sealed class RenderClock
        {
            private readonly int[] _arrivals = new int[ArrivalSampleCount];
            private readonly int[] _scratch = new int[ArrivalSampleCount];
            private int _arrivalCount;
            private int _arrivalNext;

            public double EstimatedServerTimeMs { get; private set; }
            public int RenderDelayMs { get; private set; }
            public bool HasEstimate { get; private set; }

            public RenderClock()
            {
                RenderDelayMs = Interpolation.DelayMs;
            }

            public double RenderTimeMs { get { return EstimatedServerTimeMs - RenderDelayMs; } }

            public void OnSnapshot(uint serverTimeMs, double localNowMs)
            {
                // 到达瞬间的修正目标就是该帧的 serverTimeMs；帧间外推由 Advance 负责，
                // 于是晚到/早到的帧只体现为一次被 ±50ms 钳住的修正（§5.1 的抖动吸收）。
                var target = (double)serverTimeMs;
                if (!HasEstimate)
                {
                    EstimatedServerTimeMs = target;
                    HasEstimate = true;
                }
                else
                {
                    var delta = target - EstimatedServerTimeMs;
                    if (Math.Abs(delta) > ResetEstimateThresholdMs) EstimatedServerTimeMs = target;
                    else EstimatedServerTimeMs += Interpolation.Clamp(delta, -JitterAbsorbMs, JitterAbsorbMs);
                }
                if (_hasArrival) PushArrival((int)Math.Round(localNowMs - _lastArrivalLocalMs));
                _hasArrival = true;
                _lastArrivalLocalMs = localNowMs;
                RenderDelayMs = Interpolation.DynamicDelayMs(MedianArrivalMs());
            }

            public void Advance(double localDeltaMs)
            {
                if (!HasEstimate || localDeltaMs <= 0.0) return;
                EstimatedServerTimeMs += localDeltaMs;
            }

            public void Reset()
            {
                HasEstimate = false;
                EstimatedServerTimeMs = 0.0;
                _arrivalCount = 0;
                _arrivalNext = 0;
                _hasArrival = false;
                RenderDelayMs = Interpolation.DelayMs;
            }

            // 最近到达间隔的中位数（零分配：就地插排到复用缓冲）。
            private int MedianArrivalMs()
            {
                if (_arrivalCount == 0) return 0;
                for (var i = 0; i < _arrivalCount; i++) _scratch[i] = _arrivals[i];
                for (var i = 1; i < _arrivalCount; i++)
                {
                    var value = _scratch[i];
                    var j = i - 1;
                    while (j >= 0 && _scratch[j] > value)
                    {
                        _scratch[j + 1] = _scratch[j];
                        j -= 1;
                    }
                    _scratch[j + 1] = value;
                }
                return _scratch[_arrivalCount / 2];
            }

            private void PushArrival(int intervalMs)
            {
                if (intervalMs < 0) intervalMs = 0;
                _arrivals[_arrivalNext] = intervalMs;
                _arrivalNext = (_arrivalNext + 1) % ArrivalSampleCount;
                if (_arrivalCount < ArrivalSampleCount) _arrivalCount += 1;
            }

            private bool _hasArrival;
            private double _lastArrivalLocalMs;
        }
    }
}
