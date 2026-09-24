using System;
using Ac.Core;
using Ac.Net;
using Ac.Sim;
using Ac.View;

namespace Ac.Tests
{
    // C04 §6 的四组用例：tick 单调、100ms 插值缓冲、±50ms 抖动吸收、硬纠正阈值。
    // 全部用合成快照帧，不依赖网络。
    public static class InterpolationSuite
    {
        public static void Register()
        {
            SelfTest.Add("view.tick_monotonic", ChecksTickMonotonic);
            SelfTest.Add("view.interp_100ms", ChecksInterp100Ms);
            SelfTest.Add("view.jitter_50ms", ChecksJitter50Ms);
            SelfTest.Add("view.hard_correct_1m", ChecksHardCorrect);
            SelfTest.Add("view.mirror_queries", ChecksMirrorQueries);
            SelfTest.Add("view.spawn_pose", ChecksSpawnPose);
        }

        private static SnapshotFrame MakeFrame(uint tick, uint serverTimeMs, uint baselineTick)
        {
            var frame = new SnapshotFrame();
            frame.Tick = tick;
            frame.ServerTimeMs = serverTimeMs;
            frame.BaselineTick = baselineTick;
            frame.Entities = new FrameEntity[SnapshotView.MaxRecordsPerFrame];
            frame.RemovedIds = new ushort[SnapshotView.MaxRemovedPerFrame];
            return frame;
        }

        private static void PutEntity(ref SnapshotFrame frame, ushort id, double xMeters, double zMeters, double yawRad)
        {
            var record = new FrameEntity();
            record.Id = id;
            record.KindFlags = 0;
            record.XCm = Quantize.QuantizePosition(xMeters);
            record.YCm = 0;
            record.ZCm = Quantize.QuantizePosition(zMeters);
            record.YawUnits = Quantize.QuantizeAngle(yawRad);
            record.PitchUnits = 0;
            record.HpRatioUnits = Quantize.QuantizeRatio(1.0);
            frame.Entities[frame.EntityCount] = record;
            frame.EntityCount += 1;
        }

        // §6 第 2 条：喂入 tick 20, 19, 21, 21, 23 → 只应用 20/21/23，两个计数都为 0。
        // 审计 M1：差分帧只带**变化过的**记录 ⇒ 站着不动的实体在"最新帧"里没有条目。
        // TryGetEntity 只扫最新帧，就会让 TryGetLocalAuthority 返回 false，Reconciler.cs:61 每次和解都早退
        // （ack 裁剪 / 命令重放 / 误差平滑全部停摆）。权威状态必须以镜像为准。
        // 审计 M7：插值窗口比最新帧落后一个 DelayMs（100ms），所以"在最新帧里刚出现"的实体
        // 不在插值用的 newer 差分记录里；而可见性是按**镜像**判定的 ⇒ 它会以默认 (0,0,0)
        // 在场地中心渲染若干帧。修复后这类实体必须在 MarkVisible 里就从镜像吸附姿态。
        private static void ChecksSpawnPose()
        {
            var view = new SnapshotView();
            var views = new EntityViews();
            var clock = new Interpolation.RenderClock();

            var f1 = MakeFrame(1, 1000, 0);
            PutEntity(ref f1, 7, 1.0, 0.0, 0.0);
            SelfTest.True(view.ApplyFrame(f1), "帧 1 应用", "被丢弃");
            clock.OnSnapshot(1000, 0.0);
            views.SyncFrame(view, clock, 8.0);

            for (uint tick = 2; tick <= 3; tick++)
            {
                var step = MakeFrame(tick, 1000 + (tick - 1) * 50, tick - 1);
                PutEntity(ref step, 7, 1.0 + 0.1 * (tick - 1), 0.0, 0.0);
                SelfTest.True(view.ApplyFrame(step), "帧 " + tick + " 应用", "被丢弃");
                clock.OnSnapshot(1000 + (tick - 1) * 50, 50.0);
                views.SyncFrame(view, clock, 8.0);
            }

            // 帧 4：实体 9 第一次出现。渲染时刻 = 1150 - DelayMs(100) = 1050，
            // 插值窗口是 (1050,1100)，里面没有 9；但镜像里已经有它了。
            var f4 = MakeFrame(4, 1150, 3);
            PutEntity(ref f4, 7, 1.3, 0.0, 0.0);
            PutEntity(ref f4, 9, 5.0, -2.0, 0.7);
            SelfTest.True(view.ApplyFrame(f4), "帧 4 应用", "被丢弃");
            clock.OnSnapshot(1150, 50.0);
            views.SyncFrame(view, clock, 8.0);

            EntityView spawned;
            SelfTest.True(views.TryGet(9, out spawned), "新生实体必须被创建", "缺失");
            SelfTest.True(spawned.Visible, "新生实体可见", "不可见");
            // 旧实现这里是 0：姿态要等渲染时刻追赶到帧 4 才被写上。
            SelfTest.True(Math.Abs(spawned.X - 5.0) < 0.05, "新生实体必须带镜像姿态出场", spawned.X.ToString("R"));
            SelfTest.True(Math.Abs(spawned.Z + 2.0) < 0.05, "新生实体 Z 同样来自镜像", spawned.Z.ToString("R"));
        }

