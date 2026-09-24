using UnityEngine;

namespace Ac.Core
{
    // C11 §5：四条总线。subtitle 不接任何音源，只承载文本。
    public enum MixBus : byte { Master = 0, Sfx = 1, Music = 2, Subtitle = 3 }

    // C11 §5 配方表的名字（15 条事件音 + 2 条环境循环）
    public enum SfxName : byte
    {
        Rifle = 0, Pistol = 1, Shotgun = 2, Hit = 3, Headshot = 4,
        BleatGrunt = 5, BleatRam = 6, BleatElite = 7, BleatKing = 8, ChargeWarn = 9,
        Berserk = 10, Reload = 11, UiClick = 12, Victory = 13, Defeat = 14,
        AmbientPasture = 15, AmbientTension = 16,
    }

    // Ac.Audio 实现它；Ac.UI 只经它调音频（不引用 Ac.Audio）。
    public interface IAudioSeam
    {
        void SetVolume(MixBus bus, float value);
        void Play(SfxName name, Vector3? position, float gain);
        int VoiceCount { get; }
        void AttachListener(Transform listener);
    }

    // C11 §9：注入点。默认是一个空实现，没接音频时整个游戏静音但不会崩。
    public static class AudioSeam
    {
        private static IAudioSeam _seam = new SilentSeam();

        public static void Bind(IAudioSeam seam) { _seam = seam == null ? new SilentSeam() : seam; }
        public static void SetVolume(MixBus bus, float value) { _seam.SetVolume(bus, value); }
        public static void Play(SfxName name, Vector3? position, float gain) { _seam.Play(name, position, gain); }
        public static int VoiceCount { get { return _seam.VoiceCount; } }
        public static void AttachListener(Transform listener) { _seam.AttachListener(listener); }
        public static bool IsSilent { get { return _seam is SilentSeam; } }

        private sealed class SilentSeam : IAudioSeam
        {
            public void SetVolume(MixBus bus, float value) { }
            public void Play(SfxName name, Vector3? position, float gain) { }
            public int VoiceCount { get { return 0; } }
            public void AttachListener(Transform listener) { }
        }
    }
}
