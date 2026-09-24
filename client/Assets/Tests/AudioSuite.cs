using System;
using System.IO;
using Ac.Audio;
using Ac.Core;
using Ac.UI;
using UnityEngine;
using EventType = Ac.Net.EventType;

namespace Ac.Tests
{
    // C11 §6/§7：配方逐字段、总线增益、并发上限、字幕时长与去重、程序集方向、无音频素材。
    public static class AudioSuite
    {
        public static void Register()
        {
            SelfTest.Add("audio.constants", ChecksConstants);
            SelfTest.Add("audio.recipes_table", ChecksRecipeTable);
            SelfTest.Add("audio.recipe_layers", ChecksRecipeLayers);
            SelfTest.Add("audio.render", ChecksRender);
            SelfTest.Add("audio.mixer_buses", ChecksBuses);
            SelfTest.Add("audio.voices", ChecksVoices);
            SelfTest.Add("audio.layers", ChecksLayers);
            SelfTest.Add("audio.subtitles", ChecksSubtitles);
            SelfTest.Add("audio.assembly_direction", ChecksDirection);
        }

        private static void ChecksConstants()
        {
            SelfTest.Equal(48000, (long)Synth.SampleRate);
            SelfTest.Equal(1, (long)Synth.Channels);
            SelfTest.True(Synth.EnvelopeFloorGain == 0.0001f, "包络地板 0.0001", Synth.EnvelopeFloorGain.ToString("R"));
            SelfTest.True(Synth.MinRampSeconds == 0.001f, "最小斜坡 1ms", Synth.MinRampSeconds.ToString("R"));
            SelfTest.Equal(0x9E3779B1u, (long)Synth.NoiseSeed);
            SelfTest.True(Synth.NoiseAmplitude == 0.9f, "噪声幅度 0.9", Synth.NoiseAmplitude.ToString("R"));
            SelfTest.Equal(24, (long)Mixer.MaxVoices);
            SelfTest.Equal(4, (long)Mixer.MaxVoicesPerRecipe);
            SelfTest.True(Mixer.SameRecipeWindowSec == 0.03f, "同配方去重窗口 30ms", Mixer.SameRecipeWindowSec.ToString("R"));
            SelfTest.Equal(50, (long)SfxLibrary.PrewarmBudgetMs);
            SelfTest.Equal(17, (long)SfxLibrary.RecipeCount);
            SelfTest.Equal(30, (long)Mixer.SameRecipeWindowMs);
            SelfTest.True(Mixer.SameRecipeWindowMs == (int)(Mixer.SameRecipeWindowSec * 1000f), "秒值与毫秒值同源", Mixer.SameRecipeWindowMs.ToString());
            SelfTest.Equal(4, (long)BusCount.MixBusCount);
        }

        private static void ChecksRecipeTable()
        {
            var all = SfxLibrary.All;
            SelfTest.Equal(17, (long)all.Length);
            var expected = new object[,]
            {
                { SfxName.Rifle, MixBus.Sfx, true, 0.90f, 200 },
                { SfxName.Pistol, MixBus.Sfx, true, 0.85f, 130 },
                { SfxName.Shotgun, MixBus.Sfx, true, 1.00f, 380 },
                { SfxName.Hit, MixBus.Sfx, true, 0.80f, 150 },
                { SfxName.Headshot, MixBus.Sfx, true, 0.90f, 170 },
                { SfxName.BleatGrunt, MixBus.Sfx, true, 0.55f, 320 },
                { SfxName.BleatRam, MixBus.Sfx, true, 0.60f, 560 },
                { SfxName.BleatElite, MixBus.Sfx, true, 0.50f, 460 },
                { SfxName.BleatKing, MixBus.Sfx, true, 0.70f, 1000 },
                { SfxName.ChargeWarn, MixBus.Sfx, true, 0.80f, 950 },
                { SfxName.Berserk, MixBus.Sfx, true, 0.90f, 1400 },
                { SfxName.Reload, MixBus.Sfx, false, 0.60f, 700 },
                { SfxName.UiClick, MixBus.Sfx, false, 0.50f, 90 },
                { SfxName.Victory, MixBus.Sfx, false, 0.60f, 1200 },
                { SfxName.Defeat, MixBus.Sfx, false, 0.60f, 1500 },
                { SfxName.AmbientPasture, MixBus.Music, false, 0.35f, 4000 },
                { SfxName.AmbientTension, MixBus.Music, false, 0.30f, 4000 },
            };
            for (var i = 0; i < 17; i++)
            {
                var name = (SfxName)expected[i, 0];
                var recipe = SfxLibrary.RecipeOf(name);
                SelfTest.True(recipe.Name == name, "配方 " + name + " 按名字索引", recipe.Name.ToString());
                SelfTest.True(recipe.Bus == (MixBus)expected[i, 1], name + " 总线", recipe.Bus.ToString());
                SelfTest.True(recipe.Spatial == (bool)expected[i, 2], name + " 3D 开关", recipe.Spatial.ToString());
                SelfTest.True(recipe.Gain == (float)expected[i, 3], name + " 增益", recipe.Gain.ToString("R"));
                SelfTest.Equal((long)(int)expected[i, 4], (long)recipe.DurationMs);
                SelfTest.True(SfxLibrary.GainOf(name) == recipe.Gain, name + " 增益表与配方一致", SfxLibrary.GainOf(name).ToString("R"));
            }
            SelfTest.True(SfxLibrary.RecipeOf(SfxName.AmbientPasture).Loop, "环境循环标记", "非循环");
            SelfTest.True(!SfxLibrary.RecipeOf(SfxName.Rifle).Loop, "枪声不是循环", "是循环");
            // 字幕总线不接音源：一条配方都不许落在 Subtitle
            var subtitleRecipes = 0;
            for (var i = 0; i < 17; i++) if (all[i].Bus == MixBus.Subtitle) subtitleRecipes += 1;
            SelfTest.Equal(0, subtitleRecipes);
        }