        private static void ChecksMirrorQueries()
        {
            var view = new SnapshotView();
            view.SetLocalPlayer(1);
            var full = MakeFrame(10, 1000, 0);
            PutEntity(ref full, 1, 12.5, 0.0, -3.0);
            PutEntity(ref full, 7, 2.0, 0.0, 0.0);
            SelfTest.True(view.ApplyFrame(full), "全量帧必须应用", "被丢弃");

            var hit = default(FrameEntity);
            SelfTest.True(view.TryGetEntity(1, out hit), "全量帧之后本机可取", "取不到");
            SelfTest.Equal(1250, (long)hit.XCm);

            // 下一帧只带 7（本机没动）：镜像里本机仍在，最新帧的差分记录里没有它。
            var delta = MakeFrame(11, 1050, 10);
            PutEntity(ref delta, 7, 2.2, 0.0, 0.0);
            SelfTest.True(view.ApplyFrame(delta), "差分帧必须应用", "被丢弃");
            SelfTest.True(view.TryGetEntity(1, out hit), "本机没变化时仍必须从镜像取到", "只查最新帧就会失败");
            SelfTest.Equal(1250, (long)hit.XCm);
            FrameEntity authority;
            SelfTest.True(view.TryGetLocalAuthority(out authority), "本地权威姿态必须可用（否则 Reconciler 早退）", "取不到");
            SelfTest.Equal(1250, (long)authority.XCm);

            // 逐帧历史语义不受影响：最新帧里确实没有本机这条记录。
            FrameEntity inFrame;
            SelfTest.True(!view.TryGetEntityInFrame(0, 1, out inFrame), "最新帧的差分记录里没有本机", "居然有");

            var seenLocal = false;
            view.ForEachVisible((ushort id, in FrameEntity e) => { if (id == 1) seenLocal = true; });
            SelfTest.True(seenLocal, "遍历与查询必须同源（都读镜像）", "不一致");
        }

