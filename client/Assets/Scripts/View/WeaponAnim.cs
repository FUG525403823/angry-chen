using UnityEngine;

namespace Ac.View
{
    // C09 §5(b)：动画与节奏表。所有时长/角度都是常量，状态只做积分，不做随机。
    public sealed class WeaponAnim
    {
        public const float ReloadPistolMs = 1400f;
        public const float ReloadRifleMs = 2000f;
        public const float ReloadShotgunMs = 2600f;
        public const float SwapDownMs = 260f;
        public const float MuzzleFlashMs = 45f;
        public const float RecoilPitchDegPerShot = 0.35f;
        public const float RecoilYawJitterDeg = 0.2f;
        public const float RecoilDecayPerSecond = 7.5f;
        public const float BreathHz = 0.42f;
        public const float WalkHz = 3.4f;
        public const float WalkPhasePerMeter = 0.55f;
        public const float WalkRpm = WalkHz;   // 占位避免歧义：行走相位只由 AddWalkDistance 推进
        public const float ReloadPitchRad = 0.42f;
        public const float ReloadRollRad = 0.30f;
        public const float DownedPitchRad = 0.46f;
        public const float SpreadPerShotDeg = 0.1f;
        public const float SpreadMaxDeg = 0.25f;
        public const float SpreadDecayDelayMs = 350f;
        public const float SpreadDecayPerSecond = 6.0f;
        public const int PistolRpm = 300;
        public const int RifleRpm = 600;
        public const int ShotgunRpm = 70;
        public const float FireRateMultiplier = 1.0f;

        // S08 §5：开火间隔只由公式给出，不写近似常数
        public static float IntervalMs(int rpm, float multiplier) { return 60000f / (rpm * multiplier); }

        public float RecoilPitchDeg { get; private set; }
        public float RecoilYawDeg { get; private set; }
        public float SpreadDeg { get; private set; }
        public float BreathPhase { get; private set; }
        public float WalkPhase { get; private set; }
        public float ReloadRemainingMs { get; private set; }
        public float SwapRemainingMs { get; private set; }
        public bool Downed { get; private set; }

        private float _sinceLastShotMs;
        private float _yawJitterSign = 1f;

        public float ReloadDurationMs(int slot)
        {
            if (slot == 1) return ReloadRifleMs;
            if (slot == 2) return ReloadShotgunMs;
            return ReloadPistolMs;
        }

        public void OnFire(int slot)
        {
            RecoilPitchDeg += RecoilPitchDegPerShot;
            // 偏航抖动左右交替，幅度固定——不用随机数，回放可复现
            RecoilYawDeg += RecoilYawJitterDeg * _yawJitterSign;
            _yawJitterSign = -_yawJitterSign;
            SpreadDeg += SpreadPerShotDeg;
            if (SpreadDeg > SpreadMaxDeg) SpreadDeg = SpreadMaxDeg;
            _sinceLastShotMs = 0f;
        }

        public void OnReload(int slot) { ReloadRemainingMs = ReloadDurationMs(slot); }
        public void OnSwapDown(int slot) { SwapRemainingMs = SwapDownMs; }
        public void OnDowned(bool downed) { Downed = downed; }

        public void Tick(float dtMs)
        {
            var dt = dtMs / 1000f;
            _sinceLastShotMs += dtMs;
            var decay = RecoilDecayPerSecond * dt;
            RecoilPitchDeg = Decay(RecoilPitchDeg, decay);
            RecoilYawDeg = Decay(RecoilYawDeg, decay);
            if (_sinceLastShotMs >= SpreadDecayDelayMs) SpreadDeg = Decay(SpreadDeg, SpreadDecayPerSecond * dt);
            BreathPhase = Wrap(BreathPhase + BreathHz * dt * 2f * Mathf.PI);
            if (ReloadRemainingMs > 0f) ReloadRemainingMs = ReloadRemainingMs > dtMs ? ReloadRemainingMs - dtMs : 0f;
            if (SwapRemainingMs > 0f) SwapRemainingMs = SwapRemainingMs > dtMs ? SwapRemainingMs - dtMs : 0f;
        }

        public void AddWalkDistance(float meters) { WalkPhase = Wrap(WalkPhase + meters * WalkPhasePerMeter); }

        public float ReloadPitchRadNow { get { return ReloadRemainingMs > 0f ? ReloadPitchRad : 0f; } }
        public float ReloadRollRadNow { get { return ReloadRemainingMs > 0f ? ReloadRollRad : 0f; } }
        public float DownedPitchRadNow { get { return Downed ? DownedPitchRad : 0f; } }

        private static float Decay(float value, float amount)
        {
            if (amount <= 0f) return value;
            return value > amount ? value - amount : 0f;
        }

        private static float Wrap(float phase)
        {
            var twoPi = 2f * Mathf.PI;
            while (phase >= twoPi) phase -= twoPi;
            return phase;
        }
    }
}
