using System;
using System.IO;
using Ac.Core;
using Ac.View;
using UnityEngine;

namespace Ac.Tests
{
    // C09 §6/§7：视图模型参数、动画节奏表、曳光几何与池、粒子池、三态反馈、屏幕反馈、弹药闸门、零分配。
    public static class EffectsSuite
    {
        public static void Register()
        {
            SelfTest.Add("effects.viewmodel_params", ChecksViewModel);
            SelfTest.Add("effects.anim_table", ChecksAnim);
            SelfTest.Add("effects.tracer_pool", ChecksTracer);
            SelfTest.Add("effects.particle_pool", ChecksParticles);
            SelfTest.Add("effects.hit_marker", ChecksMarker);
            SelfTest.Add("effects.screen_feedback", ChecksScreen);
            SelfTest.Add("effects.ammo_gate", ChecksAmmoGate);
            SelfTest.Add("effects.zero_alloc", ChecksZeroAlloc);
        }

        private static void ChecksViewModel()
        {
            SelfTest.True(ViewModel.ModelFovDeg == 62f, "视图模型 FOV 62", ViewModel.ModelFovDeg.ToString("R"));
            SelfTest.True(ViewModel.NearClipM == 0.01f && ViewModel.FarClipM == 12f, "近 0.01 / 远 12", ViewModel.NearClipM.ToString("R"));
            SelfTest.True(ViewModel.BaseOffset == new Vector3(0.17f, -0.19f, -0.02f), "基准位 (0.17,-0.19,-0.02)", ViewModel.BaseOffset.ToString("R"));
            SelfTest.True(ViewModel.MuzzleOffset == new Vector3(0f, 0.02f, 0.58f), "枪口挂点 (0,0.02,0.58)", ViewModel.MuzzleOffset.ToString("R"));

            var go = new GameObject("EyeCamera");
            var camera = go.AddComponent<Camera>();
            camera.transform.position = new Vector3(3f, 1.6f, -7f);
            camera.transform.rotation = Quaternion.Euler(0f, 33f, 0f);
            var model = new ViewModel();
            model.Attach(camera);
            SelfTest.True(Vector3.Distance(model.EyeWorld, model.MuzzleWorld) >= ViewModel.MinMuzzleEyeDistanceM, "枪口距眼位 ≥ 0.5m", model.MuzzleEyeDistanceM.ToString("R"));
            SelfTest.True(!Tracer.IsCollinearWithEye(model.MuzzleWorld, model.EyeWorld), "起点不是眼位（共线缺陷不成立）", "共线");
            SelfTest.True(model.MuzzleEyeDistanceM <= 0.6f, "枪口距眼位落在 0.5–0.6m 带内", model.MuzzleEyeDistanceM.ToString("R"));
            var expectedBase = camera.transform.rotation * ViewModel.BaseOffset;
            SelfTest.True(Vector3.Distance(model.BaseWorld - model.EyeWorld, expectedBase) < 1e-4f, "基准位是相机空间偏移（随朝向旋转），不是眼位", (model.BaseWorld - model.EyeWorld).ToString("R"));
            var before = model.MuzzleWorld;
            camera.transform.position += new Vector3(0f, 0f, 1f);
            model.Refresh();
            SelfTest.True(model.MuzzleWorld != before, "挂点随眼位刷新", "没刷新");
            model.OnFire();
            SelfTest.Equal(1, (long)model.FireCount);
            UnityEngine.Object.DestroyImmediate(go);
        }