        private static void ChecksRecipeLayers()
        {
            var rifle = SfxLibrary.RecipeOf(SfxName.Rifle);
            SelfTest.True(rifle.Layers.Length == 2, "步枪两层（噪声 + 谐波）", rifle.Layers.Length.ToString());
            SelfTest.True(rifle.Layers[0].Wave == Waveform.Noise, "步枪第一层是噪声", rifle.Layers[0].Wave.ToString());
            SelfTest.True(rifle.Layers[0].AttackS == 0.010f && rifle.Layers[0].DecayS == 0.120f, "噪声 attack/decay 0.010/0.120", rifle.Layers[0].AttackS.ToString("R"));
            SelfTest.True(rifle.Layers[0].Peak == 0.9f, "噪声峰值 0.9", rifle.Layers[0].Peak.ToString("R"));
            SelfTest.True(rifle.Layers[0].Filter == FilterKind.LowPass && rifle.Layers[0].FilterHz == 3000f && rifle.Layers[0].FilterQ == 0.7f, "lowpass 3000Hz Q0.7", rifle.Layers[0].FilterHz.ToString("R"));
            SelfTest.True(rifle.Layers[1].Wave == Waveform.Triangle && rifle.Layers[1].FreqHz == 1500f, "三角波 1500Hz", rifle.Layers[1].FreqHz.ToString("R"));
            SelfTest.True(rifle.Layers[1].Harmonic == 0.5f, "谐波 0.5", rifle.Layers[1].Harmonic.ToString("R"));
            SelfTest.True(rifle.Layers[1].Filter == FilterKind.BandPass && rifle.Layers[1].FilterQ == 1.1f, "bandpass Q1.1", rifle.Layers[1].FilterQ.ToString("R"));

            var shotgun = SfxLibrary.RecipeOf(SfxName.Shotgun);
            SelfTest.True(shotgun.Layers[2].Wave == Waveform.Fm && shotgun.Layers[2].FmHz == 55f && shotgun.Layers[2].FmIndex == 3f, "霰弹 FM 55Hz 指数 3", shotgun.Layers[2].FmIndex.ToString("R"));
            SelfTest.True(SfxLibrary.RecipeOf(SfxName.Berserk).Layers[0].Distortion == 0.85f, "狂暴失真 0.85", SfxLibrary.RecipeOf(SfxName.Berserk).Layers[0].Distortion.ToString("R"));
            var victory = SfxLibrary.RecipeOf(SfxName.Victory);
            SelfTest.True(victory.Layers.Length == 3 && victory.Layers[2].StartS == 0.240f, "胜利三音 0/0.12/0.24", victory.Layers[2].StartS.ToString("R"));
            var tension = SfxLibrary.RecipeOf(SfxName.AmbientTension);
            SelfTest.True(tension.Layers[0].Wave == Waveform.Fm && tension.Layers[0].FmHz == 8f, "紧张环境 FM 8Hz", tension.Layers[0].FmHz.ToString("R"));
            var pasture = SfxLibrary.RecipeOf(SfxName.AmbientPasture);
            SelfTest.True(pasture.Layers[0].GainLfoHz == 0.25f && pasture.Layers[0].GainLfoDepth == 0.15f, "牧场 LFO 0.25Hz ×0.15", pasture.Layers[0].GainLfoDepth.ToString("R"));
        }