        private static void ChecksTickMonotonic()
        {
            var view = new SnapshotView();
            uint[] ticks = { 20, 19, 21, 21, 23 };
            var applied = 0;
            for (var i = 0; i < ticks.Length; i++)
            {
                var frame = MakeFrame(ticks[i], 1000 + ticks[i] * 50, 0);
                PutEntity(ref frame, 7, 1.0, 0.0, 0.0);
                if (view.ApplyFrame(frame)) applied += 1;
            }

            SelfTest.Equal(3, applied);
            SelfTest.Equal(23, view.AppliedTick);
            SelfTest.Equal(3, view.AppliedFrames);
            SelfTest.Equal(0, view.BaselineMismatch);
            SelfTest.Equal(0, view.InvalidSnapshots);

            var badBaseline = MakeFrame(24, 2200, 30);
            SelfTest.True(!view.ApplyFrame(badBaseline), "baselineTick > tick 必须丢弃", "被应用");
            var badBaseline2 = MakeFrame(24, 2200, 25);
            SelfTest.True(!view.ApplyFrame(badBaseline2), "baselineTick > appliedTick 必须丢弃", "被应用");
            SelfTest.Equal(2, view.BaselineMismatch);
            SelfTest.Equal(23, view.AppliedTick);

            var oversize = MakeFrame(24, 2200, 0);
            oversize.EntityCount = SnapshotView.MaxRecordsPerFrame + 1;
            SelfTest.True(!view.ApplyFrame(oversize), "count 越界必须丢弃", "被应用");
            view.NoteDecodeFailure();
            SelfTest.Equal(2, view.InvalidSnapshots);

            var full = MakeFrame(30, 2500, 0);
            SelfTest.True(view.ApplyFrame(full), "全量帧必须应用", "被丢弃");
            SelfTest.Equal(0, view.VisibleCount);

            var two = MakeFrame(31, 2550, 30);
            PutEntity(ref two, 7, 2.0, 0.0, 0.0);
            PutEntity(ref two, 9, 3.0, 0.0, 0.0);
            SelfTest.True(view.ApplyFrame(two), "差分帧必须应用", "被丢弃");
            SelfTest.Equal(2, view.VisibleCount);

            var removed = MakeFrame(32, 2600, 31);
            PutEntity(ref removed, 7, 2.5, 0.0, 0.0);
            removed.RemovedIds[0] = 9;
            removed.RemovedCount = 1;
            SelfTest.True(view.ApplyFrame(removed), "含移除列表的帧必须应用", "被丢弃");
            SelfTest.Equal(1, view.VisibleCount);
            var visible = 0;
            view.ForEachVisible((ushort id, in FrameEntity entity) => { visible += 1; });
            SelfTest.Equal(1, visible);

            for (uint tick = 40; tick <= 45; tick++)
            {
                var frame = MakeFrame(tick, 3000 + (tick - 40) * 50, 0);
                PutEntity(ref frame, 7, 1.0, 0.0, 0.0);
                view.ApplyFrame(frame);
            }
            SelfTest.Equal(SnapshotView.HistoryFrames, view.FrameCount);
            var times = new uint[SnapshotView.HistoryFrames];
            SelfTest.Equal(SnapshotView.HistoryFrames, view.CopyServerTimes(times));
            var ascending = true;
            for (var i = 1; i < times.Length; i++)
            {
                if (times[i] <= times[i - 1]) ascending = false;
            }
            SelfTest.True(ascending, "时间表升序（索引 0 最旧）", "乱序");
            SnapshotFrame oldest;
            SelfTest.True(view.TryGetFrame(SnapshotView.HistoryFrames - 1, out oldest) && oldest.ServerTimeMs == times[0],
                "age 与时间表同源", "不一致");
            SnapshotFrame beyond;
            SelfTest.True(!view.TryGetFrame(SnapshotView.HistoryFrames, out beyond), "环外帧不可访问", "可访问");

            view.SetLocalPlayer(7);
            FrameEntity authority;
            SelfTest.True(view.TryGetLocalAuthority(out authority), "本地权威姿态可取", "取不到");
            SelfTest.Equal(7, authority.Id);

            var payload = new SnapshotPayload();
            payload.Tick = 60;
            payload.ServerTimeMs = 6000;
            payload.BaselineTick = 0;
            payload.Records = new EntityRecord[1];
            payload.Records[0].Id = 11;
            payload.Records[0].XCm = Quantize.QuantizePosition(4.0);
            payload.Records[0].YawUnits = Quantize.QuantizeAngle(0.5);
            payload.RemovedIds = new ushort[0];
            var mapped = MakeFrame(0, 0, 0);
            SelfTest.True(SnapshotCodec.TryToFrame(payload, ref mapped), "解码结果可映射成帧", "映射失败");
            SelfTest.True(view.ApplyFrame(mapped), "映射出的帧可应用", "被丢弃");
            SelfTest.Equal(60, view.AppliedTick);
            SelfTest.Equal(6000, view.GetServerTimeMs());
            var idFound = 0;
            view.ForEachVisible((ushort id, in FrameEntity entity) => { if (id == 11) idFound += 1; });
            SelfTest.Equal(1, idFound);
        }