        private static void ChecksAnim()
        {
            var anim = new WeaponAnim();
            SelfTest.Equal(1400, (long)WeaponAnim.ReloadPistolMs);
            SelfTest.Equal(2000, (long)WeaponAnim.ReloadRifleMs);
            SelfTest.Equal(2600, (long)WeaponAnim.ReloadShotgunMs);
            SelfTest.Equal(260, (long)WeaponAnim.SwapDownMs);
            SelfTest.Equal(45, (long)WeaponAnim.MuzzleFlashMs);
            SelfTest.True(WeaponAnim.RecoilPitchDegPerShot == 0.35f && WeaponAnim.RecoilYawJitterDeg == 0.2f, "后坐 0.35°/0.2°", WeaponAnim.RecoilPitchDegPerShot.ToString("R"));
            SelfTest.True(WeaponAnim.RecoilDecayPerSecond == 7.5f, "衰减 7.5/s", WeaponAnim.RecoilDecayPerSecond.ToString("R"));
            SelfTest.True(WeaponAnim.BreathHz == 0.42f && WeaponAnim.WalkHz == 3.4f && WeaponAnim.WalkPhasePerMeter == 0.55f, "呼吸/行走 0.42/3.4/0.55", WeaponAnim.BreathHz.ToString("R"));
            SelfTest.True(WeaponAnim.ReloadPitchRad == 0.42f && WeaponAnim.ReloadRollRad == 0.30f && WeaponAnim.DownedPitchRad == 0.46f, "换弹/倒地姿态角", WeaponAnim.ReloadPitchRad.ToString("R"));

            SelfTest.True(Math.Abs(WeaponAnim.IntervalMs(WeaponAnim.PistolRpm, 1f) - 200f) < 1e-4, "手枪 200ms", WeaponAnim.IntervalMs(300, 1f).ToString("R"));
            SelfTest.True(Math.Abs(WeaponAnim.IntervalMs(WeaponAnim.RifleRpm, 1f) - 100f) < 1e-4, "步枪 100ms", WeaponAnim.IntervalMs(600, 1f).ToString("R"));
            SelfTest.True(Math.Abs(WeaponAnim.IntervalMs(WeaponAnim.ShotgunRpm, 1f) - 60000f / 70f) < 1e-3, "霰弹按公式现算", WeaponAnim.IntervalMs(70, 1f).ToString("R"));
            SelfTest.Equal(1400, (long)anim.ReloadDurationMs(0));
            SelfTest.Equal(2600, (long)anim.ReloadDurationMs(2));
            anim.OnReload(1);
            SelfTest.Equal(2000, (long)anim.ReloadRemainingMs);
            anim.Tick(500f);
            SelfTest.Equal(1500, (long)anim.ReloadRemainingMs);
            SelfTest.True(anim.ReloadPitchRadNow == 0.42f && anim.ReloadRollRadNow == 0.30f, "换弹姿态生效", anim.ReloadPitchRadNow.ToString("R"));
            anim.Tick(5000f);
            SelfTest.True(anim.ReloadRemainingMs == 0f && anim.ReloadPitchRadNow == 0f, "换弹结束回到 0", anim.ReloadRemainingMs.ToString("R"));
            anim.OnSwapDown(0);
            SelfTest.Equal(260, (long)anim.SwapRemainingMs);
            anim.Tick(500f);
            SelfTest.True(anim.SwapRemainingMs == 0f, "切枪下沉结束", anim.SwapRemainingMs.ToString("R"));
            anim.OnDowned(true);
            SelfTest.True(anim.DownedPitchRadNow == 0.46f, "倒地俯角 0.46", anim.DownedPitchRadNow.ToString("R"));

            // 后坐：一发 +0.35°，衰减 7.5/s 需要约 0.047s 归零
            var recoil = new WeaponAnim();
            recoil.OnFire(0);
            SelfTest.True(recoil.RecoilPitchDeg == 0.35f, "一发 +0.35°", recoil.RecoilPitchDeg.ToString("R"));
            SelfTest.True(Math.Abs(recoil.RecoilYawDeg) == 0.2f, "偏航抖动 ±0.2°", recoil.RecoilYawDeg.ToString("R"));
            recoil.OnFire(0);
            SelfTest.True(recoil.RecoilYawDeg == 0f, "抖动左右交替（第二发抵消）", recoil.RecoilYawDeg.ToString("R"));
            recoil.Tick(50f);
            SelfTest.True(recoil.RecoilPitchDeg < 0.7f && recoil.RecoilPitchDeg > 0f, "按 7.5/s 衰减中", recoil.RecoilPitchDeg.ToString("R"));

            // 散布：每发 +0.1、上限 0.25、350ms 后按 6.0°/s 回落
            var spread = new WeaponAnim();
            spread.OnFire(0);
            SelfTest.True(spread.SpreadDeg == 0.1f, "每发 +0.1°", spread.SpreadDeg.ToString("R"));
            for (var i = 0; i < 10; i++) spread.OnFire(0);
            SelfTest.True(spread.SpreadDeg == 0.25f, "上限 0.25°", spread.SpreadDeg.ToString("R"));
            spread.Tick(349f);
            SelfTest.True(spread.SpreadDeg == 0.25f, "350ms 前不回落", spread.SpreadDeg.ToString("R"));
            spread.Tick(5f);   // 累计 354ms：刚过 350ms 延迟，只回落 6.0°/s × 5ms
            SelfTest.True(spread.SpreadDeg < 0.25f && spread.SpreadDeg > 0.2f, "按 6.0°/s 回落", spread.SpreadDeg.ToString("R"));
            var walk = new WeaponAnim();
            walk.AddWalkDistance(1f);
            SelfTest.True(walk.WalkPhase > 0.54f && walk.WalkPhase < 0.56f, "每米相位 0.55", walk.WalkPhase.ToString("R"));
            var breath = new WeaponAnim();
            breath.Tick(1000f);
            SelfTest.True(Math.Abs(breath.BreathPhase - 0.42f * 2f * Mathf.PI) < 1e-3, "呼吸 0.42Hz（每秒 0.42 圈）", breath.BreathPhase.ToString("R"));
        }

