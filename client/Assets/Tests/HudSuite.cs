using System;
using System.IO;
using Ac.Core;
using Ac.Net;
using Ac.UI;
using EventType = Ac.Net.EventType;   // UnityEngine 也有 EventType，取别名消歧
using UnityEngine;

namespace Ac.Tests
{
    // C10 §6/§7：元素映射（经 Hud 的事件与采样两条路径）、10Hz 节流、准星、字号与安全区、可见性、波次与击杀记录、零分配。
    public static class HudSuite
    {
        public static void Register()
        {
            SelfTest.Add("hud.element_map", ChecksElementMap);
            SelfTest.Add("hud.throttle", ChecksThrottle);
            SelfTest.Add("hud.crosshair", ChecksCrosshair);
            SelfTest.Add("hud.ammo_rage", ChecksAmmoRage);
            SelfTest.Add("hud.visibility", ChecksVisibility);
            SelfTest.Add("hud.wave_killfeed", ChecksWaveKillFeed);
            SelfTest.Add("hud.text_and_font", ChecksTextAndFont);
            SelfTest.Add("hud.revive_step", ChecksReviveStep);
            SelfTest.Add("hud.zero_alloc", ChecksZeroAlloc);
        }

        private static HudSample Sample()
        {
            var sample = default(HudSample);
            sample.HpRatio255 = 200;
            sample.Mag = 30;
            sample.MagSize = 30;
            sample.Reserve = 48;
            sample.ReloadLeft10Ms = 14000;
            sample.Rage = 60;
            sample.RageLeft100Ms = 25000;
            sample.ReviveRatio255 = 128;
            sample.NearestAllyDistanceM = 5f;
            sample.Wave = 3;
            sample.IntermissionMs = 12000;
            sample.Phase = Hud.PhasePlaying;
            return sample;
        }

        private static void ChecksElementMap()
        {
            var hud = new Hud();
            var sample = Sample();
            sample.TargetInSight = true;
            hud.Apply(sample);
            SelfTest.Equal(78, (long)hud.DisplayedHp);        // 200/255 → 78%（先取整再比较/显示）
            SelfTest.Equal(30, (long)hud.DisplayedMag);
            SelfTest.Equal(48, (long)hud.DisplayedReserve);
            SelfTest.Equal(60, (long)hud.DisplayedRage);
            SelfTest.Equal(3, (long)hud.DisplayedWave);
            SelfTest.Equal(12000, hud.DisplayedIntermissionMs);
            SelfTest.Equal(30, (long)hud.Ammo.Mag);
            SelfTest.Equal(48, (long)hud.Ammo.Reserve);
            SelfTest.True(hud.Ammo.ReloadRingMs == 1400f, "换弹环 = reloadLeft10Ms/10", hud.Ammo.ReloadRingMs.ToString("R"));
            SelfTest.Equal(60, (long)hud.Rage.Rage);
            SelfTest.True(hud.Rage.RageLeftMs == 250f, "狂暴剩余 = rageLeft100Ms/100", hud.Rage.RageLeftMs.ToString("R"));
            SelfTest.Equal((long)CrosshairState.Target, (long)hud.Crosshair.State);   // 采样 → 准星可命中态
            SelfTest.True(Math.Abs(hud.Revive.Progress - 0.50196f) < 1e-4f, "救援进度 ← reviveRatio255/255（不量化）", hud.Revive.Progress.ToString("R"));

            // 事件 → 元素（走 Hud.PushEvent，不是直接调元素）
            var hit = default(HudEvent);
            hit.Type = EventType.PlayerHit;
            hit.HitFlags = 4;
            SelfTest.True(hud.PushEvent(hit), "命中/击杀事件被接受", "被拒");
            SelfTest.Equal((long)CrosshairState.Hurt, (long)hud.Crosshair.State);
            SelfTest.Equal(1, hud.Feed.Count);
            SelfTest.True(hud.Feed.IsHeadshot(0) == false, "非爆头样式", "爆头");
            var kill = default(HudEvent);
            kill.Type = EventType.SheepKilled;
            kill.HitFlags = 1;
            SelfTest.True(hud.PushEvent(kill), "击杀事件被接受", "被拒");
            SelfTest.True(hud.Feed.IsHeadshot(1), "爆头样式 ← HIT_FLAG.headshot", "非爆头");
            SelfTest.Equal(Hud.ColorTarget, hud.Feed.ColorOf(1));
            var waveEvent = default(HudEvent);
            waveEvent.Type = EventType.WaveStart;
            waveEvent.Wave = 4;
            SelfTest.True(hud.PushEvent(waveEvent), "波次事件被接受", "被拒");
            SelfTest.Equal(4, hud.Banner.CurrentWave);
            var reviveEvent = default(HudEvent);
            reviveEvent.Type = EventType.ReviveProgress;
            reviveEvent.ReviveRatio255 = 128;
            SelfTest.True(hud.PushEvent(reviveEvent), "救援进度事件被接受", "被拒");
            SelfTest.True(hud.Revive.Visible, "救援进度事件让提示可见", "不可见");
            var done = default(HudEvent);
            done.Type = EventType.ReviveDone;
            SelfTest.True(hud.PushEvent(done), "救援完成事件被接受", "被拒");
            SelfTest.True(!hud.Revive.Visible, "救援完成后隐藏", "仍可见");
            var unknown = default(HudEvent);
            unknown.Type = EventType.MatchEnded;
            SelfTest.True(!hud.PushEvent(unknown), "无关事件被拒", "接受了");
        }

