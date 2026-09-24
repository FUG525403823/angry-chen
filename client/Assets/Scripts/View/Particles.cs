using UnityEngine;

namespace Ac.View
{
    public enum ParticleKind : byte { Impact = 0, Blood = 1, Wool = 2, MuzzleSmoke = 3, Decal = 4 }

    public struct Particle
    {
        public Vector3 Position;
        public Vector3 Velocity;
        public float AgeMs;
        public float LifetimeMs;
        public float SizePx;
        public ParticleKind Kind;
        public bool Alive;
    }

    // C09 §5(c)：粒子池。点尺寸 64px；减少动态模式下抖动 ×0.35；弹孔 8s、血屑 6 粒、毛屑 4 粒。
    public sealed class Particles
    {
        public const int Capacity = 256;
        public const float PointSizePx = 64f;
        public const float ReducedMotionJitterScale = 0.35f;
        public const float DecalLifetimeMs = 8000f;
        public const float JitterVelocityM = 2.5f;
        public const float JitterAngleStepRad = 2.399963f;   // 黄金角：逐粒方向均匀铺开
        public const float JitterPerParticleRad = 0.7f;
        public const float JitterVerticalGain = 1.4f;
        public const float SmokeLifetimeMs = 1500f;
        public const int BloodCount = 6;
        public const int WoolCount = 4;
        public const float BloodLifetimeMs = 900f;
        public const float WoolLifetimeMs = 1100f;

        private readonly Particle[] _particles = new Particle[Capacity];
        private int _cursor;
        private int _live;

        public bool ReducedMotion { get; set; }
        public int OverflowCount { get; private set; }
        public int LiveCount { get { return _live; } }
        public Particle[] Buffer { get { return _particles; } }

        public static float LifetimeOf(ParticleKind kind)
        {
            if (kind == ParticleKind.Decal) return DecalLifetimeMs;
            if (kind == ParticleKind.Wool) return WoolLifetimeMs;
            if (kind == ParticleKind.MuzzleSmoke) return SmokeLifetimeMs;
            return BloodLifetimeMs;
        }

        public void Reset()
        {
            for (var i = 0; i < Capacity; i++) _particles[i].Alive = false;
            _cursor = 0;
            _live = 0;
        }

        public int Emit(ParticleKind kind, int count, in Vector3 position)
        {
            if (count <= 0) return 0;
            var emitted = 0;
            var scale = ReducedMotion ? ReducedMotionJitterScale : 1f;
            for (var i = 0; i < count; i++)
            {
                var index = _cursor;
                if (_particles[index].Alive) OverflowCount += 1;
                var particle = default(Particle);
                particle.Position = position;
                // 抖动方向用整数派生：同一次开火逐帧复现
                var angle = (index * JitterAngleStepRad) + i * JitterPerParticleRad;
                // 方向各不同、速度大小严格相等，这样"减少动态 ×0.35"是可断言的事实
                particle.Velocity = new Vector3(Mathf.Cos(angle), Mathf.Abs(Mathf.Sin(angle)) * JitterVerticalGain, Mathf.Sin(angle)).normalized * (JitterVelocityM * scale);
                particle.AgeMs = 0f;
                particle.SizePx = PointSizePx;
                particle.Kind = kind;
                particle.LifetimeMs = LifetimeOf(kind);
                particle.Alive = true;
                _particles[index] = particle;
                _cursor = (_cursor + 1) % Capacity;
                if (_live < Capacity) _live += 1;
                emitted += 1;
            }
            return emitted;
        }

        public void Tick(float dtMs)
        {
            var live = 0;
            for (var i = 0; i < Capacity; i++)
            {
                if (!_particles[i].Alive) continue;
                _particles[i].AgeMs += dtMs;
                if (_particles[i].AgeMs >= _particles[i].LifetimeMs) { _particles[i].Alive = false; continue; }
                _particles[i].Position += _particles[i].Velocity * (dtMs / 1000f);
                live += 1;
            }
            _live = live;
        }
    }
}