        private static void ChecksRender()
        {
            var rifle = SfxLibrary.RecipeOf(SfxName.Rifle);
            var data = Synth.Render(rifle);
            SelfTest.Equal(9600, (long)data.Length);          // 48000 × 0.200s
            SelfTest.Equal((long)Synth.SampleCountOf(4000), 192000L);
            var peak = 0f;
            var energy = 0.0;
            for (var i = 0; i < data.Length; i++) { var a = data[i] < 0f ? -data[i] : data[i]; if (a > peak) peak = a; energy += data[i] * data[i]; }
            SelfTest.True(peak <= 1f, "步枪不削波", peak.ToString("R"));
            SelfTest.True(peak > 0.2f, "步枪有声", peak.ToString("R"));
            SelfTest.True(energy > 0.0, "步枪能量非零", energy.ToString("R"));
            // 归一化必须有判别性：两层 0.9 同相相加 = 1.8，归一化后必须恰好贴到 1.0（删掉归一化这条必红）
            var loud = default(SfxRecipe);
            loud.Name = SfxName.Hit;
            loud.Bus = MixBus.Sfx;
            loud.DurationMs = 100;
            var l1 = default(SynthLayer);
            l1.Wave = Waveform.Sine; l1.FreqHz = 440f; l1.AttackS = 0.001f; l1.DecayS = 0f; l1.Peak = 0.9f;
            var l2 = l1; l2.FreqHz = 440f;
            loud.Layers = new[] { l1, l2 };
            var loudData = Synth.Render(loud);
            var loudPeak = 0f;
            for (var i = 0; i < loudData.Length; i++) { var v = loudData[i] < 0f ? -loudData[i] : loudData[i]; if (v > loudPeak) loudPeak = v; }
            SelfTest.True(loudPeak > 0.999f && loudPeak <= 1.0001f, "两层 0.9 相加被归一化到 1.0", loudPeak.ToString("R"));
            // 环境循环：持续音（没有衰减段）+ LFO 恰好一个整周期，接缝处电平才连得上
            var pasture = SfxLibrary.RecipeOf(SfxName.AmbientPasture);
            var loop = Synth.Render(pasture);
            SelfTest.Equal(192000, (long)loop.Length);
            SelfTest.True(pasture.Layers[0].DecayS == 0f && pasture.Layers[1].DecayS == 0f, "循环层是持续音", pasture.Layers[0].DecayS.ToString("R"));
            var head = pasture.Layers[0].GainLfoHz * pasture.DurationMs / 1000f;
            SelfTest.True(Math.Abs(head - 1.0f) < 1e-6f, "4s 循环 = 一个整 LFO 周期", head.ToString("R"));
            var loopPeak = 0f;
            for (var i = 0; i < loop.Length; i++) { var v = loop[i] < 0f ? -loop[i] : loop[i]; if (v > loopPeak) loopPeak = v; }
            SelfTest.True(loopPeak > 0.05f && loopPeak <= 1f, "环境循环有声且不削波", loopPeak.ToString("R"));
            // 包络形状（渲染器真正用的那个函数）
            SelfTest.True(Math.Abs(Synth.Envelope(0.0, 0.01f, 0.1f) - Synth.EnvelopeFloorGain) < 1e-6f, "起点在地板", Synth.Envelope(0.0, 0.01f, 0.1f).ToString("R"));
            SelfTest.True(Synth.Envelope(0.01, 0.01f, 0.1f) > 0.99f, "attack 结束到 1", Synth.Envelope(0.01, 0.01f, 0.1f).ToString("R"));
            SelfTest.True(Synth.Envelope(0.06, 0.01f, 0.1f) < 1f && Synth.Envelope(0.06, 0.01f, 0.1f) > 0f, "衰减中途在 (0,1)", Synth.Envelope(0.06, 0.01f, 0.1f).ToString("R"));
            SelfTest.True(Synth.Envelope(0.11, 0.01f, 0.1f) < 0.001f, "decay 末尾接近零", Synth.Envelope(0.11, 0.01f, 0.1f).ToString("R"));
            SelfTest.True(Synth.Envelope(0.12, 0.01f, 0.1f) == 0f, "decay 之后归零", Synth.Envelope(0.12, 0.01f, 0.1f).ToString("R"));
            SelfTest.True(Synth.Envelope(2.0, 0.2f, 0f) > 0.99f, "无衰减层保持持续音", Synth.Envelope(2.0, 0.2f, 0f).ToString("R"));
            SelfTest.True(Synth.Envelope(0.0002, 0f, 0.1f) < 0.5f, "起音至少 1ms（不吃掉瞬态）", Synth.Envelope(0.0002, 0f, 0.1f).ToString("R"));
            // 噪声可复现：先 ResetNoise 再渲染两次应逐样本一致（也保证用例之间不互相污染）
            Synth.ResetNoise();
            var first = Synth.Render(SfxLibrary.RecipeOf(SfxName.Hit));
            Synth.ResetNoise();
            var second = Synth.Render(SfxLibrary.RecipeOf(SfxName.Hit));
            var identical = first.Length == second.Length;
            for (var i = 0; identical && i < first.Length; i++) if (first[i] != second[i]) identical = false;
            SelfTest.True(identical, "同种子渲染逐样本可复现", "不确定");
        }