        // §6 第 3 条：两帧相距 50ms，渲染时间取中点 → 位置为算术中点（误差 ≤ 1e-9）。
        private static void ChecksInterp100Ms()
        {
            SelfTest.True(Math.Abs(Interpolation.InterpolationAlpha(1025, 1000, 1050) - 0.5) < 1e-12, "alpha=0.5", "偏离");
            SelfTest.True(Math.Abs(Interpolation.Lerp(1.0, 3.0, 0.5) - 2.0) < 1e-12, "中点=2.0", "偏离");
            SelfTest.True(Math.Abs(Interpolation.LerpAngle(0.0, 1.0, 1.0) - 1.0) < 1e-12, "端点=to", "偏离");

            uint[] times = { 1000, 1050, 1100 };
            int older;
            int newer;
            Interpolation.FindBracket(times, 3, 1025, out older, out newer);
            SelfTest.Equal(0, older);
            SelfTest.Equal(1, newer);
            Interpolation.FindBracket(times, 3, 900, out older, out newer);
            SelfTest.Equal(0, older);
            SelfTest.Equal(0, newer);
            Interpolation.FindBracket(times, 3, 1200, out older, out newer);
            SelfTest.Equal(2, older);
            SelfTest.Equal(2, newer);
            Interpolation.FindBracket(times, 1, 1025, out older, out newer);
            SelfTest.Equal(0, older);
            SelfTest.Equal(0, newer);

            var from = 350.0 * Math.PI / 180.0;
            var to = 10.0 * Math.PI / 180.0;
            var delta = Interpolation.ShortestAngleDelta(from, to);
            SelfTest.True(Math.Abs(delta - 20.0 * Math.PI / 180.0) < 1e-9, "最短弧 +20°", "偏离");
            SelfTest.True(Math.Abs(Interpolation.ShortestAngleDelta(to, from) + 20.0 * Math.PI / 180.0) < 1e-9, "反向 -20°", "偏离");
            SelfTest.True(Math.Abs(Interpolation.LerpAngle(from, to, 0.5)) < 1e-9, "跨 0° 的中点落在 0°", "偏离");

            var view = new SnapshotView();
            var views = new EntityViews();
            var clock = new Interpolation.RenderClock();
            var first = MakeFrame(1, 1000, 0);
            PutEntity(ref first, 7, 1.0, 0.0, 0.0);
            SelfTest.True(view.ApplyFrame(first), "帧 1 应用", "被丢弃");
            clock.OnSnapshot(1000, 0.0);
            var second = MakeFrame(2, 1050, 0);
            PutEntity(ref second, 7, 3.0, 0.0, 0.0);
            SelfTest.True(view.ApplyFrame(second), "帧 2 应用", "被丢弃");
            clock.OnSnapshot(1050, 50.0);
            clock.Advance(75.0);

            SelfTest.Equal(Interpolation.DelayMs, clock.RenderDelayMs);
            SelfTest.True(Math.Abs(clock.RenderTimeMs - 1025.0) < 1e-9, "渲染时间=两帧中点", clock.RenderTimeMs.ToString("R"));
            SelfTest.Equal(1, views.SyncFrame(view, clock, 8.0));
            EntityView entity;
            SelfTest.True(views.TryGet(7, out entity), "视图对象已创建", "缺失");
            SelfTest.True(Math.Abs(entity.X - 2.0) < 1e-9, "位置为中点 2.0m", entity.X.ToString("R"));
            SelfTest.True(Math.Abs(entity.RenderX - 2.0) < 1e-9, "渲染位置（无偏移）", entity.RenderX.ToString("R"));
            SelfTest.True(Math.Abs(entity.HpRatio - 1.0) < 1e-9, "血量比例解回 1.0", entity.HpRatio.ToString("R"));

            view.SetLocalPlayer(7);
            views.SetLocalPlayer(7);
            entity.HasPrediction = true;
            entity.PredictedX = 9.0;
            entity.PredictedY = 0.0;
            entity.PredictedZ = 0.0;
            views.SyncFrame(view, clock, 8.0);
            SelfTest.True(Math.Abs(entity.X - 9.0) < 1e-9, "本地玩家用预测值渲染", entity.X.ToString("R"));
            SelfTest.True(Math.Abs(views.LastAuthorityX - 3.0) < 1e-9, "权威基线来自只读镜像", views.LastAuthorityX.ToString("R"));
        }