        private static void ChecksThrottle()
        {
            SelfTest.True(UiThrottle.NumericRefreshMs == 100f, "数值类 100ms（≤10Hz）", UiThrottle.NumericRefreshMs.ToString("R"));
            SelfTest.True(UiThrottle.StatsRefreshMs == 250f, "统计行 250ms", UiThrottle.StatsRefreshMs.ToString("R"));
            var hud = new Hud();
            var sample = Sample();
            hud.Apply(sample);
            SelfTest.Equal(1, (long)hud.Throttle.NumericWrites);
            for (var i = 0; i < 4; i++)
            {
                hud.Tick(20f);
                sample.Mag = 30 - i;
                hud.Apply(sample);
            }
            SelfTest.Equal(1, (long)hud.Throttle.NumericWrites);      // 80ms 内一次都没到窗口
            SelfTest.Equal(30, (long)hud.DisplayedMag);              // 显示值也还没动
            hud.Tick(30f);
            hud.Apply(sample);                                        // 累计 110ms → 窗口到了
            SelfTest.Equal(2, (long)hud.Throttle.NumericWrites);
            SelfTest.Equal(27, (long)hud.DisplayedMag);
            hud.Tick(100f);
            hud.Apply(sample);                                        // 窗口到了但值没变 → 不写
            SelfTest.Equal(2, (long)hud.Throttle.NumericWrites);
            SelfTest.True(hud.Throttle.SkippedWrites > 0, "有被拦下的写", hud.Throttle.SkippedWrites.ToString());
            // 状态类事件立即刷新且不清数值窗口
            var evt = default(HudEvent);
            evt.Type = EventType.ReviveProgress;
            evt.ReviveRatio255 = 25;
            var writes = hud.Throttle.NumericWrites;
            SelfTest.True(hud.PushEvent(evt), "状态类事件立即生效", "被拒");
            SelfTest.Equal(1, (long)hud.Throttle.EventWrites);
            SelfTest.Equal(writes, (long)hud.Throttle.NumericWrites);   // 事件不冒充数值写
            hud.Apply(sample);
            SelfTest.Equal(writes, (long)hud.Throttle.NumericWrites);   // 事件也不清窗口
        }