        private static void ChecksBuses()
        {
            var mixer = new Mixer();
            SelfTest.True(Mixer.ConstGainOf(MixBus.Master) == 1.00f, "master 恒定 1.0", Mixer.ConstGainOf(MixBus.Master).ToString("R"));
            SelfTest.True(Mixer.ConstGainOf(MixBus.Sfx) == 0.80f, "sfx 恒定 0.8", Mixer.ConstGainOf(MixBus.Sfx).ToString("R"));
            SelfTest.True(Mixer.ConstGainOf(MixBus.Music) == 0.50f, "music 恒定 0.5", Mixer.ConstGainOf(MixBus.Music).ToString("R"));
            SelfTest.True(Mixer.ConstGainOf(MixBus.Subtitle) == 0.00f, "subtitle 恒定 0", Mixer.ConstGainOf(MixBus.Subtitle).ToString("R"));
            SelfTest.True(mixer.VolumeOf(MixBus.Sfx) == 0.80f, "sfx 默认音量 0.8", mixer.VolumeOf(MixBus.Sfx).ToString("R"));
            SelfTest.True(mixer.VolumeOf(MixBus.Master) == 0.80f, "master 默认音量 0.8", mixer.VolumeOf(MixBus.Master).ToString("R"));
            SelfTest.True(mixer.VolumeOf(MixBus.Music) == 0.50f, "music 默认 0.5", mixer.VolumeOf(MixBus.Music).ToString("R"));
            mixer.SetVolume(MixBus.Sfx, 0.25f);
            SelfTest.True(mixer.VolumeOf(MixBus.Sfx) == 0.25f, "设置后读回 0.25", mixer.VolumeOf(MixBus.Sfx).ToString("R"));
            mixer.SetVolume(MixBus.Sfx, 9f);
            SelfTest.True(mixer.VolumeOf(MixBus.Sfx) == 1f, "上限夹到 1", mixer.VolumeOf(MixBus.Sfx).ToString("R"));
            mixer.SetVolume(MixBus.Sfx, -9f);
            SelfTest.True(mixer.VolumeOf(MixBus.Sfx) == 0f, "下限夹到 0", mixer.VolumeOf(MixBus.Sfx).ToString("R"));
            mixer.SetVolume(MixBus.Subtitle, 0.9f);
            SelfTest.True(mixer.VolumeOf(MixBus.Subtitle) == 0f, "字幕总线音量恒为 0", mixer.VolumeOf(MixBus.Subtitle).ToString("R"));
        }

