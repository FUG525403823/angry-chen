using UnityEngine;

namespace Ac.View
{
    public struct TracerSegment
    {
        public Vector3 From;
        public Vector3 To;
        public float AgeMs;
        public bool Alive;
    }

    // C09 §5(c)(d)：曳光池。起点必须是枪口（MuzzleWorld），不是眼位——这是本步要修的 v1 缺陷。
    public sealed class Tracer
    {
        public const int Capacity = 32;
        public const float LifetimeMs = 120f;
        public const float FallbackLengthM = 30f;

        private readonly TracerSegment[] _segments = new TracerSegment[Capacity];
        private int _cursor;
        private int _live;
        private int _lastSpawned = -1;

        public int OverflowCount { get; private set; }
        public int LiveCount { get { return _live; } }
        public TracerSegment[] Segments { get { return _segments; } }

        public void Reset()
        {
            for (var i = 0; i < Capacity; i++) _segments[i].Alive = false;
            _cursor = 0;
            _live = 0;
            _lastSpawned = -1;
        }

        // 权威命中点回填（S09 的 PlayerHit.HitX/Y/Z）：本地开火那一刻还没有命中结果，终点只能按视线
        // 外推 30m；回执一到就把**最近那一段**的终点改到真正打中的点上（寿命 120ms，回执通常 1–3 帧内到）。
        // 只改最近一段：更早的几段早该消失了，而且玩家正在看的就是刚打出去的那一条。
        public bool RetargetNewest(in Vector3 end)
        {
            if (_lastSpawned < 0) return false;
            var segment = _segments[_lastSpawned];
            if (!segment.Alive) return false;   // 已过期的段不能改（否则会改到"上一枪"）
            segment.To = end;
            _segments[_lastSpawned] = segment;
            return true;
        }

        // §5(c)：池满覆盖最旧一条并计数
        public int Spawn(in Vector3 muzzle, in Vector3 end)
        {
            var index = _cursor;
            if (_segments[index].Alive) OverflowCount += 1;
            var segment = default(TracerSegment);
            segment.From = muzzle;
            segment.To = end;
            segment.AgeMs = 0f;
            segment.Alive = true;
            _segments[index] = segment;
            _cursor = (_cursor + 1) % Capacity;
            if (_live < Capacity) _live += 1;
            _lastSpawned = index;
            return index;
        }

        // 未命中：终点 = 起点 + 视轴方向 × 30m
        public static Vector3 FallbackEnd(in Vector3 muzzle, in Vector3 viewDirection)
        {
            var direction = viewDirection.sqrMagnitude > 0f ? viewDirection.normalized : Vector3.forward;
            return muzzle + direction * FallbackLengthM;
        }

        public void Tick(float dtMs)
        {
            var live = 0;
            for (var i = 0; i < Capacity; i++)
            {
                if (!_segments[i].Alive) continue;
                _segments[i].AgeMs += dtMs;
                if (_segments[i].AgeMs >= LifetimeMs) { _segments[i].Alive = false; continue; }
                live += 1;
            }
            _live = live;
        }

        // §5(d) 共线判据：起点与眼位距离 < 0.5m 即缺陷
        public static bool IsCollinearWithEye(in Vector3 muzzle, in Vector3 eye)
        {
            return Vector3.Distance(muzzle, eye) < ViewModel.MinMuzzleEyeDistanceM;
        }

        // 正规入口：起点直接取 ViewModel.MuzzleWorld（不是 ViewModel.EyeWorld）
        public int SpawnFromMuzzle(in ViewModel model, in Vector3 to)
        {
            return Spawn(model.MuzzleWorld, to);
        }
    }
}
