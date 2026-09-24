using System;
using Ac.Core;
using UnityEngine;

namespace Ac.Audio
{
    public enum Waveform : byte { Sine = 0, Saw = 1, Triangle = 2, Square = 3, Noise = 4, Fm = 5 }
    public enum FilterKind : byte { None = 0, LowPass = 1, BandPass = 2, HighPass = 3 }

    // 一层 = 一个波形 + 包络 + 可选滤波/颤音/谐波/失真（§5 配方表的行内要点）
    public struct SynthLayer
    {
        public Waveform Wave;
        public FilterKind Filter;
        public float FreqHz;
        public float FreqEndHz;
        public float StartS;
        public float AttackS;
        public float DecayS;
        public float Peak;
        public float Harmonic;
        public float FilterHz;
        public float FilterQ;
        public float VibratoHz;
        public float VibratoCents;
        public float FmHz;
        public float FmIndex;
        public float Distortion;
        public float GainLfoHz;      // 环境循环的慢 LFO
        public float GainLfoDepth;
    }

    public struct SfxRecipe
    {
        public SfxName Name;
        public MixBus Bus;
        public bool Spatial;
        public float Gain;
        public int DurationMs;
        public bool Loop;
        public SynthLayer[] Layers;
    }

    // C11 §5/§4 任务 1：唯一的 AudioClip.Create 调用点。配方 → 采样。
    public static class Synth
    {
        public const int SampleRate = 48000;
        public const int Channels = 1;
        public const float EnvelopeFloorGain = 0.0001f;
        public const float MinRampSeconds = 0.001f;
        public const uint NoiseSeed = 0x9E3779B1u;
        public const float NoiseAmplitude = 0.9f;

        private static uint _noiseState = NoiseSeed;

        public static void ResetNoise() { _noiseState = NoiseSeed; }

        // 线性同余：state = state * 1664525 + 1013904223（uint 回绕），映射到 [-1, 1)
        public static float NextNoise()
        {
            _noiseState = unchecked(_noiseState * 1664525u + 1013904223u);
            return (float)(_noiseState / 2147483648.0 - 1.0) * NoiseAmplitude;
        }

        public static int SampleCountOf(int durationMs) { return SampleRate * durationMs / 1000; }

        public static float[] Render(in SfxRecipe recipe)
        {
            var count = SampleCountOf(recipe.DurationMs);
            var data = new float[count];
            var layers = recipe.Layers;
            if (layers == null) return data;
            for (var l = 0; l < layers.Length; l++) AddLayer(data, layers[l]);
            // 峰值归一：任何一条配方都不许削波（§8 第一条风险）
            var peak = 0f;
            for (var i = 0; i < count; i++) { var a = data[i] < 0f ? -data[i] : data[i]; if (a > peak) peak = a; }
            if (peak > 1f) for (var i = 0; i < count; i++) data[i] /= peak;
            return data;
        }

        public static AudioClip CreateClip(in SfxRecipe recipe)
        {
            var data = Render(recipe);
            var clip = AudioClip.Create(recipe.Name.ToString(), data.Length, Channels, SampleRate, false);
            clip.SetData(data, 0);
            return clip;
        }

        private static void AddLayer(float[] data, in SynthLayer layer)
        {
            var rate = SampleRate;
            var phase = 0.0;
            var lfoPhase = 0.0;
            var lp = 0f;
            var bp = 0f;
            var noiseLp = 0f;
            for (var i = 0; i < data.Length; i++)
            {
                var t = i / (double)rate;
                var local = t - layer.StartS;
                if (local < 0.0) continue;
                var envelope = Envelope(local, layer.AttackS, layer.DecayS);
                if (envelope <= 0f) continue;

                var freq = layer.FreqHz;
                if (layer.FreqEndHz > 0f && layer.DecayS > 0f)
                {
                    var k = local / layer.DecayS;
                    if (k > 1.0) k = 1.0;
                    freq = (float)(layer.FreqHz + (layer.FreqEndHz - layer.FreqHz) * k);
                }
                if (layer.VibratoHz > 0f && layer.VibratoCents > 0f)
                {
                    var cents = Math.Sin(2.0 * Math.PI * layer.VibratoHz * local) * layer.VibratoCents;
                    freq *= (float)Math.Pow(2.0, cents / 1200.0);
                }

                float sample;
                if (layer.Wave == Waveform.Noise)
                {
                    sample = NextNoise();
                }
                else if (layer.Wave == Waveform.Fm)
                {
                    var modulator = Math.Sin(2.0 * Math.PI * layer.FmHz * local);
                    sample = (float)Math.Sin(2.0 * Math.PI * freq * local + layer.FmIndex * modulator);
                }
                else
                {
                    phase += freq / rate;
                    if (phase >= 1.0) phase -= 1.0;
                    sample = Oscillator(layer.Wave, phase);
                    if (layer.Harmonic > 0f) sample += Oscillator(layer.Wave, phase * 2.0 % 1.0) * layer.Harmonic;
                }

                if (layer.Filter != FilterKind.None && layer.FilterHz > 0f)
                {
                    var fc = 2.0 * Math.PI * layer.FilterHz / rate;
                    if (fc > 1.0) fc = 1.0;
                    var damping = layer.FilterQ > 0f ? (float)(1.0 / layer.FilterQ) : 1f;
                    lp += (float)(fc * (sample - lp));
                    var hp = sample - lp;
                    noiseLp += (float)(fc * (hp - noiseLp));
                    if (layer.Filter == FilterKind.LowPass) sample = lp;
                    else if (layer.Filter == FilterKind.HighPass) sample = hp;
                    else { bp += (float)(fc * (noiseLp * damping - bp)); sample = bp * layer.FilterQ; }
                }

                if (layer.Distortion > 0f) sample = (float)Math.Tanh(sample * (1.0 + layer.Distortion * 4.0)) * (1f - layer.Distortion * 0.4f);

                var gain = layer.Peak;
                if (layer.GainLfoHz > 0f)
                {
                    // §5：4s 循环 = 1 个整周期，循环点相位自洽
                    lfoPhase += layer.GainLfoHz / rate;
                    if (lfoPhase >= 1.0) lfoPhase -= 1.0;
                    gain *= 1f + layer.GainLfoDepth * (float)Math.Sin(2.0 * Math.PI * lfoPhase);
                }
                data[i] += sample * envelope * gain;
            }
        }

        private static float Oscillator(Waveform wave, double phase)
        {
            if (wave == Waveform.Saw) return (float)(2.0 * phase - 1.0);
            if (wave == Waveform.Triangle) return (float)(phase < 0.5 ? 4.0 * phase - 1.0 : 3.0 - 4.0 * phase);
            if (wave == Waveform.Square) return phase < 0.5 ? 1f : -1f;
            return (float)Math.Sin(2.0 * Math.PI * phase);
        }

        // 指数斜坡：地板 0.0001 避免 0 值；起音至少 1ms
        public static float Envelope(double local, float attackS, float decayS)
        {
            if (attackS < MinRampSeconds) attackS = MinRampSeconds;
            if (local < attackS)
            {
                var k = local / attackS;
                return (float)(EnvelopeFloorGain + (1.0 - EnvelopeFloorGain) * k);
            }
            if (decayS <= 0f) return 1f;     // 持续音（循环层）：attack 之后保持满幅，不是掉到地板
            var d = (local - attackS) / decayS;
            if (d >= 1.0) return 0f;
            return (float)Math.Pow(EnvelopeFloorGain, d);
        }
    }
}