        private static void ChecksVoices()
        {
            var mixer = new Mixer();
            for (var i = 0; i < 24; i++)
            {
                mixer.Play(SfxName.Rifle, null, 1f);
                mixer.Tick(40f);          // 越过 30ms 去重窗口
            }
            SelfTest.True(mixer.VoiceCount <= Mixer.MaxVoices, "并发不超 24", mixer.VoiceCount.ToString());
            // 同配方窗口内去重
            var dedupe = new Mixer();
            dedupe.Play(SfxName.Pistol, null, 1f);
            dedupe.Tick(10f);
            dedupe.Play(SfxName.Pistol, null, 1f);
            SelfTest.Equal(1, (long)dedupe.VoiceCount);
            SelfTest.Equal(1, dedupe.DedupedCount);
            dedupe.Tick(30f);
            dedupe.Play(SfxName.Pistol, null, 1f);
            SelfTest.Equal(2, (long)dedupe.VoiceCount);
            // 同配方上限 4
            var perRecipe = new Mixer();
            for (var i = 0; i < 8; i++) { perRecipe.Play(SfxName.Hit, new Vector3(i * 5f, 0f, 0f), 1f); perRecipe.Tick(40f); }
            var hits = 0;
            for (var i = 0; i < Mixer.MaxVoices; i++) if (perRecipe.VoiceAt(i).Active && perRecipe.VoiceAt(i).Name == SfxName.Hit) hits += 1;
            SelfTest.True(hits <= Mixer.MaxVoicesPerRecipe, "同配方不超 4 条", hits.ToString());
            // 最远抢占：新的一声进来时，先被抢掉的是最远那条
            // 6 条长配方 × 4 = 24（每条都在同配方上限内），位置一次比一次远
            var fill = new[] { SfxName.Shotgun, SfxName.BleatRam, SfxName.BleatKing, SfxName.Berserk, SfxName.ChargeWarn, SfxName.BleatElite };
            var preempt = new Mixer();
            var placed = 0;
            for (var group = 0; group < 4; group++)
            {
                for (var r = 0; r < fill.Length; r++)
                {
                    placed += 1;
                    preempt.Play(fill[r], new Vector3(placed * 10f, 0f, 0f), 1f);
                }
                preempt.Tick(40f);
            }
            SelfTest.True(preempt.VoiceCount == Mixer.MaxVoices, "填满 24 条", preempt.VoiceCount.ToString());
            // 全满时再来一条（配方计数没满，走不到同配方抢占）→ 抢掉最远的那条
            preempt.Play(SfxName.UiClick, null, 1f);
            var farthestAlive = false;
            var uiClickAlive = false;
            for (var i = 0; i < Mixer.MaxVoices; i++)
            {
                if (!preempt.VoiceAt(i).Active) continue;
                if (preempt.VoiceAt(i).Position.x >= 240f) farthestAlive = true;
                if (preempt.VoiceAt(i).Name == SfxName.UiClick) uiClickAlive = true;
            }
            SelfTest.True(!farthestAlive, "最远的那条被抢掉", "还在");
            SelfTest.True(uiClickAlive, "新的一条进来了", "没进来");
            SelfTest.True(preempt.VoiceCount == Mixer.MaxVoices, "抢占后仍是满的", preempt.VoiceCount.ToString());
            // 到期回收：槽位必须释放，且能重新被占用
            var recycle = new Mixer();
            recycle.Play(SfxName.UiClick, null, 1f);
            SelfTest.Equal(1, (long)recycle.VoiceCount);
            recycle.Tick(200f);
            SelfTest.Equal(0, (long)recycle.VoiceCount);
            SelfTest.True(!recycle.VoiceAt(0).Active, "到期后槽位释放", "仍占用");
            recycle.Play(SfxName.UiClick, null, 1f);
            SelfTest.Equal(1, (long)recycle.VoiceCount);
            // 启动装配点：接缝 + 预热（生产路径，不是只给测试用）
            var bootstrap = Mixer.EnsureStarted();
            SelfTest.True(bootstrap.Bound && !AudioSeam.IsSilent, "Mixer 接进 AudioSeam", "仍是静音");
            SelfTest.Equal(17, (long)SfxLibrary.PrewarmedCount);
            AudioSeam.Play(SfxName.UiClick, null, 1f);
            SelfTest.Equal(1, (long)AudioSeam.VoiceCount);
            AudioSeam.Bind(null);
            SelfTest.True(AudioSeam.IsSilent, "Bind(null) 复位为静音", "没复位");
            // 3D 衰减
            var listenerObject = new GameObject("Listener");
            listenerObject.transform.position = Vector3.zero;
            var distanceMixer = new Mixer();
            distanceMixer.AttachListener(listenerObject.transform);
            SelfTest.True(distanceMixer.DistanceGain(Vector3.zero) == 1f, "听者处增益 1", distanceMixer.DistanceGain(Vector3.zero).ToString("R"));
            // inverse 模型没有硬性归零点：maxDistance 只用于抢占排序（Unity 的 inverse 曲线在 maxDistance 处不跳变）
            var far = distanceMixer.DistanceGain(new Vector3(40f, 0f, 0f));
            SelfTest.True(far > 0f && far < 0.1f, "最远处很小但不为零", far.ToString("R"));
            SelfTest.True(distanceMixer.DistanceGain(new Vector3(80f, 0f, 0f)) < far, "更远更小（单调）", distanceMixer.DistanceGain(new Vector3(80f, 0f, 0f)).ToString("R"));
            SelfTest.True(distanceMixer.DistanceGain(new Vector3(15f, 0f, 0f)) > 0f, "中途还有声", distanceMixer.DistanceGain(new Vector3(15f, 0f, 0f)).ToString("R"));
            // 字幕总线不接音源
            var subtitleMixer = new Mixer();
            subtitleMixer.Play(SfxName.Victory, null, 1f);
            SelfTest.Equal(1, (long)subtitleMixer.VoiceCount);
            UnityEngine.Object.DestroyImmediate(listenerObject);
        }