        private static void ChecksCrosshair()
        {
            SelfTest.Equal(4, (long)Crosshair.SegmentCount);
            SelfTest.Equal(0xE8E8F0, (long)Hud.ColorNormal);
            SelfTest.Equal(0xFFD24D, (long)Hud.ColorTarget);
            SelfTest.Equal(0xD2402F, (long)Hud.ColorHurt);
            SelfTest.True(Hud.HurtFlashMs == 150f, "受伤色 150ms", Hud.HurtFlashMs.ToString("R"));
            var crosshair = new Crosshair();
            crosshair.SetSpread(0.5f);
            SelfTest.True(crosshair.SizePx == 2f, "0.5° → 2px", crosshair.SizePx.ToString("R"));
            crosshair.SetSpread(5f);
            SelfTest.True(crosshair.SizePx == 24f, "5° → 24px", crosshair.SizePx.ToString("R"));
            crosshair.SetSpread(2.75f);
            SelfTest.True(Math.Abs(crosshair.SizePx - 13f) < 1e-4f, "线性映射中点", crosshair.SizePx.ToString("R"));
            crosshair.SetSpread(0.1f);
            SelfTest.True(crosshair.SpreadDeg == 0.5f, "下界夹取", crosshair.SpreadDeg.ToString("R"));
            crosshair.SetSpread(9f);
            SelfTest.True(crosshair.SpreadDeg == 5f, "上界夹取", crosshair.SpreadDeg.ToString("R"));
            crosshair.SetState(CrosshairState.Target);
            SelfTest.Equal(0xFFD24D, (long)crosshair.CurrentColor);
            crosshair.MarkHurt();
            SelfTest.Equal(0xD2402F, (long)crosshair.CurrentColor);
            crosshair.Tick(149f);
            SelfTest.Equal((long)CrosshairState.Hurt, (long)crosshair.State);
            crosshair.Tick(1f);
            SelfTest.Equal((long)CrosshairState.Normal, (long)crosshair.State);
            crosshair.SetState(CrosshairState.Hidden);
            SelfTest.True(!crosshair.Visible, "隐藏态 Visible=false", "可见");
            crosshair.MarkHurt();
            SelfTest.Equal((long)CrosshairState.Hidden, (long)crosshair.State);   // 隐藏时受伤不复活
        }

        private static void ChecksAmmoRage()
        {
            var ammo = new AmmoCounter();
            ammo.Set(9, 0, 0, 30, false);
            SelfTest.True(!ammo.IsLow, "步枪 9/30 = 30% 不算低", ammo.IsLow.ToString());
            ammo.Set(8, 0, 0, 30, false);
            SelfTest.True(ammo.IsLow, "步枪 8/30 变红", ammo.IsLow.ToString());
            SelfTest.Equal(0xD2402F, (long)ammo.Color);
            ammo.Set(2, 0, 0, 6, false);
            SelfTest.True(!ammo.IsLow, "霰弹 2/6 = 33% 不算低", ammo.IsLow.ToString());
            ammo.Set(1, 0, 0, 6, false);
            SelfTest.True(ammo.IsLow, "霰弹 1/6 变红", ammo.IsLow.ToString());
            ammo.Set(4, 0, 0, 12, false);
            SelfTest.True(!ammo.IsLow, "手枪 4/12 不算低", ammo.IsLow.ToString());
            SelfTest.Equal(0xE8E8F0, (long)ammo.Color);
            ammo.Set(-1, -1, -1, 12, false);
            SelfTest.True(ammo.Mag == 0 && ammo.Reserve == 0 && !ammo.Reloading, "负数钳到 0", ammo.Mag.ToString());
            ammo.Set(0, 12, 200, 12, false);
            SelfTest.True(ammo.Reloading && ammo.ReloadRingMs == 20f, "换弹环 20ms", ammo.ReloadRingMs.ToString("R"));
            ammo.Set(0, 12, 0, 12, true);
            SelfTest.True(ammo.Reloading, "单播 reloading 也点亮换弹环", ammo.Reloading.ToString());
            SelfTest.True(AmmoCounter.AmmoLowRatio == 0.3f, "低弹比 0.3", AmmoCounter.AmmoLowRatio.ToString("R"));

            var rage = new RageBar();
            rage.Set(99, 0, false);
            SelfTest.True(!rage.CanActivate, "99 不能激活", rage.CanActivate.ToString());
            rage.Set(100, 0, false);
            SelfTest.True(rage.CanActivate, "满值可激活", rage.CanActivate.ToString());
            SelfTest.Equal(0xFFD24D, (long)rage.Color);
            rage.Set(100, 0, true);
            SelfTest.True(!rage.CanActivate, "狂暴期间不再提示激活", rage.CanActivate.ToString());
            SelfTest.Equal(0x7AD1FF, (long)rage.Color);
            rage.Set(150, 12345, true);
            SelfTest.True(rage.Rage == 100, "怒气上限 100", rage.Rage.ToString());
            SelfTest.True(rage.RageLeftMs == 123.45f, "狂暴剩余毫秒", rage.RageLeftMs.ToString("R"));
        }

