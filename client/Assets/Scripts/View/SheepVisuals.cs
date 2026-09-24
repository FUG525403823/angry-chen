using UnityEngine;

namespace Ac.View
{
    // C08 §5(e)：13 个 SHEEP_STATE 码的动画目标（顺序即冻结表 0..12）
    public enum SheepAnim : byte
    {
        Idle = 0, Alert = 1, Run = 2, Windup = 3, Charge = 4, Attack = 5, Ranged = 6,
        Stagger = 7, Dead = 8, KingIdle = 9, KingWalk = 10, KingRage = 11, Summon = 12,
    }

    // §9：快照状态 → 实例变换 + 额标强度
    public sealed class SheepVisuals
    {
        public const int StateCount = 13;
        public const byte StateDead = 8;
        // 线上掩码（Ac.Net.SnapshotCodec）：charging = 1<<5、fading = 1<<6。
        // S03 计划里写的是 kindFlags 内的相对位（3/4），直接当成掩码会撞上 rageMode(8)/reloading(16)。
        public const byte FlagCharging = 32;
        public const byte FlagFading = 64;
        public const double EmblemIntensityBase = 0.2;
        public const double EmblemIntensitySpan = 0.8;
        public const double EmblemSmoothingPerTick = 0.153518;   // = 1 - e^(-0.05/0.3)
        public const double CorpseFadeMs = 1500;
        public const double CorpseShrink = 0.72;
        public const double CorpseSinkM = 0.16;
        public const double CorpseDarken = 0.62;

        private readonly SheepInstancePool _pool;
        private readonly double[] _deathMs = new double[SheepInstancePool.EntityCapacity];
        private readonly bool[] _dead = new bool[SheepInstancePool.EntityCapacity];

        public int SkippedCount { get; private set; }
        public int CorpseCount { get; private set; }

        public SheepVisuals(SheepInstancePool pool) { _pool = pool; }

        public static double EmblemIntensity(double hpRatio)
        {
            var health = hpRatio < 0.0 ? 0.0 : (hpRatio > 1.0 ? 1.0 : hpRatio);
            return EmblemIntensityBase + EmblemIntensitySpan * (1.0 - health);
        }

        public static double SmoothEmblem(double previous, double target)
        {
            return previous + (target - previous) * EmblemSmoothingPerTick;
        }

        public static SheepAnim MapState(byte state)
        {
            if (state >= StateCount) state = 0;
            return (SheepAnim)state;
        }

        // §5(e)：位域覆盖——charging 强制 Charge；fading 播放尸体淡出
        public static SheepAnim MapAnim(byte state, byte flags)
        {
            if ((flags & FlagCharging) != 0) return SheepAnim.Charge;
            return MapState(state);
        }

        // 尸体淡出曲线：p=0 完整、p=1 收缩 0.72 / 下沉 0.16m / 变暗 0.62
        public static double CorpseProgress(double elapsedMs)
        {
            if (elapsedMs <= 0.0) return 0.0;
            var p = elapsedMs / CorpseFadeMs;
            return p > 1.0 ? 1.0 : p;
        }

        public static void CorpseParams(double progress, out float shrink, out float sinkM, out float darken)
        {
            var p = progress < 0.0 ? 0.0 : (progress > 1.0 ? 1.0 : progress);
            shrink = (float)(1.0 - (1.0 - CorpseShrink) * p);
            sinkM = (float)(CorpseSinkM * p);
            darken = (float)(1.0 - (1.0 - CorpseDarken) * p);
        }

        // §9：写一条实例；返回实例下标，剔除或池满返回 -1
        public int Write(in EntityView view, double nowMs)
        {
            if (!view.Visible) { SkippedCount += 1; return -1; }
            var kind = SheepMesh.KindOf(view.Kind);
            var form = SheepMesh.Form(kind);
            int index;
            if (_pool.TryAcquire((int)kind, out index) < 0) { SkippedCount += 1; return -1; }

            var anim = MapAnim(view.State, view.Flags);
            var shrink = 1f;
            var sink = 0f;
            var darken = 1f;
            if (anim == SheepAnim.Dead || (view.Flags & FlagFading) != 0)
            {
                if (!_dead[index]) { _dead[index] = true; _deathMs[index] = nowMs; }
                var progress = CorpseProgress(nowMs - _deathMs[index]);
                CorpseParams(progress, out shrink, out sink, out darken);
                CorpseCount += 1;
                if (progress >= 1.0)
                {
                    var finished = _pool.Instances[index];
                    finished.Visible = false;
                    _pool.Instances[index] = finished;
                    SkippedCount += 1;
                    return -1;
                }
            }
            else if (_dead[index])
            {
                _dead[index] = false;
            }

            // §5(b)：团抖动以 EntityId 为种子，落在逐实例尺寸上（网格按羊形共用一份，装不下逐羊差异）
            var scale = (float)form.Scale * shrink * SheepMesh.WoolJitter(kind, view.Id);
            // 用权威坐标：§4 任务 3 要求写 x/y/z；逐实体误差平滑（C04 的 Smoother）是玩家与近处实体的活，
            // 羊群这一层先不做偏移，免得 1024 只羊各带一份平滑状态
            var position = new Vector3((float)view.X, (float)(view.Y - sink), (float)view.Z);
            var rotation = Quaternion.Euler(0f, (float)view.YawRad * Mathf.Rad2Deg, 0f);
            var instance = _pool.Instances[index];
            instance.Transform = Matrix4x4.TRS(position, rotation, new Vector3(scale, scale, scale));
            instance.Darken = darken;
            var head = new Vector3(0f, (float)(SheepMesh.HeadOffsetY * form.Scale), (float)(SheepMesh.HeadOffsetZ * form.Scale));
            instance.EmblemTransform = Matrix4x4.TRS(position + rotation * head, rotation, Vector3.one);
            instance.HpRatio = (float)view.HpRatio;
            instance.Kind = (byte)kind;
            instance.State = (byte)anim;
            instance.Visible = true;
            instance.EmblemIntensity = (float)SmoothEmblem(instance.EmblemIntensity, EmblemIntensity(view.HpRatio));
            _pool.Instances[index] = instance;
            return index;
        }
    }
}
