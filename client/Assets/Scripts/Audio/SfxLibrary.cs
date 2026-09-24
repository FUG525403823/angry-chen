using System.Collections.Generic;
using Ac.Core;

namespace Ac.Audio
{
    // C11 §5：17 条配方（15 条事件音 + 2 条环境循环）。数值逐字段被 audio_test 比对，不得擅改。
    public static class SfxLibrary
    {
        public const int RecipeCount = 17;
        public const int PrewarmBudgetMs = 50;

        private static readonly SfxRecipe[] Recipes = BuildRecipes();
        private static readonly Dictionary<SfxName, UnityEngine.AudioClip> Cache = new Dictionary<SfxName, UnityEngine.AudioClip>();
        private static readonly Dictionary<SfxName, float> Gains = BuildGains();
        private static int _prewarmed;

        public static int PrewarmedCount { get { return _prewarmed; } }

        public static SfxRecipe RecipeOf(SfxName name) { return Recipes[(int)name]; }
        public static SfxRecipe[] All { get { return Recipes; } }
        public static float GainOf(SfxName name) { return Gains[name]; }

        public static UnityEngine.AudioClip ClipOf(SfxName name)
        {
            if (!UnityEngine.Application.isPlaying) return null;   // 批处理下不建 AudioClip
            UnityEngine.AudioClip clip;
            if (Cache.TryGetValue(name, out clip)) return clip;
            clip = Synth.CreateClip(Recipes[(int)name]);
            Cache[name] = clip;
            return clip;
        }

        public static int Prewarm()
        {
            for (var i = 0; i < Recipes.Length; i++) ClipOf(Recipes[i].Name);
            _prewarmed = Recipes.Length;
            return _prewarmed;
        }

        private static Dictionary<SfxName, float> BuildGains()
        {
            var map = new Dictionary<SfxName, float>();
            for (var i = 0; i < Recipes.Length; i++) map[Recipes[i].Name] = Recipes[i].Gain;
            return map;
        }

        private static SynthLayer Layer(Waveform wave, float freq, float attack, float decay, float peak)
        {
            var layer = default(SynthLayer);
            layer.Wave = wave;
            layer.FreqHz = freq;
            layer.AttackS = attack;
            layer.DecayS = decay;
            layer.Peak = peak;
            return layer;
        }

        private static SfxRecipe Recipe(SfxName name, MixBus bus, bool spatial, float gain, int durationMs, params SynthLayer[] layers)
        {
            var recipe = default(SfxRecipe);
            recipe.Name = name;
            recipe.Bus = bus;
            recipe.Spatial = spatial;
            recipe.Gain = gain;
            recipe.DurationMs = durationMs;
            recipe.Loop = name == SfxName.AmbientPasture || name == SfxName.AmbientTension;
            recipe.Layers = layers;
            return recipe;
        }