        private static void ChecksLayers()
        {
            SelfTest.True(Layers.DeathBleatGain == 0.40f, "死亡咩叫 0.40", Layers.DeathBleatGain.ToString("R"));
            SelfTest.True(Layers.DownedHitGain == 0.50f, "倒地受击 0.50", Layers.DownedHitGain.ToString("R"));
            SelfTest.True(Layers.ChargeWarnCooldownMs == 1600f, "冲锋预警冷却 1600ms", Layers.ChargeWarnCooldownMs.ToString("R"));
            SelfTest.True(Layers.StaleMs == 2000f, "清理阈值 2000ms", Layers.StaleMs.ToString("R"));
            SelfTest.True(Layers.BleatGain(1f) == 1.0f && Layers.BleatGain(26f) == 0.45f, "距离增益端点 1.0/0.45", Layers.BleatGain(26f).ToString("R"));
            SelfTest.True(Math.Abs(Layers.BleatGain(15f) - 0.725f) < 1e-4f, "中点线性 0.725", Layers.BleatGain(15f).ToString("R"));
            var interval = Layers.BleatIntervalMs(7, 3);
            SelfTest.True(interval >= 3600 && interval < 7000, "羊叫间隔落在 3.6–7s", interval.ToString());
            SelfTest.True(Layers.BleatIntervalMs(7, 3) == interval, "同 id/盐 稳定", Layers.BleatIntervalMs(7, 3).ToString());
            SelfTest.True(Layers.BleatIntervalMs(8, 3) != interval, "不同 id 不同间隔", Layers.BleatIntervalMs(8, 3).ToString());

            var layers = new Layers();
            var sheep = new SheepAudioView[2];
            sheep[0].EntityId = 11;
            sheep[0].Position = new Vector3(1f, 0f, 0f);
            sheep[0].Kind = 0;
            sheep[0].Alive = true;
            sheep[1].EntityId = 12;
            sheep[1].Position = new Vector3(30f, 0f, 0f);
            sheep[1].Kind = 3;
            sheep[1].Alive = true;
            var world = default(AudioWorldView);
            world.Sheep = sheep;
            world.Count = 2;
            layers.Update(16f, world, Vector3.zero);
            SelfTest.Equal(2, layers.TrackedCount);
            sheep[1].Windup = true;
            layers.Update(16f, world, Vector3.zero);
            SelfTest.Equal(1, layers.ChargeWarnCount);        // 首次前摇
            layers.Update(16f, world, Vector3.zero);
            SelfTest.Equal(1, layers.ChargeWarnCount);        // 持续前摇不再触发
            SelfTest.True(layers.Subtitles.LineCount >= 1, "冲锋预警进字幕", layers.Subtitles.LineCount.ToString());
            sheep[1].Alive = false;
            layers.Update(16f, world, Vector3.zero);
            SelfTest.True(layers.DeathBleatCount >= 1, "死亡咩叫", layers.DeathBleatCount.ToString());
            // 2s 没再看到 → 清理
            var empty = default(AudioWorldView);
            empty.Sheep = new SheepAudioView[0];
            empty.Count = 0;
            layers.Update(1999f, empty, Vector3.zero);
            SelfTest.Equal(2, layers.TrackedCount);
            layers.Update(2f, empty, Vector3.zero);
            SelfTest.Equal(0, layers.TrackedCount);
            // 槽位回收后必须整槽清零：旧实体的前摇与冷却不能吃掉新实体的首次预警，也不能串音
            var reused = new Layers();
            var one = new SheepAudioView[1];
            one[0].EntityId = 21;
            one[0].Alive = true;
            var single = default(AudioWorldView);
            single.Sheep = one;
            single.Count = 1;
            reused.Update(16f, single, Vector3.zero);
            one[0].Windup = true;
            reused.Update(16f, single, Vector3.zero);
            SelfTest.Equal(1, reused.ChargeWarnCount);
            reused.Update(2000f, default(AudioWorldView), Vector3.zero);
            SelfTest.Equal(0, reused.TrackedCount);
            one[0].EntityId = 22;
            reused.Update(16f, single, Vector3.zero);
            SelfTest.Equal(2, reused.ChargeWarnCount);       // 新实体首次前摇必须能再触发
            // 羊叫距离用真实听者距离（听者就在羊身边 vs 远在 999m）
            var spaced = new Layers();
            var spacedView = new SheepAudioView[1];
            spacedView[0].EntityId = 31;
            spacedView[0].Alive = true;
            spacedView[0].Position = new Vector3(999f, 0f, 0f);
            var spacedWorld = default(AudioWorldView);
            spacedWorld.Sheep = spacedView;
            spacedWorld.Count = 1;
            spaced.Update(16f, spacedWorld, Vector3.zero);
            SelfTest.Equal(0, (long)spaced.BleatCount);          // 刚看到的羊不会立刻叫
            for (var step = 0; step < 4; step++) spaced.Update(1900f, spacedWorld, Vector3.zero);   // 间隔 3.6–7s
            SelfTest.True(spaced.BleatCount >= 1, "远距离也按间隔叫", spaced.BleatCount.ToString());
            SelfTest.True(global::Ac.UI.Layers.BleatGain(0.5f) > global::Ac.UI.Layers.BleatGain(999f), "近处增益更大", spaced.BleatCount.ToString());

            var events = new Layers();
            var hit = default(HudEvent);
            hit.Type = EventType.PlayerHit;
            hit.HitFlags = 1;
            events.HandleEvent(hit, Vector3.zero, 0, 0);
            var kill = default(HudEvent);
            kill.Type = EventType.SheepKilled;
            events.HandleEvent(kill, Vector3.zero, 0, 0);
            SelfTest.True(events.DeathBleatCount == 1, "击杀位触发死亡咩叫", events.DeathBleatCount.ToString());
            var wave = default(HudEvent);
            wave.Type = EventType.WaveStart;
            wave.Wave = 3;
            events.HandleEvent(wave, Vector3.zero, 0, 0);
            var ended = default(HudEvent);
            ended.Type = EventType.MatchEnded;
            events.HandleEvent(ended, Vector3.zero, 1, 0);      // winner=1 / self=0 → 败方
            var defeatText = Subtitles.TextOf(SubtitleCue.Defeat, 0);
            var sawDefeat = false;
            for (var i = 0; i < events.Subtitles.Lines.Count; i++) if (events.Subtitles.Lines[i] == defeatText) sawDefeat = true;
            SelfTest.True(sawDefeat, "败方拿到失败文案（不是恒胜利）", "只看到胜利");
            var waveText = Subtitles.TextOf(SubtitleCue.WaveStart, 3);
            var sawWave = false;
            for (var i = 0; i < events.Subtitles.Lines.Count; i++) if (events.Subtitles.Lines[i] == waveText) sawWave = true;
            SelfTest.True(sawWave, "波次进字幕", "没进");
            SelfTest.True(events.NotifyFire(1) && events.NotifyFire(2) && events.NotifyFire(0), "开火转发三槽位", "失败");
        }

