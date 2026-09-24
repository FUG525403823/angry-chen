using System;
using Ac.Core;
using UnityEngine;

namespace Ac.Audio
{
    // C11 §5/§4 任务 3：四条总线、增益表、24 音源上限、最远抢占、AudioSource 池。
    // 时钟用整数毫秒：浮点累积会让"恰好落在 30ms 边界"的比较随机翻转。
    public sealed class Mixer : IAudioSeam
    {
        public const int MaxVoices = 24;
        public const int MaxVoicesPerRecipe = 4;
        public const float SameRecipeWindowSec = 0.03f;
        public const int SameRecipeWindowMs = 30;      // 由 §5 的秒值派生，别在两处各写一遍
        public const float RefDistanceM = 3f;
        public const float MaxDistanceM = 40f;
        public const float Rolloff = 1.4f;

        public struct Voice
        {
            public SfxName Name;
            public MixBus Bus;
            public Vector3 Position;
            public float Gain;
            public float RemainingMs;
            public bool Spatial;
            public bool Active;
        }

        private static readonly float[] ConstGain = { 1.00f, 0.80f, 0.50f, 0.00f };
        private static readonly float[] DefaultVolume = { 0.80f, 0.80f, 0.50f, 0.00f };

        private readonly Voice[] _voices = new Voice[MaxVoices];
        private readonly float[] _volume = new float[BusCount.MixBusCount];
        private readonly int[] _lastPlayMs = new int[SfxLibrary.RecipeCount];
        private readonly int[] _perRecipe = new int[SfxLibrary.RecipeCount];
        private readonly AudioSource[] _sources = new AudioSource[MaxVoices];

        private Transform _listener;
        private GameObject _poolRoot;
        private int _clockMs;
        private bool _bound;

        public Mixer()
        {
            for (var i = 0; i < BusCount.MixBusCount; i++) _volume[i] = DefaultVolume[i];
            // "从没放过"用一个大负数哨兵：开局时钟就是 0，不留哨兵的话第一条配方会被 30ms 去重窗口吃掉；
            // 也不能用 int.MinValue——减去当前时钟会溢出回绕成负数，等于永远在窗口内。
            for (var i = 0; i < SfxLibrary.RecipeCount; i++) _lastPlayMs[i] = -1000000;
        }

        public int VoiceCount { get; private set; }
        public int DroppedCount { get; private set; }
        public int DedupedCount { get; private set; }
        public int PreemptCount { get; private set; }
        public int SourceCreatedCount { get; private set; }

        public Voice VoiceAt(int index) { return _voices[index]; }

        public static float ConstGainOf(MixBus bus) { return ConstGain[(int)bus]; }
        public static float DefaultVolumeOf(MixBus bus) { return DefaultVolume[(int)bus]; }

        public void AttachListener(Transform listener) { _listener = listener; }

        // 启动装配点：把 Mixer 接到 AudioSeam 并预热配方。幂等，场景装配阶段调用一次即可。
        public static Mixer EnsureStarted()
        {
            var mixer = new Mixer();
            mixer.BindAndPrewarm();
            return mixer;
        }

        public void BindAndPrewarm()
        {
            AudioSeam.Bind(this);
            SfxLibrary.Prewarm();
            _bound = true;
        }

        public bool Bound { get { return _bound; } }

        public void SetVolume(MixBus bus, float value)
        {
            var index = (int)bus;
            if (bus == MixBus.Subtitle) { _volume[index] = 0f; return; }   // subtitle 增益恒为 0
            _volume[index] = value < 0f ? 0f : (value > 1f ? 1f : value);
        }

        public float VolumeOf(MixBus bus) { return _volume[(int)bus]; }

        public void Tick(float dtMs)
        {
            _clockMs += (int)dtMs;
            var live = 0;
            for (var i = 0; i < MaxVoices; i++)
            {
                if (!_voices[i].Active) continue;
                _voices[i].RemainingMs -= dtMs;
                if (_voices[i].RemainingMs <= 0f) { _voices[i].Active = false; Release(i); continue; }
                live += 1;
            }
            VoiceCount = live;
            for (var i = 0; i < SfxLibrary.RecipeCount; i++) _perRecipe[i] = 0;
            for (var i = 0; i < MaxVoices; i++) if (_voices[i].Active) _perRecipe[(int)_voices[i].Name] += 1;
        }