        private static SfxRecipe[] BuildRecipes()
        {
            var list = new SfxRecipe[RecipeCount];
            var rifleNoise = Layer(Waveform.Noise, 0f, 0.010f, 0.120f, 0.9f);
            rifleNoise.Filter = FilterKind.LowPass; rifleNoise.FilterHz = 3000f; rifleNoise.FilterQ = 0.7f;
            var rifleTone = Layer(Waveform.Triangle, 1500f, 0.002f, 0.120f, 0.35f);
            rifleTone.Harmonic = 0.5f; rifleTone.Filter = FilterKind.BandPass; rifleTone.FilterHz = 1500f; rifleTone.FilterQ = 1.1f;
            list[(int)SfxName.Rifle] = Recipe(SfxName.Rifle, MixBus.Sfx, true, 0.90f, 200, rifleNoise, rifleTone);

            var pistolNoise = Layer(Waveform.Noise, 0f, 0.004f, 0.065f, 0.8f);
            pistolNoise.Filter = FilterKind.LowPass; pistolNoise.FilterHz = 2600f; pistolNoise.FilterQ = 0.7f;
            var pistolTone = Layer(Waveform.Triangle, 2200f, 0.002f, 0.065f, 0.30f);
            pistolTone.Harmonic = 0.35f; pistolTone.Filter = FilterKind.BandPass; pistolTone.FilterHz = 2400f; pistolTone.FilterQ = 1.3f;
            list[(int)SfxName.Pistol] = Recipe(SfxName.Pistol, MixBus.Sfx, true, 0.85f, 130, pistolNoise, pistolTone);

            var shotgunLow = Layer(Waveform.Noise, 0f, 0.012f, 0.280f, 1.0f);
            shotgunLow.Filter = FilterKind.LowPass; shotgunLow.FilterHz = 1200f; shotgunLow.FilterQ = 0.6f;
            var shotgunSub = Layer(Waveform.Noise, 0f, 0.008f, 0.180f, 0.7f);
            shotgunSub.Filter = FilterKind.LowPass; shotgunSub.FilterHz = 400f; shotgunSub.FilterQ = 0.8f;
            var shotgunBody = Layer(Waveform.Fm, 160f, 0.004f, 0.300f, 0.4f);
            shotgunBody.FmHz = 55f; shotgunBody.FmIndex = 3f;
            list[(int)SfxName.Shotgun] = Recipe(SfxName.Shotgun, MixBus.Sfx, true, 1.00f, 380, shotgunLow, shotgunSub, shotgunBody);

            var hit = Layer(Waveform.Noise, 0f, 0.004f, 0.060f, 0.8f);
            hit.Filter = FilterKind.BandPass; hit.FilterHz = 900f; hit.FilterQ = 0.8f;
            list[(int)SfxName.Hit] = Recipe(SfxName.Hit, MixBus.Sfx, true, 0.80f, 150, hit);

            var headNoise = Layer(Waveform.Noise, 0f, 0.003f, 0.050f, 0.7f);
            headNoise.Filter = FilterKind.HighPass; headNoise.FilterHz = 2000f; headNoise.FilterQ = 0.8f;
            var headTone = Layer(Waveform.Sine, 1400f, 0.003f, 0.090f, 0.6f);
            headTone.FreqEndHz = 700f; headTone.Filter = FilterKind.BandPass; headTone.FilterHz = 1600f; headTone.FilterQ = 1.2f;
            list[(int)SfxName.Headshot] = Recipe(SfxName.Headshot, MixBus.Sfx, true, 0.90f, 170, headNoise, headTone);

            var grunt = Layer(Waveform.Saw, 300f, 0.020f, 0.220f, 0.50f);
            grunt.Harmonic = 0.6f; grunt.Filter = FilterKind.BandPass; grunt.FilterHz = 900f; grunt.FilterQ = 1.1f; grunt.VibratoHz = 7f; grunt.VibratoCents = 25f;
            list[(int)SfxName.BleatGrunt] = Recipe(SfxName.BleatGrunt, MixBus.Sfx, true, 0.55f, 320, grunt);

            var ram = Layer(Waveform.Saw, 240f, 0.030f, 0.420f, 0.60f);
            ram.Harmonic = 0.55f; ram.Filter = FilterKind.BandPass; ram.FilterHz = 650f; ram.FilterQ = 1.0f; ram.VibratoHz = 5.5f; ram.VibratoCents = 30f;
            list[(int)SfxName.BleatRam] = Recipe(SfxName.BleatRam, MixBus.Sfx, true, 0.60f, 560, ram);

            var elite = Layer(Waveform.Triangle, 280f, 0.020f, 0.350f, 0.45f);
            elite.Harmonic = 0.7f; elite.Filter = FilterKind.BandPass; elite.FilterHz = 1100f; elite.FilterQ = 1.4f; elite.VibratoHz = 8f; elite.VibratoCents = 20f;
            list[(int)SfxName.BleatElite] = Recipe(SfxName.BleatElite, MixBus.Sfx, true, 0.50f, 460, elite);

            var king = Layer(Waveform.Saw, 220f, 0.040f, 0.800f, 0.75f);
            king.Harmonic = 0.6f; king.Filter = FilterKind.BandPass; king.FilterHz = 500f; king.FilterQ = 0.9f; king.VibratoHz = 5f; king.VibratoCents = 45f;
            list[(int)SfxName.BleatKing] = Recipe(SfxName.BleatKing, MixBus.Sfx, true, 0.70f, 1000, king);

            var warnTone = Layer(Waveform.Sine, 320f, 0.030f, 0.500f, 0.6f);
            warnTone.FreqEndHz = 900f;
            var warnNoise = Layer(Waveform.Noise, 0f, 0.030f, 0.450f, 0.4f);
            warnNoise.Filter = FilterKind.BandPass; warnNoise.FilterHz = 1400f; warnNoise.FilterQ = 1.2f;
            list[(int)SfxName.ChargeWarn] = Recipe(SfxName.ChargeWarn, MixBus.Sfx, true, 0.80f, 950, warnTone, warnNoise);

            var berserkSaw = Layer(Waveform.Saw, 62f, 0.040f, 1.000f, 0.8f);
            berserkSaw.Distortion = 0.85f;
            var berserkNoise = Layer(Waveform.Noise, 0f, 0.050f, 1.100f, 0.5f);
            berserkNoise.Filter = FilterKind.LowPass; berserkNoise.FilterHz = 240f; berserkNoise.FilterQ = 0.7f;
            list[(int)SfxName.Berserk] = Recipe(SfxName.Berserk, MixBus.Sfx, true, 0.90f, 1400, berserkSaw, berserkNoise);

            var reloadA = Layer(Waveform.Square, 1700f, 0.001f, 0.060f, 0.35f);
            var reloadB = Layer(Waveform.Square, 1150f, 0.001f, 0.060f, 0.35f);
            reloadB.StartS = 0.160f;
            var reloadC = Layer(Waveform.Square, 900f, 0.001f, 0.080f, 0.35f);
            reloadC.StartS = 0.340f;
            var reloadNoise = Layer(Waveform.Noise, 0f, 0.001f, 0.060f, 0.25f);
            reloadNoise.StartS = 0.340f;
            list[(int)SfxName.Reload] = Recipe(SfxName.Reload, MixBus.Sfx, false, 0.60f, 700, reloadA, reloadB, reloadC, reloadNoise);

            var click = Layer(Waveform.Square, 880f, 0.002f, 0.030f, 0.3f);
            list[(int)SfxName.UiClick] = Recipe(SfxName.UiClick, MixBus.Sfx, false, 0.50f, 90, click);

            var v1 = Layer(Waveform.Triangle, 523f, 0.005f, 0.250f, 0.4f);
            var v2 = Layer(Waveform.Triangle, 659f, 0.005f, 0.250f, 0.4f);
            v2.StartS = 0.120f;
            var v3 = Layer(Waveform.Triangle, 784f, 0.005f, 0.250f, 0.4f);
            v3.StartS = 0.240f;
            list[(int)SfxName.Victory] = Recipe(SfxName.Victory, MixBus.Sfx, false, 0.60f, 1200, v1, v2, v3);

            var d1 = Layer(Waveform.Saw, 392f, 0.020f, 0.500f, 0.45f);
            d1.FreqEndHz = 330f;
            var d2 = Layer(Waveform.Saw, 311f, 0.020f, 0.700f, 0.45f);
            d2.FreqEndHz = 262f; d2.StartS = 0.300f;
            list[(int)SfxName.Defeat] = Recipe(SfxName.Defeat, MixBus.Sfx, false, 0.60f, 1500, d1, d2);

            // 循环层用持续音（attack 0.2s 后保持，不做 3.9s 衰减——那会让循环听起来每次都在掉音量）
            var pastureA = Layer(Waveform.Noise, 0f, 0.200f, 0f, 0.5f);
            pastureA.Filter = FilterKind.LowPass; pastureA.FilterHz = 300f; pastureA.FilterQ = 0.5f;
            pastureA.GainLfoHz = 0.25f; pastureA.GainLfoDepth = 0.15f;
            var pastureB = Layer(Waveform.Noise, 0f, 0.200f, 0f, 0.4f);
            pastureB.Filter = FilterKind.LowPass; pastureB.FilterHz = 900f; pastureB.FilterQ = 0.6f;
            pastureB.GainLfoHz = 0.25f; pastureB.GainLfoDepth = 0.15f;
            list[(int)SfxName.AmbientPasture] = Recipe(SfxName.AmbientPasture, MixBus.Music, false, 0.35f, 4000, pastureA, pastureB);

            var tensionFm = Layer(Waveform.Fm, 55f, 0.200f, 0f, 0.5f);
            tensionFm.FmHz = 8f; tensionFm.FmIndex = 2f;
            var tensionNoise = Layer(Waveform.Noise, 0f, 0.200f, 0f, 0.35f);
            tensionNoise.Filter = FilterKind.LowPass; tensionNoise.FilterHz = 200f; tensionNoise.FilterQ = 0.7f;
            list[(int)SfxName.AmbientTension] = Recipe(SfxName.AmbientTension, MixBus.Music, false, 0.30f, 4000, tensionFm, tensionNoise);
            return list;
        }
    }
}
