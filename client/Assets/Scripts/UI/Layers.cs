using Ac.Core;
using UnityEngine;

namespace Ac.UI
{
    // 世界侧的最小视图：Layers 只关心"哪些羊、在哪、是不是在冲锋/倒地/死了"
    public struct SheepAudioView
    {
        public ushort EntityId;
        public Vector3 Position;
        public byte Kind;        // 0 grunt / 1 ram / 2 elite / 3 king（Ac.Sim 的羊种编码）
        public bool Alive;
        public bool Windup;      // 进入冲锋前摇
        public bool Downed;
    }

    public struct AudioWorldView
    {
        public SheepAudioView[] Sheep;
        public int Count;
    }

    // C11 §5/§4 任务 5：事件与世界状态 → 播放调度（一律经 Ac.Core.AudioSeam，不引用 Ac.Audio）
    public sealed class Layers
    {
        public const int MaxTracked = 1024;
        public const float DeathBleatGain = 0.40f;
        public const float DownedHitGain = 0.50f;
        public const float ChargeWarnCooldownMs = 1600f;
        public const float StaleMs = 2000f;
        public const float BleatNearM = 4f;
        public const float BleatFarM = 26f;
        public const float BleatNearGain = 1.0f;
        public const float BleatFarGain = 0.45f;

        private struct Tracked
        {
            public ushort EntityId;
            public Vector3 Position;
            public float SinceSeenMs;
            public float BleatDueMs;
            public float WarnCooldownMs;
            public bool Windup;
            public bool Alive;
        }

        private readonly Tracked[] _tracked = new Tracked[MaxTracked];
        private readonly Subtitles _subtitles = new Subtitles();
        private int _salt;

        public int TrackedCount { get; private set; }
        public int BleatCount { get; private set; }
        public int ChargeWarnCount { get; private set; }
        public int DeathBleatCount { get; private set; }
        public int MaxProbeCount { get; private set; }
        public Subtitles Subtitles { get { return _subtitles; } }

        // §5：羊叫间隔 = 3600 + ((id + 0x9E37) * 0x85EBCA6B ^ (salt + 1) * 0xC2B2AE35) % 3400
        public static int BleatIntervalMs(int id, int salt)
        {
            const int k1 = unchecked((int)0x85EBCA6Bu);
            const int k2 = unchecked((int)0xC2B2AE35u);
            var mixed = unchecked((id + 0x9E37) * k1) ^ unchecked((salt + 1) * k2);
            var mod = mixed % 3400;
            if (mod < 0) mod += 3400;
            return 3600 + mod;
        }

        public static float BleatGain(float distanceM)
        {
            if (distanceM <= BleatNearM) return BleatNearGain;
            if (distanceM >= BleatFarM) return BleatFarGain;
            var t = (distanceM - BleatNearM) / (BleatFarM - BleatNearM);
            return BleatNearGain + (BleatFarGain - BleatNearGain) * t;
        }

        private static SfxName BleatOf(byte kind)
        {
            if (kind == 1) return SfxName.BleatRam;
            if (kind == 2) return SfxName.BleatElite;
            if (kind >= 3) return SfxName.BleatKing;
            return SfxName.BleatGrunt;
        }

        public bool NotifyFire(int slot)
        {
            var name = slot == 1 ? SfxName.Rifle : (slot == 2 ? SfxName.Shotgun : SfxName.Pistol);
            AudioSeam.Play(name, null, 1f);
            return true;
        }

        public void NotifyReload() { AudioSeam.Play(SfxName.Reload, null, 1f); }

