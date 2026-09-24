using UnityEngine;

namespace Ac.View
{
    // C09 §5(c)/§4 任务 6：特效统筹。池用环形游标（写满即覆盖最旧并计数），每帧只写结构体字段，帧内不分配。
    public sealed class Effects
    {
        public const float HitFlashMs = 150f;
        public const float VignetteMin = 0.12f;
        public const float VignetteMax = 0.35f;
        public const float BloodScreenMax = 0.55f;
        public const float BloodDecayPerSecond = 0.35f;
        public const float ShakeMaxM = 0.22f;
        public const float ShakeDecayPerSecond = 7.5f;
        public const int EliteBoltCapacity = 24;
        public const float EliteBoltSpinHz = 1.6f;
        public const float EliteBoltGlowScale = 1.6f;
        public const int ChargeRingCapacity = 12;
        public const float ChargeRingLifetimeMs = 1150f;
        public const float ChargeRingMissStepMs = 16f;
        public const float EliteBoltLifetimeMs = 3000f;   // 计划未给寿命：池不能只写不清，取 3s 上限
        public static readonly float DefaultFireIntervalMs = WeaponAnim.IntervalMs(WeaponAnim.PistolRpm, WeaponAnim.FireRateMultiplier);

        public struct ChargeRing { public Vector3 Position; public float AgeMs; public bool Alive; }
        public struct EliteBolt { public Vector3 Position; public Vector3 Direction; public float AgeMs; public bool Alive; }

        private readonly Tracer _tracer = new Tracer();
        private readonly Particles _particles = new Particles();
        private readonly HitMarker _marker = new HitMarker();
        private readonly ChargeRing[] _rings = new ChargeRing[ChargeRingCapacity];
        private readonly EliteBolt[] _bolts = new EliteBolt[EliteBoltCapacity];
        private int _ringCursor;
        private int _boltCursor;

        public Tracer Tracers { get { return _tracer; } }
        public Particles ParticlePool { get { return _particles; } }
        public HitMarker Marker { get { return _marker; } }
        public ChargeRing[] Rings { get { return _rings; } }
        public EliteBolt[] Bolts { get { return _bolts; } }

        public int AmmoGate { get; private set; }
        public int MuzzleFlashRemainingMs { get; private set; }
        public float HitFlashRemainingMs { get; private set; }
        public float Vignette { get; private set; }
        public float BloodScreen { get; private set; }
        public float ShakeM { get; private set; }
        public int RejectedShots { get; private set; }
        public int RingOverflowCount { get; private set; }
        public int BoltOverflowCount { get; private set; }

        public int ThrottledShots { get; private set; }
        private float _fireIntervalMs = DefaultFireIntervalMs;
        private float _sinceLastShotMs = float.MaxValue;

        public void SetAmmoGate(int gateMag) { AmmoGate = gateMag < 0 ? 0 : gateMag; }
        public void SetReducedMotion(bool reduced) { _particles.ReducedMotion = reduced; }
        public void SetFireIntervalMs(float intervalMs) { _fireIntervalMs = intervalMs; }
        public void SetBloodScreen(float value) { BloodScreen = value < 0f ? 0f : (value > BloodScreenMax ? BloodScreenMax : value); }

        // §8：池层只接受闸门放行的开火（空弹匣不出曳光）
        public bool SpawnTracer(in Vector3 from, in Vector3 to)
        {
            if (AmmoGate <= 0) { RejectedShots += 1; return false; }
            // §5(d) 数量上限：射速节流在这里（闸门只管弹药，管不了按住左键的连发）
            if (_sinceLastShotMs < _fireIntervalMs) { ThrottledShots += 1; return false; }
            _sinceLastShotMs = 0f;
            _tracer.Spawn(from, to);
            MuzzleFlashRemainingMs = (int)WeaponAnim.MuzzleFlashMs;
            return true;
        }