        private static void ChecksSubtitles()
        {
            SelfTest.True(Subtitles.LineMs == 2600f, "字幕 2600ms", Subtitles.LineMs.ToString("R"));
            SelfTest.Equal(3, (long)Subtitles.MaxLines);
            SelfTest.True(Subtitles.TextOf(SubtitleCue.ChargeWarn, 0).Length > 0, "冲锋预警有文案", "空");
            SelfTest.True(Subtitles.TextOf(SubtitleCue.WaveStart, 4).Contains("4"), "波次文案带波号", Subtitles.TextOf(SubtitleCue.WaveStart, 4));
            SelfTest.True(Subtitles.TextOf(SubtitleCue.Victory, 0) != Subtitles.TextOf(SubtitleCue.Defeat, 0), "胜负文案不同", "相同");
            var subtitles = new Subtitles();
            subtitles.Push(SubtitleCue.WaveStart, 1);
            subtitles.Push(SubtitleCue.ChargeWarn, 0);
            subtitles.Push(SubtitleCue.Downed, 0);
            SelfTest.Equal(3, subtitles.LineCount);
            subtitles.Push(SubtitleCue.Victory, 0);
            SelfTest.Equal(1, subtitles.OverflowCount);
            SelfTest.Equal(3, subtitles.LineCount);
            var before = subtitles.PushCount;
            subtitles.Push(SubtitleCue.Downed, 0);            // 同文本 → 只刷新计时
            SelfTest.Equal(before, (long)subtitles.PushCount);
            SelfTest.Equal(1, subtitles.DedupedCount);
            subtitles.Tick(2599f);
            SelfTest.True(subtitles.LineCount == 3, "2599ms 还在", subtitles.LineCount.ToString());
            subtitles.Tick(2f);
            SelfTest.Equal(0, subtitles.LineCount);
        }

