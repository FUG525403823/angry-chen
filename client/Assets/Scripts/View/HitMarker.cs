using UnityEngine;

namespace Ac.View
{
    public enum MarkerState : byte { Hit = 0, Kill = 1, Headshot = 2 }

    // C09 §5(e)：命中 / 击杀 / 爆头三态反馈。颜色与时长是冻结值。
    public sealed class HitMarker
    {
        public const int HitColor = 0xFFFFFF;
        public const int KillColor = 0xFF4D4D;
        public const int HeadshotColor = 0xFFD24D;
        public const float LifetimeMs = 140f;
        public const float HeadshotScale = 1.35f;
        public const float HeadshotDamageMultiplier = 2.0f;

        // HIT_FLAG 位（权威值来自 server/src/config/combat.hpp:18-20，C10 计划同）
        public const int FlagHeadshot = 1;
        public const int FlagDowned = 2;
        public const int FlagKilled = 4;

        public MarkerState State { get; private set; }
        public float RemainingMs { get; private set; }
        public int ShowCount { get; private set; }

        public void Show(MarkerState state)
        {
            State = state;
            RemainingMs = LifetimeMs;
            ShowCount += 1;
        }

        // 事件位 → 三态：爆头优先于击杀，击杀优先于命中（§5(e) 的行序）
        public MarkerState ShowFromFlags(int hitFlags)
        {
            var state = MarkerState.Hit;
            if ((hitFlags & FlagHeadshot) != 0) state = MarkerState.Headshot;
            else if ((hitFlags & FlagKilled) != 0) state = MarkerState.Kill;
            Show(state);
            return state;
        }

        public static int ColorOf(MarkerState state)
        {
            if (state == MarkerState.Kill) return KillColor;
            if (state == MarkerState.Headshot) return HeadshotColor;
            return HitColor;
        }

        public float ScaleOf(MarkerState state) { return state == MarkerState.Headshot ? HeadshotScale : 1f; }

        public void Tick(float dtMs)
        {
            if (RemainingMs <= 0f) return;
            RemainingMs = RemainingMs > dtMs ? RemainingMs - dtMs : 0f;
        }
    }
}
