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
        private struct EntityState
        {
            public bool Tracked, Seen, Dead;
            public int ActiveIndex;
            public uint Lifetime;
            public double DeathMs, Emblem;
        }
        // Wire entity capacity is fixed; state follows identity, not draw order.
        private readonly EntityState[] _states = new EntityState[EntityViews.Capacity + 1];
        private readonly ushort[] _active = new ushort[ushort.MaxValue + 1];
        private int _activeCount;
        private bool _frameOpen;

        public int SkippedCount { get; private set; }
        public int CorpseCount { get; private set; }

        public SheepVisuals(SheepInstancePool pool) { _pool = pool; }

        // 每帧入口（必须在当帧所有 Write 之前调用一次）：复位池游标与逐帧计数。
        // 审计 M6：池的"每帧 Reset"契约此前**没有任何调用者** —— Write 只管取槽位，
        // 同形态累计到 PerFormCapacity(256) 之后 TryAcquire 恒返 -1，羊会静默消失。
        public void BeginFrame()
        {
            if (_frameOpen) EndFrame();
            _frameOpen = true;
            for (var i = 0; i < _activeCount; i++) _states[_active[i]].Seen = false;
            _pool.Reset();
            SkippedCount = 0;
            CorpseCount = 0;
        }

        // Call Write for every current sheep, including invisible/pool-overflow sheep.
        public void EndFrame()
        {
            if (!_frameOpen) return;
            for (var i = _activeCount - 1; i >= 0; i--)
                if (!_states[_active[i]].Seen) Remove(_active[i]);
            _frameOpen = false;
        }

        // Explicit removal also handles an ID removed and reused between render frames.
        public void Remove(ushort id)
        {
            if (id >= _states.Length || !_states[id].Tracked) return;
            var index = _states[id].ActiveIndex;
            var moved = _active[--_activeCount];
            _active[index] = moved;
            _states[moved].ActiveIndex = index;
            _states[id] = default(EntityState);
        }

        public void Clear()
        {
            while (_activeCount > 0) Remove(_active[_activeCount - 1]);
            _frameOpen = false;
            _pool.Reset();
            SkippedCount = 0;
            CorpseCount = 0;
        }

        public static double EmblemIntensity(double hpRatio)
        {
            var health = hpRatio < 0.0 ? 0.0 : (hpRatio > 1.0 ? 1.0 : hpRatio);
            return EmblemIntensityBase + EmblemIntensitySpan * (1.0 - health);
        }

        public static double SmoothEmblem(double previous, double target)
        {
            return previous + (target - previous) * EmblemSmoothingPerTick;
        }

        public static double SmoothEmblem(double previous, double target, double dtMs)
        {
            if (!(dtMs > 0)) return previous;
            return previous + (target - previous) * (1.0 - System.Math.Exp(-dtMs / 300.0));
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
        public int Write(in EntityView view, double nowMs) { return Write(view, nowMs, 50.0); }

        public int Write(in EntityView view, double nowMs, double dtMs)
        {
            var id = view.Id;
            if (id == 0 || id >= _states.Length) { SkippedCount += 1; return -1; }
            if (_states[id].Tracked && _states[id].Lifetime != view.Lifetime) Remove(id);
            var state = _states[id];
            if (!state.Tracked)
            {
                state.Tracked = true;
                state.Lifetime = view.Lifetime;
                state.ActiveIndex = _activeCount;
                _active[_activeCount++] = id;
                state.Emblem = EmblemIntensity(view.HpRatio);
            }
            state.Seen = true;
            state.Emblem = SmoothEmblem(state.Emblem, EmblemIntensity(view.HpRatio), dtMs);
            var anim = MapAnim(view.State, view.Flags);
            var shrink = 1f;
            var sink = 0f;
            var darken = 1f;
            var expired = false;
            if (anim == SheepAnim.Dead || (view.Flags & FlagFading) != 0)
            {
                if (!state.Dead) { state.Dead = true; state.DeathMs = nowMs; }
                var progress = CorpseProgress(nowMs - state.DeathMs);
                CorpseParams(progress, out shrink, out sink, out darken);
                CorpseCount += 1;
                expired = progress >= 1.0;
            }
            else state.Dead = false;
            _states[id] = state;
            if (!view.Visible || expired) { SkippedCount += 1; return -1; }
            var kind = SheepMesh.KindOf(view.Kind);
            var form = SheepMesh.Form(kind);
            int index;
            if (_pool.TryAcquire((int)kind, out index) < 0) { SkippedCount += 1; return -1; }
            // §5(b)：团抖动以 EntityId 为种子，落在逐实例尺寸上（网格按羊形共用一份，装不下逐羊差异）
            var scale = (float)form.Scale * shrink * SheepMesh.WoolJitter(kind, view.Id);
            // 用权威坐标：§4 任务 3 要求写 x/y/z；逐实体误差平滑（C04 的 Smoother）是玩家与近处实体的活，
            // 羊群这一层先不做偏移，免得 1024 只羊各带一份平滑状态
            var position = new Vector3((float)view.X, (float)(view.Y - sink), (float)view.Z);
            var rotation = Quaternion.Euler(0f, (float)view.YawRad * Mathf.Rad2Deg, 0f);
            var instance = _pool.Instances[index];
            instance.Transform = Matrix4x4.TRS(position, rotation, new Vector3(scale, scale, scale));
            instance.Darken = darken;
            var head = SheepMesh.EmblemAnchor(kind) * scale;
            instance.EmblemTransform = Matrix4x4.TRS(position + rotation * head,
                rotation * Quaternion.Euler(-55f, 0f, 0f), new Vector3(scale, scale, scale));
            instance.HpRatio = (float)view.HpRatio;
            instance.Kind = (byte)kind;
            instance.State = (byte)anim;
            instance.Visible = true;
            instance.EmblemIntensity = (float)state.Emblem;
            _pool.Instances[index] = instance;
            return index;
        }
    }
}