        private static void ChecksTracer()
        {
            SelfTest.Equal(32, (long)Tracer.Capacity);
            SelfTest.Equal(120, (long)Tracer.LifetimeMs);
            SelfTest.Equal(30, (long)Tracer.FallbackLengthM);
            var tracer = new Tracer();
            var eye = new Vector3(0f, 1.6f, 0f);
            var muzzle = eye + new Vector3(0f, 0.02f, 0.58f);
            var hit = new Vector3(0f, 1.4f, 25f);
            var index = tracer.Spawn(muzzle, hit);
            SelfTest.Equal(0, index);
            SelfTest.True(tracer.Segments[0].Alive, "落池即存活", "未存活");
            SelfTest.True(tracer.Segments[0].From == muzzle, "起点 = 枪口", tracer.Segments[0].From.ToString("R"));
            SelfTest.True(tracer.Segments[0].To == hit, "终点 = playerHit 命中点", tracer.Segments[0].To.ToString("R"));
            SelfTest.True(!Tracer.IsCollinearWithEye(tracer.Segments[0].From, eye), "起点距眼位 ≥ 0.5m", Vector3.Distance(muzzle, eye).ToString("R"));
            tracer.Tick(119f);
            SelfTest.True(tracer.Segments[0].Alive, "119ms 还活着", "没了");
            tracer.Tick(1f);
            SelfTest.True(!tracer.Segments[0].Alive, "120ms 到期", "还活着");

            // 未命中：终点 = 起点 + 视轴方向 × 30m
            var fallback = Tracer.FallbackEnd(muzzle, new Vector3(0f, 0f, 1f));
            SelfTest.True(Vector3.Distance(fallback, muzzle) == 30f, "未命中 30m", Vector3.Distance(fallback, muzzle).ToString("R"));
            SelfTest.True(Tracer.FallbackEnd(muzzle, Vector3.zero) == muzzle + Vector3.forward * 30f, "零方向回退到前方", "不对");

            // 池满覆盖最旧
            var full = new Tracer();
            for (var i = 0; i < 32; i++) full.Spawn(muzzle, hit + Vector3.forward * i);
            SelfTest.Equal(0, full.OverflowCount);
            full.Spawn(muzzle, hit + Vector3.forward * 99f);
            SelfTest.Equal(1, full.OverflowCount);
            SelfTest.True(full.Segments[0].AgeMs == 0f, "覆盖后计龄从 0 起", full.Segments[0].AgeMs.ToString("R"));
            SelfTest.True(full.Segments[0].To == hit + Vector3.forward * 99f, "覆盖的是最旧一条", full.Segments[0].To.ToString("R"));
            full.Reset();
            SelfTest.Equal(0, full.LiveCount);
            SelfTest.True(!full.Segments[0].Alive, "Reset 清空", "还活着");

            // 连射 1 秒 ≤ 1s/间隔 + 1（手枪 = 6）
            var burst = new Tracer();
            var interval = WeaponAnim.IntervalMs(WeaponAnim.PistolRpm, 1f);
            var shots = 0;
            for (var t = 0f; t <= 1000f; t += interval) { burst.Spawn(muzzle, hit); shots += 1; }
            SelfTest.True(shots <= 6, "1 秒手枪曳光 ≤ 6", shots.ToString());
        }