        private static void ChecksVisibility()
        {
            SelfTest.Equal(1, (long)Hud.PhaseLoading);
            SelfTest.Equal(2, (long)Hud.PhasePlaying);
            SelfTest.Equal(3, (long)Hud.PhaseIntermission);
            SelfTest.Equal(4, (long)Hud.PhaseEnded);
            var hud = new Hud();
            var sample = Sample();
            sample.Downed = true;
            sample.Phase = Hud.PhasePlaying;
            hud.Apply(sample);
            SelfTest.True(hud.Downed.Visible, "倒地 + playing → 显示遮罩", "隐藏");
            sample.Phase = Hud.PhaseLoading;
            hud.Apply(sample);
            SelfTest.True(!hud.Downed.Visible, "倒地 + loading → 隐藏", "显示");
            sample.Phase = Hud.PhaseEnded;
            hud.Apply(sample);
            SelfTest.True(!hud.Downed.Visible, "倒地 + ended → 隐藏", "显示");
            SelfTest.True(!Hud.CombatUiVisible(Hud.PhaseEnded), "ended 隐藏全部战斗 UI", "仍显示");
            SelfTest.True(Hud.CombatUiVisible(Hud.PhasePlaying), "playing 显示战斗 UI", "隐藏");
            SelfTest.True(!Hud.CombatUiVisible(Hud.PhaseLoading), "loading 不显示战斗 UI", "显示");

            sample.Phase = Hud.PhasePlaying;
            hud.Apply(sample);
            SelfTest.True(!Hud.CrosshairVisible(sample) && !Hud.AmmoVisible(sample), "自己倒地隐藏准星与弹药", "仍显示");
            SelfTest.Equal((long)CrosshairState.Hidden, (long)hud.Crosshair.State);
            sample.Downed = false;
            sample.NearestAllyDistanceM = 1.2f;
            hud.Apply(sample);
            SelfTest.True(Hud.CrosshairVisible(sample) && hud.Crosshair.Visible, "站起来准星恢复", "仍隐藏");
            SelfTest.True(hud.Revive.Visible, "2.0m 内有倒地队友 → 提示可见", "隐藏");
            sample.NearestAllyDistanceM = 2.5f;
            hud.Apply(sample);
            SelfTest.True(!hud.Revive.Visible, "超出 2.0m → 隐藏", "可见");
            SelfTest.True(RevivePrompt.RangeM == 2.0f, "救援距离 2.0m", RevivePrompt.RangeM.ToString("R"));
            SelfTest.True(!hud.Revive.CanPrompt(true, 1.0f), "自己倒地不提示救援", "提示了");
        }