        // §5.1/§5.3：动态延迟 clamp(2*中位数, 100, 250)、单帧修正量 ±50ms、超 500ms 直接重置。
        private static void ChecksJitter50Ms()
        {
            SelfTest.Equal(Interpolation.DelayMs, Interpolation.DynamicDelayMs(20));
            SelfTest.Equal(100, Interpolation.DynamicDelayMs(50));
            SelfTest.Equal(160, Interpolation.DynamicDelayMs(80));
            SelfTest.Equal(Interpolation.DelayMaxMs, Interpolation.DynamicDelayMs(200));
            SelfTest.Equal(Interpolation.DelayMs, Interpolation.DynamicDelayMs(0));

            var view = new SnapshotView();
            var views = new EntityViews();
            var clock = new Interpolation.RenderClock();
            double[] arrivals = { 0.0, 60.0, 95.0, 165.0, 210.0, 285.0, 330.0, 400.0 };
            var lastRenderX = double.NaN;
            var maxStep = 0.0;
            for (var i = 0; i < arrivals.Length; i++)
            {
                var serverTimeMs = (uint)(1000 + i * 50);
                var frame = MakeFrame((uint)(1 + i), serverTimeMs, 0);
                PutEntity(ref frame, 7, 1.0 + 0.1 * i, 0.0, 0.0);
                SelfTest.True(view.ApplyFrame(frame), "丢包回环帧必须应用", "被丢弃");
                if (i > 0) clock.Advance(arrivals[i] - arrivals[i - 1]);
                var before = clock.EstimatedServerTimeMs;
                clock.OnSnapshot(serverTimeMs, arrivals[i]);
                if (i > 0)
                {
                    var correction = clock.EstimatedServerTimeMs - before;
                    SelfTest.True(correction <= Interpolation.JitterAbsorbMs + 1e-9, "单帧修正量不超过 50ms", correction.ToString("R"));
                    SelfTest.True(clock.EstimatedServerTimeMs <= serverTimeMs + 1e-9, "估计值不越过目标值", clock.EstimatedServerTimeMs.ToString("R"));
                }
                views.SyncFrame(view, clock, 16.0);
                EntityView entity;
                if (!views.TryGet(7, out entity)) continue;
                if (!double.IsNaN(lastRenderX))
                {
                    var step = Math.Abs(entity.RenderX - lastRenderX);
                    if (step > maxStep) maxStep = step;
                    SelfTest.True(step <= 0.5, "连续帧位置差不超过 0.5m", step.ToString("R"));
                }
                lastRenderX = entity.RenderX;
            }
            SelfTest.True(maxStep > 0.0, "位置确实在推进", "没动");

            var reset = new Interpolation.RenderClock();
            reset.OnSnapshot(1000, 0.0);
            reset.Advance(10.0);
            reset.OnSnapshot(2000, 10.0);
            SelfTest.True(Math.Abs(reset.EstimatedServerTimeMs - 2000.0) < 1e-9, "偏差 >500ms 直接重置", reset.EstimatedServerTimeMs.ToString("R"));
        }