        public void Play(SfxName name, Vector3? position, float gain)
        {
            var recipe = SfxLibrary.RecipeOf(name);
            if (recipe.Bus == MixBus.Subtitle) { DroppedCount += 1; return; }        // 字幕总线不接音源
            if (_clockMs - _lastPlayMs[(int)name] < SameRecipeWindowMs) { DedupedCount += 1; return; }
            if (_perRecipe[(int)name] >= MaxVoicesPerRecipe && !PreemptSameRecipe(name)) { DroppedCount += 1; return; }

            var index = FreeSlot();
            if (index < 0) { DroppedCount += 1; return; }

            var voice = default(Voice);
            voice.Name = name;
            voice.Bus = recipe.Bus;
            voice.Spatial = recipe.Spatial;
            voice.Position = position.HasValue ? position.Value : Vector3.zero;
            voice.Gain = recipe.Gain * gain * _volume[(int)recipe.Bus];
            voice.RemainingMs = recipe.DurationMs;
            voice.Active = true;
            _voices[index] = voice;
            _lastPlayMs[(int)name] = _clockMs;
            _perRecipe[(int)name] += 1;
            VoiceCount += 1;
            Assign(index, in voice);
        }

        // 同配方满了：抢掉该配方里最远的一条
        private bool PreemptSameRecipe(SfxName incoming)
        {
            var worst = -1;
            var worstDistance = -1f;
            for (var i = 0; i < MaxVoices; i++)
            {
                if (!_voices[i].Active || _voices[i].Name != incoming) continue;
                var distance = _voices[i].Spatial ? Vector3.Distance(_voices[i].Position, ListenerPosition()) : 0f;
                if (distance > worstDistance) { worstDistance = distance; worst = i; }
            }
            if (worst < 0) return false;
            _voices[worst].Active = false;
            Release(worst);
            VoiceCount -= 1;
            PreemptCount += 1;
            return true;
        }

        private int FreeSlot()
        {
            for (var i = 0; i < MaxVoices; i++) if (!_voices[i].Active) return i;
            var worst = -1;                                  // 全满：抢最远的
            var worstDistance = -1f;
            for (var i = 0; i < MaxVoices; i++)
            {
                var distance = _voices[i].Spatial ? Vector3.Distance(_voices[i].Position, ListenerPosition()) : 0f;
                if (distance > worstDistance) { worstDistance = distance; worst = i; }
            }
            if (worst >= 0)
            {
                _voices[worst].Active = false;
                Release(worst);
                VoiceCount -= 1;
                PreemptCount += 1;
                return worst;
            }
            return -1;
        }

        private Vector3 ListenerPosition() { return _listener == null ? Vector3.zero : _listener.position; }

        // §5 的 3D 衰减：inverse 模型（refDistance/(refDistance + rolloff·(d − refDistance))）。
        // maxDistance 只用于"谁更远"的抢占排序，不把它当硬性归零——Unity 的 inverse 曲线不会在 maxDistance 处跳成 0。
        public float DistanceGain(in Vector3 position)
        {
            var distance = Vector3.Distance(position, ListenerPosition());
            if (distance <= RefDistanceM) return 1f;
            return (float)(RefDistanceM / (RefDistanceM + Rolloff * (distance - RefDistanceM)));
        }

        // AudioSource 池：池子只在真机（播放模式）里建，批处理不碰 Unity 音频对象。
        private void Assign(int index, in Voice voice)
        {
            if (!Application.isPlaying) return;
            var source = _sources[index];
            if (source == null)
            {
                if (_poolRoot == null)
                {
                    _poolRoot = new GameObject("AcAudioPool");
                    UnityEngine.Object.DontDestroyOnLoad(_poolRoot);
                }
                source = _poolRoot.AddComponent<AudioSource>();
                source.playOnAwake = false;
                source.spatialBlend = voice.Spatial ? 1f : 0f;
                source.rolloffMode = AudioRolloffMode.Logarithmic;   // Unity 的 inverse 距离曲线
                source.minDistance = RefDistanceM;
                source.maxDistance = MaxDistanceM;
                _sources[index] = source;
                SourceCreatedCount += 1;
            }
            var clip = SfxLibrary.ClipOf(voice.Name);
            if (clip == null) return;
            source.clip = clip;
            source.volume = voice.Gain;
            if (voice.Spatial) source.transform.position = voice.Position;
            source.loop = SfxLibrary.RecipeOf(voice.Name).Loop;
            source.Play();
        }

        private void Release(int index)
        {
            var source = _sources[index];
            if (source == null) return;
            source.Stop();
            source.clip = null;
        }
    }
}