        private static void ChecksWaveKillFeed()
        {
            SelfTest.True(WaveBanner.BannerMs == 4500f, "横幅 4500ms", WaveBanner.BannerMs.ToString("R"));
            SelfTest.Equal(3, (long)WaveBanner.QueueCapacity);
            SelfTest.Equal(10, (long)WaveBanner.TickSegments);
            SelfTest.Equal(5, (long)WaveBanner.BossInterval);
            SelfTest.True(WaveBanner.IsBossWave(5) && !WaveBanner.IsBossWave(4), "Boss 波间隔 5", WaveBanner.IsBossWave(5).ToString());
            SelfTest.Equal(0, (long)WaveBanner.TickIndex(1));
            SelfTest.Equal(9, (long)WaveBanner.TickIndex(10));
            SelfTest.Equal(0, (long)WaveBanner.TickIndex(11));

            var banner = new WaveBanner();
            banner.ShowWave(1);
            SelfTest.True(banner.RemainingMs == 4500f, "展示后开始计时", banner.RemainingMs.ToString("R"));
            for (var i = 2; i <= 5; i++) banner.ShowWave(i);
            SelfTest.Equal(3, banner.QueuedCount);
            SelfTest.Equal(1, banner.OverflowCount);          // 队列上限 3
            banner.Tick(4500f);
            banner.Tick(1f);
            SelfTest.Equal(2, banner.CurrentWave);            // 出队按入队顺序
            banner.Tick(4500f);
            banner.Tick(1f);
            SelfTest.Equal(3, banner.CurrentWave);
            banner.SetIntermission(12000);
            SelfTest.Equal(12000, banner.IntermissionMs);
            banner.SetIntermission(-5);
            SelfTest.Equal(0, banner.IntermissionMs);

            SelfTest.True(KillFeed.EntryMs == 3000f, "击杀记录 3000ms", KillFeed.EntryMs.ToString("R"));
            SelfTest.Equal(6, (long)KillFeed.Capacity);
            SelfTest.True(KillFeed.FadeMs == 600f, "渐隐 600ms", KillFeed.FadeMs.ToString("R"));
            var feed = new KillFeed();
            for (var i = 0; i < 6; i++)
            {
                var entry = default(KillEntry);
                entry.VictimId = i;
                feed.Push(entry);
            }
            SelfTest.Equal(6, feed.Count);
            var seventh = default(KillEntry);
            seventh.VictimId = 99;
            feed.Push(seventh);
            SelfTest.Equal(1, feed.OverflowCount);
            SelfTest.Equal(6, feed.Count);
            SelfTest.True(KillFeed.Alpha(3000f) == 1f && KillFeed.Alpha(300f) == 0.5f && KillFeed.Alpha(0f) == 0f, "渐隐曲线", KillFeed.Alpha(300f).ToString("R"));
            SelfTest.True(feed.AlphaOf(0) == 1f, "条目剩余时间映射到 alpha", feed.AlphaOf(0).ToString("R"));
            feed.Tick(3000f);
            SelfTest.Equal(0, feed.Count);
        }

        private static void ChecksTextAndFont()
        {
            SelfTest.Equal(48, (long)Hud.FontTitlePx);
            SelfTest.Equal(32, (long)Hud.FontNumericPx);
            SelfTest.Equal(18, (long)Hud.FontLabelPx);
            SelfTest.Equal(16, (long)Hud.FontFeedPx);
            SelfTest.Equal(48, (long)Hud.FontPx("title"));
            SelfTest.Equal(18, (long)Hud.FontPx("label"));
            SelfTest.True(Hud.SafeAreaPercent == 0.04f, "安全区 4%", Hud.SafeAreaPercent.ToString("R"));
            SelfTest.True(Math.Abs(Hud.SafeAreaInsetPx(1080) - 43.2f) < 1e-3f, "1080p 下内缩 ≥43px", Hud.SafeAreaInsetPx(1080).ToString("R"));
            SelfTest.True(Hud.FontFallback[0] == "Microsoft YaHei" && Hud.FontFallback[1] == "Segoe UI" && Hud.FontFallback[2] == "Arial", "字体回退链", Hud.FontFallback[0]);

            var directory = Path.Combine(Application.dataPath, "Scripts", "UI");
            SelfTest.True(Directory.Exists(directory), "UI 目录存在", directory);
            var files = Directory.GetFiles(directory, "*.cs");
            SelfTest.True(files.Length >= 9, "9 个 UI 文件都在", files.Length.ToString());
            var tmpro = 0;
            for (var i = 0; i < files.Length; i++)
            {
                if (File.ReadAllText(files[i]).IndexOf("TextMeshPro", StringComparison.Ordinal) >= 0) tmpro += 1;
            }
            SelfTest.Equal(0, tmpro);
        }