        public bool SpawnTracerFromMuzzle(in ViewModel model, in Vector3 viewDirection, bool hit, in Vector3 hitPoint)
        {
            var end = hit ? hitPoint : Tracer.FallbackEnd(model.MuzzleWorld, viewDirection);
            return SpawnTracer(model.MuzzleWorld, end);
        }

        public MarkerState SpawnHit(int hitFlags)
        {
            var state = _marker.ShowFromFlags(hitFlags);
            HitFlashRemainingMs = HitFlashMs;
            Vignette = VignetteMax;
            if (state == MarkerState.Kill)
            {
                _particles.Emit(ParticleKind.Wool, Particles.WoolCount, _markerStateAnchor);
                ShakeM = ShakeMaxM;
            }
            else
            {
                // 命中与爆头都出血屑；爆头多一个 1.35 缩放（伤害数字由 C10 HUD 负责）
                _particles.Emit(ParticleKind.Blood, Particles.BloodCount, _markerStateAnchor);
            }
            return state;
        }

        private Vector3 _markerStateAnchor;

        public void SetHitAnchor(in Vector3 position) { _markerStateAnchor = position; }

        public int SpawnChargeRing(in Vector3 position)
        {
            var index = _ringCursor;
            if (_rings[index].Alive) RingOverflowCount += 1;
            var ring = default(ChargeRing);
            ring.Position = position;
            ring.AgeMs = 0f;
            ring.Alive = true;
            _rings[index] = ring;
            _ringCursor = (_ringCursor + 1) % ChargeRingCapacity;
            return index;
        }

        public int SpawnEliteBolt(in Vector3 position, in Vector3 direction)
        {
            var index = _boltCursor;
            if (_bolts[index].Alive) BoltOverflowCount += 1;
            var bolt = default(EliteBolt);
            bolt.Position = position;
            bolt.Direction = direction;
            bolt.AgeMs = 0f;
            bolt.Alive = true;
            _bolts[index] = bolt;
            _boltCursor = (_boltCursor + 1) % EliteBoltCapacity;
            return index;
        }

        public void Tick(float dtMs)
        {
            _tracer.Tick(dtMs);
            _particles.Tick(dtMs);
            _marker.Tick(dtMs);
            _sinceLastShotMs = _sinceLastShotMs > 1e6f ? _sinceLastShotMs : _sinceLastShotMs + dtMs;
            if (MuzzleFlashRemainingMs > 0) MuzzleFlashRemainingMs = MuzzleFlashRemainingMs > dtMs ? MuzzleFlashRemainingMs - (int)dtMs : 0;
            HitFlashRemainingMs = HitFlashRemainingMs > dtMs ? HitFlashRemainingMs - dtMs : 0f;
            Vignette = HitFlashRemainingMs > 0f ? VignetteMax : VignetteMin;
            ShakeM = ShakeM > 0f ? (ShakeM > ShakeDecayPerSecond * dtMs / 1000f ? ShakeM - ShakeDecayPerSecond * dtMs / 1000f : 0f) : 0f;
            if (BloodScreen > 0f)
            {
                var decay = BloodDecayPerSecond * dtMs / 1000f;
                BloodScreen = BloodScreen > decay ? BloodScreen - decay : 0f;
            }
            TickRings(dtMs);
            TickBolts(dtMs);
        }

        private void TickRings(float dtMs)
        {
            for (var i = 0; i < ChargeRingCapacity; i++)
            {
                if (!_rings[i].Alive) continue;
                _rings[i].AgeMs += dtMs;
                if (_rings[i].AgeMs >= ChargeRingLifetimeMs) _rings[i].Alive = false;
            }
        }

        private void TickBolts(float dtMs)
        {
            for (var i = 0; i < EliteBoltCapacity; i++)
            {
                if (!_bolts[i].Alive) continue;
                _bolts[i].AgeMs += dtMs;
                if (_bolts[i].AgeMs >= EliteBoltLifetimeMs) { _bolts[i].Alive = false; continue; }
                _bolts[i].Position += _bolts[i].Direction * (dtMs / 1000f);
            }
        }
    }
}