        // 事件缝（C10 的 HudEvent / Ac.Net.EventType）。winnerTeam 与 selfTeam 只用于胜负音的选择。
        public void HandleEvent(in HudEvent hudEvent, Vector3 position, int winnerTeam, int selfTeam)
        {
            switch (hudEvent.Type)
            {
                case Ac.Net.EventType.PlayerHit:
                    if ((hudEvent.HitFlags & CombatFlags.HitFlagHeadshot) != 0) AudioSeam.Play(SfxName.Headshot, position, 1f);
                    else AudioSeam.Play(SfxName.Hit, position, 1f);
                    if ((hudEvent.HitFlags & CombatFlags.HitFlagDowned) != 0) { AudioSeam.Play(SfxName.Hit, position, DownedHitGain); _subtitles.Push(SubtitleCue.Downed); }
                    break;
                case Ac.Net.EventType.SheepKilled:
                    DeathBleatCount += 1;
                    AudioSeam.Play(SfxName.BleatGrunt, position, DeathBleatGain);
                    break;
                case Ac.Net.EventType.WaveStart:
                    _subtitles.Push(SubtitleCue.WaveStart, hudEvent.Wave);
                    break;
                case Ac.Net.EventType.PlayerDowned:
                    AudioSeam.Play(SfxName.Hit, position, DownedHitGain);
                    _subtitles.Push(SubtitleCue.Downed);
                    break;
                case Ac.Net.EventType.RageActivated:
                    AudioSeam.Play(SfxName.Berserk, position, 1f);
                    break;
                case Ac.Net.EventType.MatchEnded:
                    var won = winnerTeam == selfTeam;
                    AudioSeam.Play(won ? SfxName.Victory : SfxName.Defeat, null, 1f);
                    _subtitles.Push(won ? SubtitleCue.Victory : SubtitleCue.Defeat);
                    break;
            }
        }

        public void Update(float dtMs, in AudioWorldView world, Vector3 listenerPosition)
        {
            _subtitles.Tick(dtMs);
            for (var i = 0; i < MaxTracked; i++)
            {
                if (_tracked[i].EntityId == 0) continue;
                _tracked[i].SinceSeenMs += dtMs;
                _tracked[i].BleatDueMs -= dtMs;
                if (_tracked[i].WarnCooldownMs > 0f) _tracked[i].WarnCooldownMs -= dtMs;
                if (_tracked[i].SinceSeenMs >= StaleMs) Release(i);
            }

            var sheep = world.Sheep;
            if (sheep == null) return;
            var count = world.Count;
            if (count > sheep.Length) count = sheep.Length;
            for (var s = 0; s < count; s++)
            {
                var view = sheep[s];
                if (view.EntityId == 0) continue;
                var index = SlotOf(view.EntityId);
                if (index < 0) continue;
                if (_tracked[index].EntityId != view.EntityId)
                {
                    // 槽位被回收过：所有派生状态（前摇/冷却/计时）都要按新实体重来，否则会串音并吃掉首次预警
                    _tracked[index].EntityId = view.EntityId;
                    _tracked[index].BleatDueMs = BleatIntervalMs(view.EntityId, _salt++);
                    _tracked[index].WarnCooldownMs = 0f;
                    _tracked[index].Windup = false;
                    TrackedCount += 1;
                }
                _tracked[index].SinceSeenMs = 0f;
                _tracked[index].Position = view.Position;

                if (!view.Alive && _tracked[index].Alive)
                {
                    DeathBleatCount += 1;
                    AudioSeam.Play(BleatOf(view.Kind), view.Position, DeathBleatGain);
                }
                _tracked[index].Alive = view.Alive;

                if (view.Windup && !_tracked[index].Windup && _tracked[index].WarnCooldownMs <= 0f)
                {
                    _tracked[index].WarnCooldownMs = ChargeWarnCooldownMs;
                    ChargeWarnCount += 1;
                    AudioSeam.Play(SfxName.ChargeWarn, view.Position, 1f);
                    _subtitles.Push(SubtitleCue.ChargeWarn);
                }
                _tracked[index].Windup = view.Windup;

                if (view.Alive && _tracked[index].BleatDueMs <= 0f)
                {
                    var distance = Vector3.Distance(view.Position, listenerPosition);
                    BleatCount += 1;
                    AudioSeam.Play(BleatOf(view.Kind), view.Position, BleatGain(distance));
                    _tracked[index].BleatDueMs = BleatIntervalMs(view.EntityId, _salt++);
                }
            }
        }

        private void Release(int index)
        {
            _tracked[index] = default(Tracked);      // 整槽清零：漏一个字段就是跨实体串音
            TrackedCount -= 1;
        }

        private int SlotOf(ushort entityId)
        {
            var start = entityId % MaxTracked;
            for (var i = 0; i < MaxTracked; i++)
            {
                var index = (start + i) % MaxTracked;
                MaxProbeCount += 1;
                if (_tracked[index].EntityId == entityId || _tracked[index].EntityId == 0) return index;
            }
            return -1;
        }
    }
}