        private static void ChecksParticles()
        {
            SelfTest.Equal(256, (long)Particles.Capacity);
            SelfTest.True(Particles.PointSizePx == 64f, "点尺寸 64px", Particles.PointSizePx.ToString("R"));
            SelfTest.True(Particles.ReducedMotionJitterScale == 0.35f, "减少动态 ×0.35", Particles.ReducedMotionJitterScale.ToString("R"));
            SelfTest.Equal(8000, (long)Particles.DecalLifetimeMs);
            SelfTest.Equal(6, (long)Particles.BloodCount);
            SelfTest.Equal(4, (long)Particles.WoolCount);

            var particles = new Particles();
            var position = new Vector3(1f, 2f, 3f);
            SelfTest.Equal(6, (long)particles.Emit(ParticleKind.Blood, Particles.BloodCount, position));
            SelfTest.Equal(6, particles.LiveCount);
            SelfTest.True(particles.Buffer[0].SizePx == Particles.PointSizePx, "点尺寸落到粒子", particles.Buffer[0].SizePx.ToString("R"));
            SelfTest.True(particles.Buffer[0].LifetimeMs == Particles.BloodLifetimeMs, "血屑寿命", particles.Buffer[0].LifetimeMs.ToString("R"));
            particles.Emit(ParticleKind.Wool, Particles.WoolCount, position);
            SelfTest.Equal(10, particles.LiveCount);
            var fullSpeed = particles.Buffer[6].Velocity.magnitude;
            particles.ReducedMotion = true;
            particles.Emit(ParticleKind.Blood, 1, position);
            var reduced = particles.Buffer[10].Velocity.magnitude;
            SelfTest.True(Math.Abs(reduced - fullSpeed * 0.35f) < 1e-4, "减少动态抖动 ×0.35", reduced.ToString("R"));
            particles.Emit(ParticleKind.Decal, 1, position);
            SelfTest.True(particles.Buffer[11].LifetimeMs == 8000f, "弹孔 8s", particles.Buffer[11].LifetimeMs.ToString("R"));
            particles.Tick(7999f);
            SelfTest.True(particles.Buffer[11].Alive, "弹孔 8s 内还在", "没了");
            particles.Tick(2f);
            SelfTest.True(!particles.Buffer[11].Alive, "弹孔过期", "还在");
            var overflowing = new Particles();
            for (var i = 0; i < Particles.Capacity + 3; i++) overflowing.Emit(ParticleKind.Blood, 1, position);
            SelfTest.Equal(3, overflowing.OverflowCount);
            overflowing.Reset();
            SelfTest.Equal(0, overflowing.LiveCount);
        }