        private static void ChecksReviveStep()
        {
            SelfTest.True(RevivePrompt.DurationMs == 3000f, "救援总时长 3000ms", RevivePrompt.DurationMs.ToString("R"));
            SelfTest.True(RevivePrompt.ProgressStep == 0.05f, "进度事件步长 5%", RevivePrompt.ProgressStep.ToString("R"));
            SelfTest.True(Hud.ChargeWarningMs == 1600f, "冲锋警戒 1600ms", Hud.ChargeWarningMs.ToString("R"));
            SelfTest.True(Hud.DamageNumberMs == 900f, "伤害数字 900ms", Hud.DamageNumberMs.ToString("R"));
            SelfTest.True(Hud.HpTrailMs == 800f, "血量尾影 800ms", Hud.HpTrailMs.ToString("R"));

            var hud = new Hud();
            var sample = Sample();
            sample.Charging = true;
            hud.Apply(sample);
            SelfTest.True(hud.ChargeWarningRemainingMs == 1600f, "冲锋警戒被 Charging 点亮", hud.ChargeWarningRemainingMs.ToString("R"));
            hud.Tick(1600f);
            SelfTest.True(hud.ChargeWarningRemainingMs == 0f, "冲锋警戒到期", hud.ChargeWarningRemainingMs.ToString("R"));
            hud.Apply(sample);
            SelfTest.True(hud.ChargeWarningRemainingMs == 1600f, "再次冲锋重新点亮", hud.ChargeWarningRemainingMs.ToString("R"));

            var prompt = new RevivePrompt();
            prompt.SetProgress(0.52f);
            SelfTest.True(Math.Abs(prompt.Progress - 0.52f) < 1e-6f, "进度按原值显示（不量化）", prompt.Progress.ToString("R"));
            prompt.SetProgress(2f);
            SelfTest.True(prompt.Progress == 1f, "上限夹取", prompt.Progress.ToString("R"));
            prompt.SetProgress(-1f);
            SelfTest.True(prompt.Progress == 0f, "下限夹取", prompt.Progress.ToString("R"));
            prompt.SetFromRatio255(255);
            SelfTest.True(prompt.Progress == 1f && prompt.RemainingMs == 0f, "255 → 完成", prompt.RemainingMs.ToString("R"));
            prompt.SetFromRatio255(64);
            SelfTest.True(Math.Abs(prompt.Progress - 64f / 255f) < 1e-6f, "64/255 原样显示", prompt.Progress.ToString("R"));
            SelfTest.True(Math.Abs(prompt.RemainingMs - 3000f * (1f - 64f / 255f)) < 1e-3f, "剩余时间按比例", prompt.RemainingMs.ToString("R"));
        }

        private static void ChecksZeroAlloc()
        {
            var hud = new Hud();
            var sample = Sample();
            var evt = default(HudEvent);
            evt.Type = EventType.WaveStart;
            evt.Wave = 4;
            var entry = default(KillEntry);
            entry.VictimId = 7;
            var before = GC.GetAllocatedBytesForCurrentThread();
            for (var frame = 0; frame < 3; frame++)
            {
                sample.Mag = 30 - frame;
                sample.HpRatio255 = 200 - frame;
                hud.Tick(16f);
                hud.Apply(sample);
                hud.PushEvent(evt);
                hud.Feed.Push(entry);
                hud.Banner.SetIntermission(sample.IntermissionMs);
                hud.Crosshair.MarkHurt();
                hud.Revive.SetFromRatio255(sample.ReviveRatio255);
                hud.Rage.Set(sample.Rage, sample.RageLeft100Ms, sample.RageMode);
                hud.Ammo.Set(sample.Mag, sample.Reserve, sample.ReloadLeft10Ms, sample.MagSize, sample.Reloading);
                hud.Downed.Update(sample.Downed, sample.Phase);
            }
            var after = GC.GetAllocatedBytesForCurrentThread();
            SelfTest.Equal(0, after - before);
            SelfTest.Equal(3, hud.Throttle.EventWrites);
            SelfTest.True(hud.Feed.Count > 0, "击杀记录有内容", hud.Feed.Count.ToString());
        }
    }
}