        // §5.5：忽略阈值 0.001m、吸附阈值 1.0m、0.12s 指数衰减归零，且偏移从不写回镜像。
        private static void ChecksHardCorrect()
        {
            var smoother = new ErrorSmoother();
            SelfTest.Equal((long)SmoothingAction.None, (long)smoother.Apply(0.0005, 0.0, 0.0));
            SelfTest.True(smoother.Magnitude == 0.0, "忽略阈值内归零", smoother.Magnitude.ToString("R"));

            SelfTest.Equal((long)SmoothingAction.Smooth, (long)smoother.Apply(0.5, 0.0, 0.0));
            SelfTest.True(Math.Abs(smoother.Magnitude - 0.5) < 1e-9, "平滑初始偏移=误差", smoother.Magnitude.ToString("R"));
            smoother.Decay(120.0);
            var expected = 0.5 * Math.Exp(-1.0);
            SelfTest.True(Math.Abs(smoother.Magnitude - expected) < 1e-9, "一个时间常数后衰减到 1/e", smoother.Magnitude.ToString("R"));
            SelfTest.Equal((long)SmoothingAction.Smooth, (long)smoother.Apply(1.0, 0.0, 0.0));
            smoother.Decay(2000.0);
            SelfTest.True(smoother.Magnitude == 0.0, "衰减到阈值以下归零", smoother.Magnitude.ToString("R"));
            SelfTest.True(smoother.ResetCount > 0, "归零计数在涨", smoother.ResetCount.ToString());

            var snap = new ErrorSmoother();
            SelfTest.Equal((long)SmoothingAction.Snap, (long)snap.Apply(1.0001, 0.0, 0.0));
            SelfTest.True(snap.Magnitude == 0.0, "吸附后无偏移（过渡 0ms）", snap.Magnitude.ToString("R"));
            SelfTest.Equal(1, snap.SnapCount);

            var view = new SnapshotView();
            var views = new EntityViews();
            var clock = new Interpolation.RenderClock();
            var frame = MakeFrame(1, 1000, 0);
            PutEntity(ref frame, 7, 5.0, 0.0, 0.0);
            SelfTest.True(view.ApplyFrame(frame), "帧应用", "被丢弃");
            clock.OnSnapshot(1000, 0.0);
            views.SyncFrame(view, clock, 16.0);
            EntityView entity;
            SelfTest.True(views.TryGet(7, out entity), "视图存在", "缺失");
            SelfTest.True(Math.Abs(entity.X - 5.0) < 1e-9, "姿态来自镜像", entity.X.ToString("R"));

            entity.X = 5.4;   // 模拟被硬纠正后的权威姿态
            SelfTest.Equal((long)SmoothingAction.Smooth, (long)views.ApplyCorrection(entity, 5.8, 0.0, 0.0));
            SelfTest.True(Math.Abs(entity.RenderX - 5.8) < 1e-9, "纠正瞬间渲染位置连续", entity.RenderX.ToString("R"));
            entity.Smoother.Decay(120.0);
            SelfTest.True(entity.RenderX > 5.4 && entity.RenderX < 5.8, "偏移向权威衰减", entity.RenderX.ToString("R"));

            views.SyncFrame(view, clock, 16.0);
            SnapshotFrame applied;
            SelfTest.True(view.TryGetFrame(0, out applied), "镜像帧可取", "取不到");
            SelfTest.Equal(1, view.AppliedTick);
            SelfTest.Equal(Quantize.QuantizePosition(5.0), applied.Entities[0].XCm);
            SelfTest.Equal((long)view.AppliedTick, 1);
        }
    }
}