        private static void ChecksMarker()
        {
            SelfTest.Equal(0xFFFFFF, (long)HitMarker.HitColor);
            SelfTest.Equal(0xFF4D4D, (long)HitMarker.KillColor);
            SelfTest.Equal(0xFFD24D, (long)HitMarker.HeadshotColor);
            SelfTest.True(HitMarker.LifetimeMs == 140f, "三态都是 140ms", HitMarker.LifetimeMs.ToString("R"));
            SelfTest.True(HitMarker.HeadshotScale == 1.35f, "爆头缩放 1.35", HitMarker.HeadshotScale.ToString("R"));
            SelfTest.True(HitMarker.HeadshotDamageMultiplier == 2.0f, "爆头伤害 ×2.0", HitMarker.HeadshotDamageMultiplier.ToString("R"));
            SelfTest.Equal(0xFFFFFF, (long)HitMarker.ColorOf(MarkerState.Hit));
            SelfTest.Equal(0xFF4D4D, (long)HitMarker.ColorOf(MarkerState.Kill));
            SelfTest.Equal(0xFFD24D, (long)HitMarker.ColorOf(MarkerState.Headshot));

            var marker = new HitMarker();
            // 权威位值：headshot=1 / downed=2 / killed=4（server/src/config/combat.hpp）
            SelfTest.Equal(1, (long)HitMarker.FlagHeadshot);
            SelfTest.Equal(2, (long)HitMarker.FlagDowned);
            SelfTest.Equal(4, (long)HitMarker.FlagKilled);
            SelfTest.Equal((long)MarkerState.Hit, (long)marker.ShowFromFlags(0));
            SelfTest.Equal((long)MarkerState.Hit, (long)marker.ShowFromFlags(2));            // 倒地不是命中反馈态
            SelfTest.Equal((long)MarkerState.Headshot, (long)marker.ShowFromFlags(1));
            SelfTest.Equal((long)MarkerState.Kill, (long)marker.ShowFromFlags(4));
            SelfTest.Equal((long)MarkerState.Headshot, (long)marker.ShowFromFlags(1 | 4));   // 爆头优先
            SelfTest.True(marker.ScaleOf(marker.State) == HitMarker.HeadshotScale, "爆头缩放生效", marker.ScaleOf(marker.State).ToString("R"));
            SelfTest.True(marker.ScaleOf(MarkerState.Hit) == 1f, "命中态不缩放", marker.ScaleOf(MarkerState.Hit).ToString("R"));
            marker.Tick(139f);
            SelfTest.True(marker.RemainingMs > 0f, "139ms 还在", marker.RemainingMs.ToString("R"));
            marker.Tick(1f);
            SelfTest.True(marker.RemainingMs == 0f, "140ms 结束", marker.RemainingMs.ToString("R"));
        }