        // 注释里提到别的程序集名不算依赖：门禁只看真代码
        private static string StripComments(string[] lines)
        {
            var builder = new System.Text.StringBuilder();
            for (var i = 0; i < lines.Length; i++)
            {
                var line = lines[i];
                var cut = line.IndexOf("//", StringComparison.Ordinal);
                if (cut >= 0) line = line.Substring(0, cut);
                builder.Append(line).Append('\n');
            }
            return builder.ToString();
        }

        private static void ChecksDirection()
        {
            var audioDirectory = Path.Combine(Application.dataPath, "Scripts", "Audio");
            var uiDirectory = Path.Combine(Application.dataPath, "Scripts", "UI");
            var coreDirectory = Path.Combine(Application.dataPath, "Scripts", "Core");
            SelfTest.True(Directory.Exists(audioDirectory), "Audio 目录存在", audioDirectory);
            var audioFiles = Directory.GetFiles(audioDirectory, "*.cs");
            SelfTest.True(audioFiles.Length >= 3, "三个音频文件都在", audioFiles.Length.ToString());
            var clipCreate = 0;
            var audioToSim = 0;
            for (var i = 0; i < audioFiles.Length; i++)
            {
                var lines = File.ReadAllLines(audioFiles[i]);
                for (var l = 0; l < lines.Length; l++)
                {
                    var trimmed = lines[l].TrimStart();
                    if (trimmed.StartsWith("//", StringComparison.Ordinal)) continue;   // 注释里的调用点不算
                    if (trimmed.Contains("AudioClip.Create")) clipCreate += 1;
                }
                var text = StripComments(File.ReadAllLines(audioFiles[i]));
                if (text.Contains("Ac.Sim") || text.Contains("SimEvent")) audioToSim += 1;
            }
            SelfTest.Equal(1, clipCreate);                    // 唯一调用点在 Synth
            SelfTest.Equal(0, audioToSim);
            var uiFiles = Directory.GetFiles(uiDirectory, "*.cs");
            var uiToAudio = 0;
            var uiUsesSeam = 0;
            for (var i = 0; i < uiFiles.Length; i++)
            {
                var text = StripComments(File.ReadAllLines(uiFiles[i]));
                if (text.Contains("Ac.Audio") || text.Contains("new Mixer")) uiToAudio += 1;
                if (text.Contains("AudioSeam")) uiUsesSeam += 1;
            }
            SelfTest.Equal(0, uiToAudio);
            SelfTest.True(uiUsesSeam >= 1, "UI 经 AudioSeam 调音频", uiUsesSeam.ToString());
            var coreText = File.ReadAllText(Path.Combine(coreDirectory, "AudioSeam.cs"));
            SelfTest.True(coreText.Contains("IAudioSeam") && coreText.Contains("MixBus") && coreText.Contains("SfxName"), "缝定义齐全", "缺失");
            SelfTest.True(AudioSeam.IsSilent, "默认是静音实现（没接音频也不崩）", "非静音");
            AudioSeam.SetVolume(MixBus.Sfx, 0.5f);
            AudioSeam.Play(SfxName.UiClick, null, 1f);
            SelfTest.Equal(0, AudioSeam.VoiceCount);
        }
    }
}