        private static void ChecksScreen()
        {
            SelfTest.True(Effects.HitFlashMs == 150f, "命中红闪 150ms", Effects.HitFlashMs.ToString("R"));
            SelfTest.True(Effects.VignetteMin == 0.12f && Effects.VignetteMax == 0.35f, "暗角 0.12→0.35", Effects.VignetteMin.ToString("R"));
            SelfTest.True(Effects.BloodScreenMax == 0.55f, "血屏上限 0.55", Effects.BloodScreenMax.ToString("R"));
            SelfTest.True(Effects.ShakeMaxM == 0.22f && Effects.ShakeDecayPerSecond == 7.5f, "抖动上限 0.22m / 7.5/s", Effects.ShakeMaxM.ToString("R"));
            SelfTest.Equal(24, (long)Effects.EliteBoltCapacity);
            SelfTest.Equal(12, (long)Effects.ChargeRingCapacity);
            SelfTest.True(Effects.ChargeRingLifetimeMs == 1150f && Effects.ChargeRingMissStepMs == 16f, "警戒环 1150ms / 阶梯 16ms", Effects.ChargeRingLifetimeMs.ToString("R"));
            SelfTest.True(Effects.EliteBoltSpinHz == 1.6f && Effects.EliteBoltGlowScale == 1.6f, "问号弹 1.6Hz / 缩放 1.6", Effects.EliteBoltSpinHz.ToString("R"));

            var effects = new Effects();
            effects.SetAmmoGate(3);
            effects.SetHitAnchor(new Vector3(0f, 1.4f, 8f));
            effects.SpawnTracer(new Vector3(0f, 1.6f, 0f), new Vector3(0f, 1.4f, 25f));
            SelfTest.True(effects.MuzzleFlashRemainingMs == 45, "枪口火焰 45ms", effects.MuzzleFlashRemainingMs.ToString());
            SelfTest.Equal((long)MarkerState.Kill, (long)effects.SpawnHit(HitMarker.FlagKilled));
            SelfTest.True(effects.HitFlashRemainingMs == 150f && effects.Vignette == Effects.VignetteMax, "红闪与暗角拉满", effects.Vignette.ToString("R"));
            SelfTest.True(effects.ShakeM == 0.22f, "击杀触发抖动上限", effects.ShakeM.ToString("R"));
            SelfTest.Equal(4, (long)effects.ParticlePool.LiveCount);
            SelfTest.True(effects.ParticlePool.Buffer[0].Kind == ParticleKind.Wool, "击杀出毛屑", effects.ParticlePool.Buffer[0].Kind.ToString());
            effects.Tick(10f);   // 7.5/s × 10ms = 0.075m
            SelfTest.True(effects.ShakeM < 0.22f && effects.ShakeM > 0.1f, "抖动按 7.5/s 衰减", effects.ShakeM.ToString("R"));
            effects.Tick(200f);
            SelfTest.True(effects.HitFlashRemainingMs == 0f && effects.Vignette == Effects.VignetteMin, "红闪结束后回暗角下限", effects.Vignette.ToString("R"));
            SelfTest.True(effects.ParticlePool.ReducedMotion == false, "动态抖动默认全量", "异常");

            var ring = effects.SpawnChargeRing(Vector3.zero);
            SelfTest.Equal(0, ring);
            SelfTest.True(effects.Rings[0].Alive, "警戒环入池", "未入池");
            effects.Tick(1149f);
            SelfTest.True(effects.Rings[0].Alive, "1150ms 内还在", "没了");
            effects.Tick(2f);
            SelfTest.True(!effects.Rings[0].Alive, "1150ms 到期", "还在");
            var bolt = effects.SpawnEliteBolt(Vector3.zero, Vector3.forward);
            SelfTest.Equal(0, bolt);
            effects.Tick(100f);
            SelfTest.True(Math.Abs(effects.Bolts[0].Position.z - 0.1f) < 1e-4, "问号弹按方向推进", effects.Bolts[0].Position.z.ToString("R"));
            var bolt2 = effects.SpawnEliteBolt(Vector3.zero, Vector3.forward);
            effects.Tick(Effects.EliteBoltLifetimeMs + 1f);
            SelfTest.True(!effects.Bolts[bolt2].Alive, "问号弹到期退场（池不能只写不清）", "还活着");
            for (var i = 0; i < Effects.EliteBoltCapacity + 2; i++) effects.SpawnEliteBolt(Vector3.zero, Vector3.forward);
            SelfTest.Equal(2, effects.BoltOverflowCount);   // 到期清空后 26 次复用（24 槽）
            effects.SetBloodScreen(9f);
            SelfTest.True(effects.BloodScreen == Effects.BloodScreenMax, "血屏夹到上限 0.55", effects.BloodScreen.ToString("R"));
            effects.Tick(5000f);
            SelfTest.True(effects.BloodScreen == 0f, "血屏按下限清零", effects.BloodScreen.ToString("R"));
            effects.SetReducedMotion(true);
            SelfTest.True(effects.ParticlePool.ReducedMotion, "减少动态开关直达粒子池", "没生效");
            for (var i = 0; i < Effects.ChargeRingCapacity + 5; i++) effects.SpawnChargeRing(Vector3.zero);
            SelfTest.Equal(5, effects.RingOverflowCount);
        }

        private static void ChecksAmmoGate()
        {
            var effects = new Effects();
            var muzzle = new Vector3(0.17f, 1.41f, -0.02f);
            var to = new Vector3(0f, 1.4f, 25f);
            effects.SetAmmoGate(0);
            SelfTest.True(!effects.SpawnTracer(muzzle, to), "空弹匣不放行", "放行了");
            SelfTest.Equal(0, effects.Tracers.LiveCount);
            SelfTest.Equal(1, effects.RejectedShots);
            SelfTest.True(effects.MuzzleFlashRemainingMs == 0, "空弹匣没有枪口火焰", effects.MuzzleFlashRemainingMs.ToString());
            effects.SetAmmoGate(-5);
            SelfTest.True(!effects.SpawnTracer(muzzle, to), "负闸门也当 0", "放行了");
            SelfTest.Equal(2, effects.RejectedShots);
            effects.SetAmmoGate(2);
            effects.SetFireIntervalMs(200f);
            SelfTest.True(effects.SpawnTracer(muzzle, to), "有弹放行", "被拒");
            SelfTest.Equal(1, effects.Tracers.LiveCount);
            SelfTest.Equal(2, effects.RejectedShots);
            SelfTest.True(!effects.SpawnTracer(muzzle, to), "间隔内连点被节流", "放行了");
            SelfTest.Equal(1, effects.ThrottledShots);
            effects.Tick(300f);
            SelfTest.True(effects.SpawnTracer(muzzle, to), "过一个间隔后放行", "被拒");
            // §5(d)：1 秒手枪最多 6 发
            var fired = new Effects();
            fired.SetAmmoGate(99);
            fired.SetFireIntervalMs(WeaponAnim.IntervalMs(WeaponAnim.PistolRpm, WeaponAnim.FireRateMultiplier));
            var accepted = 0;
            for (var t = 0f; t < 1000f; t += 100f) { if (fired.SpawnTracer(muzzle, to)) accepted += 1; fired.Tick(100f); }
            SelfTest.True(accepted <= 6, "1 秒最多 6 发（节流真的在拦）", accepted.ToString());
            SelfTest.Equal(10, fired.ThrottledShots + accepted);   // 1 秒内 10 次尝试

            // 从视图模型开火：终点取权威命中点或 30m 回退
            var go = new GameObject("EyeCamera2");
            var camera = go.AddComponent<Camera>();
            camera.transform.position = Vector3.zero;
            camera.transform.rotation = Quaternion.identity;
            var model = new ViewModel();
            model.Attach(camera);
            var hit = new Vector3(0f, 1.4f, 20f);
            effects.Tick(300f);
            SelfTest.True(effects.SpawnTracerFromMuzzle(model, Vector3.forward, true, hit), "命中时放行", "被拒");
            SelfTest.True(effects.Tracers.Segments[2].From == model.MuzzleWorld, "起点是枪口", effects.Tracers.Segments[2].From.ToString("R"));
            SelfTest.True(effects.Tracers.Segments[2].To == hit, "终点是权威命中点", effects.Tracers.Segments[2].To.ToString("R"));
            effects.Tick(300f);
            SelfTest.True(effects.SpawnTracerFromMuzzle(model, Vector3.forward, false, hit), "未命中时放行", "被拒");
            SelfTest.True(Vector3.Distance(effects.Tracers.Segments[3].To, model.MuzzleWorld) == 30f, "未命中 30m", Vector3.Distance(effects.Tracers.Segments[3].To, model.MuzzleWorld).ToString("R"));
            UnityEngine.Object.DestroyImmediate(go);
        }

        private static void ChecksZeroAlloc()
        {
            var effects = new Effects();
            effects.SetAmmoGate(99);
            effects.SetFireIntervalMs(0f);
            effects.SetHitAnchor(new Vector3(0f, 1.4f, 8f));
            var muzzle = new Vector3(0.17f, 1.41f, -0.02f);
            var to = new Vector3(0f, 1.4f, 25f);
            var segmentsBefore = effects.Tracers.Segments;
            var particlesBefore = effects.ParticlePool.Buffer;
            var before = GC.GetAllocatedBytesForCurrentThread();
            for (var frame = 0; frame < 3; frame++)
            {
                effects.SpawnTracer(muzzle, to);
                effects.SpawnHit(HitMarker.FlagKilled);
                effects.SpawnChargeRing(Vector3.zero);
                effects.SpawnEliteBolt(Vector3.zero, Vector3.forward);
                effects.Tick(16f);
            }
            var after = GC.GetAllocatedBytesForCurrentThread();
            SelfTest.Equal(0, after - before);
            SelfTest.True(ReferenceEquals(segmentsBefore, effects.Tracers.Segments), "曳光数组整局不换", "换了");
            SelfTest.True(ReferenceEquals(particlesBefore, effects.ParticlePool.Buffer), "粒子数组整局不换", "换了");
            SelfTest.Equal(3, effects.Tracers.LiveCount);
            SelfTest.Equal(0, effects.RejectedShots);
            SelfTest.Equal(3, effects.Marker.ShowCount);
            SelfTest.Equal(0, effects.ThrottledShots);
        }
    }
}
