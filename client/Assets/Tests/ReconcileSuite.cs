using System;
using Ac.Core;
using Ac.Net;
using Ac.Sim;
using Ac.View;

namespace Ac.Tests
{
    // C06 §5(a)(b)(c)：命令缓冲、回滚重放与误差切换、弹药本地账。
    public static class ReconcileSuite
    {
        public static void Register()
        {
            SelfTest.Add("reconcile.seq_wrap", ChecksSeqWrap);
            SelfTest.Add("reconcile.overflow_drop_oldest", ChecksOverflow);
            SelfTest.Add("reconcile.replay_order", ChecksReplayOrder);
            SelfTest.Add("reconcile.error_boundary", ChecksErrorBoundary);
            SelfTest.Add("reconcile.smoothing_decay", ChecksSmoothingDecay);
            SelfTest.Add("reconcile.ammo_ledger", ChecksAmmoLedger);
            SelfTest.Add("weapon.mirror_values", ChecksWeaponMirrorValues);
            SelfTest.Add("weapon.local_fire_interval", ChecksLocalFireInterval);
            SelfTest.Add("weapon.ammo_gate", ChecksLocalWeaponGate);
        }

        // server/src/config/weapons.hpp 的**逐值镜像**。这张表是跨语言冻结值：任何一项漂了，
        // 客户端的射速/散布/弹匣就会与服务端分叉（本地突突突、服务端慢半拍），而画面看不出原因。
        // 改这里必须同时改 weapons.hpp（ADR-010 §7）。
        private static void ChecksWeaponMirrorValues()
        {
            SelfTest.Equal(3, (long)WeaponTable.SlotCount);
            SelfTest.Equal(12, (long)WeaponTable.MagSize[0]);
            SelfTest.Equal(30, (long)WeaponTable.MagSize[1]);
            SelfTest.Equal(6, (long)WeaponTable.MagSize[2]);
            Near(300.0, WeaponTable.Rpm[0], "手枪 300rpm");
            Near(600.0, WeaponTable.Rpm[1], "步枪 600rpm");
            Near(70.0, WeaponTable.Rpm[2], "霰弹 70rpm");
            SelfTest.Equal(1, (long)WeaponTable.Pellets[0]);
            SelfTest.Equal(1, (long)WeaponTable.Pellets[1]);
            SelfTest.Equal(8, (long)WeaponTable.Pellets[2]);
            Near(1400.0, WeaponTable.ReloadMs[0], "手枪换弹 1400ms", FloatEps);
            Near(2000.0, WeaponTable.ReloadMs[1], "步枪换弹 2000ms", FloatEps);
            Near(2600.0, WeaponTable.ReloadMs[2], "霰弹换弹 2600ms", FloatEps);
            Near(0.8, WeaponTable.BaseSpreadDeg[0], "手枪散布 0.8°", FloatEps);
            Near(0.6, WeaponTable.BaseSpreadDeg[1], "步枪散布 0.6°", FloatEps);
            Near(4.0, WeaponTable.BaseSpreadDeg[2], "霰弹散布 4.0°", FloatEps);
            Near(0.1, WeaponTable.SpreadGrowthPerShotDeg, "每发 +0.1°", FloatEps);
            Near(0.25, WeaponTable.SpreadMaxDeg, "散布上限 0.25°", FloatEps);
            Near(350.0, WeaponTable.SpreadDecayDelayMs, "衰减延迟 350ms", FloatEps);
            Near(6.0, WeaponTable.SpreadDecayPerSecondDeg, "衰减 6°/s", FloatEps);
            SelfTest.Equal(120, (long)WeaponTable.ReserveAmmoInitial);
            Near(0.35, WeaponTable.RecoilPitchPerShotDeg, "后坐俯仰 0.35°", FloatEps);
            Near(0.2, WeaponTable.RecoilYawJitterDeg, "后坐偏航 ±0.2°", FloatEps);
            Near(1.5533, WeaponTable.AimPitchLimitRad, "射击俯仰夹取 1.5533", FloatEps);
            Near(160.0, WeaponTable.ShotMaxDistanceM, "射线上限 160m", FloatEps);

            // rpmToIntervalMs：60000 / (rpm × 倍率)，公式现算（霰弹枪 60000/70 不是近似常数）
            Near(200.0, WeaponTable.FireIntervalMs(0, 1f), "手枪 300rpm → 200ms", 1e-4);
            Near(100.0, WeaponTable.FireIntervalMs(1, 1f), "步枪 600rpm → 100ms", 1e-4);
            Near(60000.0 / 70.0, WeaponTable.FireIntervalMs(2, 1f), "霰弹 70rpm → 60000/70", 1e-3);
            // kRageFireRateMultiplier = 1.25：狂暴里射速快 1/4（本地镜像暂不接这一档，见 A21 残余）
            Near(80.0, WeaponTable.FireIntervalMs(1, 1.25f), "狂暴 1.25× → 80ms", 1e-4);
            SelfTest.True(float.IsPositiveInfinity(WeaponTable.FireIntervalMs(1, 0f)),
                "倍率 0 → +∞（与服务端 rpmToIntervalMs 同语义）", WeaponTable.FireIntervalMs(1, 0f).ToString("R"));
        }

        // SelfTest 只有 long/string 的 Equal：镜像表里的浮点值统一按容差比。
        // 默认容差 1e-9 给 double 精算；float 常量（0.8f 之类）要放宽到 1e-5。
        private const double FloatEps = 1e-5;

        private static void Near(double expected, double actual, string what, double tolerance = 1e-9)
        {
            SelfTest.True(Math.Abs(expected - actual) <= tolerance, what, actual.ToString("R"));
        }

        // 射速由 `nextFireAllowedAtMs` 把关，与"调用频率"无关：这条用例按 1ms 与 60fps 两种帧长
        // 各驱动 2 秒，断言发数完全一致（帧回路按帧驱动不会改变射速，这是把镜像放进帧回路的前提）。
        private static void ChecksLocalFireInterval()
        {
            SelfTest.Equal(10, ShotsInTwoSeconds(0, 1.0));
            SelfTest.Equal(20, ShotsInTwoSeconds(1, 1.0));
            SelfTest.Equal(3, ShotsInTwoSeconds(2, 1.0));
            SelfTest.Equal(10, ShotsInTwoSeconds(0, 1000.0 / 60.0));
            SelfTest.Equal(20, ShotsInTwoSeconds(1, 1000.0 / 60.0));
            SelfTest.Equal(3, ShotsInTwoSeconds(2, 1000.0 / 60.0));
        }

        private static int ShotsInTwoSeconds(byte slot, double frameMs)
        {
            var weapon = new LocalWeapon();
            weapon.SwitchSlot(slot, 0.0);
            var shots = 0;
            var now = 0.0;
            while (now < 2000.0)
            {
                weapon.Update(now, frameMs, 1f);
                if (weapon.TryFire(now, 1f)) shots += 1;
                now += frameMs;
            }
            return shots;
        }

        // §5(c)：弹匣/换弹/槽位/散布四处闸门。少任何一条，客户端都会"打得出服务端不认的枪"。
        private static void ChecksLocalWeaponGate()
        {
            var weapon = new LocalWeapon();
            SelfTest.Equal(0, (long)weapon.Slot);
            SelfTest.Equal(12, (long)weapon.ActiveMag);
            SelfTest.Equal(120, (long)weapon.Reserve);

            // 空弹匣：打满 12 发之后第 13 发必须被拒（本地绝不产生服务端不会产生的开火）
            var shots = 0;
            var now = 0.0;
            for (var i = 0; i < 40; i++)
            {
                if (weapon.TryFire(now, 1f)) shots += 1;
                now += 200.0;
            }
            SelfTest.Equal(12, shots);
            SelfTest.Equal(0, (long)weapon.ActiveMag);

            // 换弹：1400ms 到点从备弹补满（手枪）
            SelfTest.True(weapon.TryStartReload(now), "空弹匣可以起换弹", "被拒");
            SelfTest.True(weapon.IsReloading, "换弹中", "没进换弹");
            SelfTest.True(Math.Abs(weapon.ReloadRemainingMs(now) - 1400.0) < 1e-6, "换弹 1400ms", weapon.ReloadRemainingMs(now).ToString("R"));
            SelfTest.True(!weapon.TryFire(now + 100.0, 1f), "换弹中不能开火", "开火了");
            SelfTest.True(weapon.Update(now + 1400.0, 50.0, 1f), "到点要报换弹完成", "没报");
            SelfTest.Equal(12, (long)weapon.ActiveMag);
            SelfTest.Equal(108, (long)weapon.Reserve);
            SelfTest.True(!weapon.IsReloading, "换弹结束", "还在换弹");

            // 槽位：切到步枪后弹匣容量与射速都跟着换（600rpm → 100ms）
            SelfTest.True(weapon.SwitchSlot(1, now), "切步枪", "没切");
            SelfTest.Equal(30, (long)weapon.ActiveMag);
            SelfTest.True(!weapon.SwitchSlot(1, now), "同槽位不重复切", "切了");
            SelfTest.True(weapon.TryFire(now, 1f), "切枪后能开火", "哑火");
            SelfTest.True(!weapon.TryFire(now + 50.0, 1f), "步枪 100ms 间隔内不能连发", "连发了");
            SelfTest.True(weapon.TryFire(now + 100.0, 1f), "到 100ms 可以再发", "哑火");

            // 散布：每发 +0.1°，封顶 0.25°；距上次开火不足 350ms 不衰减，到点按 6°/s 衰减
            var spread = new LocalWeapon();
            SelfTest.True(spread.TryFire(0.0, 1f), "第一发", "哑火");
            Near(0.1, spread.SpreadDeg, "一发 +0.1°", FloatEps);
            spread.TryFire(200.0, 1f);
            spread.TryFire(400.0, 1f);
            Near(0.25, spread.SpreadDeg, "封顶 0.25°", FloatEps);
            spread.Update(500.0, 50.0, 1f);   // 距最后一发 100ms < 350ms：不衰减
            Near(0.25, spread.SpreadDeg, "350ms 内不衰减", FloatEps);
            spread.Update(750.0, 10.0, 1f);   // 距最后一发 350ms：衰减 6°/s × 10ms = 0.06°
            Near(0.19, spread.SpreadDeg, "到点按 6°/s 衰减", 1e-5);

            // 权威对齐只降不升：MatchState 更高的弹匣值不采用（那会把刚打掉的子弹还回来）
            weapon.SyncAuthority(9, 20, 1);
            SelfTest.Equal(9, (long)weapon.ActiveMag);
            SelfTest.Equal(20, (long)weapon.Reserve);
            weapon.SyncAuthority(28, 99, 1);
            SelfTest.Equal(9, (long)weapon.ActiveMag);
            SelfTest.Equal(20, (long)weapon.Reserve);
        }

        // §5(a)：ushort 回绕比较与 ack 裁剪
        private static void ChecksSeqWrap()
        {
            SelfTest.Equal(0, (long)CommandBuffer.SeqDiff(100, 100));
            SelfTest.Equal(1, (long)CommandBuffer.SeqDiff(0, 65535));
            SelfTest.Equal(-1, (long)CommandBuffer.SeqDiff(65535, 0));
            SelfTest.Equal(2, (long)CommandBuffer.SeqDiff(1, 65535));
            SelfTest.Equal(1, (long)CommandBuffer.SeqDiff(65535, 65534));

            var buffer = new CommandBuffer();
            buffer.Push(Step(65534, 0.0, 1));
            buffer.Push(Step(65535, 0.0, 1));
            buffer.Push(Step(0, 0.0, 1));
            buffer.Push(Step(1, 0.0, 1));
            SelfTest.Equal(4, buffer.Size);
            SelfTest.Equal(2, buffer.AckUpTo(65535));
            SelfTest.Equal(2, buffer.Size);
            SelfTest.Equal(0, (long)buffer.At(0).Seq);
            SelfTest.Equal(2, buffer.AckUpTo(1));
            SelfTest.Equal(0, buffer.Size);
            buffer.Push(Step(7, 0.0, 1));
            buffer.Reset();
            SelfTest.Equal(0, buffer.Size);
            SelfTest.True(buffer.At(0).Seq != 7 || buffer.Size == 0, "Reset 清空", buffer.Size.ToString());
            SelfTest.Equal(0, (long)CommandBuffer.Capacity - 128);
            SelfTest.Equal(3, (long)AmmoLedger.AmmoSlotCount);
        }

        // §5(a)：容量 128、溢出丢最旧
        private static void ChecksOverflow()
        {
            var buffer = new CommandBuffer();
            for (var i = 1; i <= 128; i++) buffer.Push(Step((ushort)i, 0.0, 1));
            SelfTest.Equal(128, buffer.Size);
            SelfTest.Equal(0, buffer.OverflowCount);
            buffer.Push(Step(129, 0.0, 1));
            SelfTest.Equal(128, buffer.Size);
            SelfTest.Equal(1, buffer.OverflowCount);
            SelfTest.Equal(2, (long)buffer.At(0).Seq);
            SelfTest.Equal(129, (long)buffer.At(127).Seq);
        }

        // §5(b)：落地权威姿态 → AckUpTo → 最旧到最新逐条重放
        private static void ChecksReplayOrder()
        {
            var predictor = new Predictor();
            var buffer = new CommandBuffer();
            predictor.SetAuthoritative(0.0, 0.0, 10.0, 0, 0);   // 谷仓盒外，避免碰撞干扰纯积分
            for (var i = 1; i <= 3; i++) buffer.Push(Step((ushort)i, 0.0, 1));
            for (var i = 0; i < 3; i++) predictor.Advance((int)LocalStep.StepDtMs, Step((ushort)(i + 1), 0.0, 1));
            SelfTest.True(Math.Abs(predictor.State.Z - 10.675) < 1e-12, "预测三步 0.675m", predictor.State.Z.ToString("R"));

            var authority = default(LocalAuthority);
            authority.Found = true;
            authority.X = 2.0;
            authority.Z = 10.0;
            authority.YawUnits = 0;
            authority.PitchUnits = 0;
            authority.LastAckedSeq = 1;
            var reconciler = new Reconciler();
            var result = reconciler.Reconcile(authority, buffer, predictor);
            SelfTest.Equal(2, result.Replayed);
            SelfTest.Equal(2, buffer.Size);
            SelfTest.Equal(2, (long)buffer.At(0).Seq);
            SelfTest.True(Math.Abs(predictor.State.X - 2.0) < 1e-12, "权威 x 落地", predictor.State.X.ToString("R"));
            SelfTest.True(Math.Abs(predictor.State.Z - 10.45) < 1e-12, "重放剩余两条 0.45m", predictor.State.Z.ToString("R"));
            SelfTest.True(Math.Abs(result.OffsetX - (0.0 - 2.0)) < 1e-12, "偏移 = 和解前 − 重放后", result.OffsetX.ToString("R"));
            SelfTest.True(result.HardCorrect, "误差 2.0125m 触发硬纠正", result.ErrorM.ToString("R"));
            SelfTest.Equal(1, reconciler.HardCorrectCount);
            SelfTest.True(reconciler.DebugLine(0).Contains("hardCorrect=1"), "调试行含 hardCorrect", reconciler.DebugLine(0));

            // 权威未命中（§8 回滚路径）：不动状态、不计数
            var missing = default(LocalAuthority);
            var before = predictor.State.Z;
            var skipped = reconciler.Reconcile(missing, buffer, predictor);
            SelfTest.Equal(0, skipped.Replayed);
            SelfTest.True(!skipped.HardCorrect, "found=false 不纠正", "纠正了");
            SelfTest.True(Math.Abs(predictor.State.Z - before) < 1e-12, "found=false 不动状态", predictor.State.Z.ToString("R"));
        }

        // §5(b)：err == 1.0 走平滑，err > 1.0 硬纠正
        private static void ChecksErrorBoundary()
        {
            var predictor = new Predictor();
            var buffer = new CommandBuffer();
            var reconciler = new Reconciler();

            var exactly = default(LocalAuthority);
            exactly.Found = true;
            exactly.X = 1.0;
            exactly.LastAckedSeq = 0;
            var smooth = reconciler.Reconcile(exactly, buffer, predictor);
            SelfTest.True(Math.Abs(smooth.ErrorM - 1.0) < 1e-12, "误差恰好 1.0m", smooth.ErrorM.ToString("R"));
            SelfTest.True(!smooth.HardCorrect, "1.0m 不硬纠正", "硬纠正了");
            SelfTest.Equal(0, reconciler.HardCorrectCount);

            var over = default(LocalAuthority);
            over.Found = true;
            over.X = predictor.State.X + 1.0 + 1e-9;   // 相对当前状态再远 1.0m 多一点
            var hard = reconciler.Reconcile(over, buffer, predictor);
            SelfTest.True(hard.ErrorM > 1.0 && hard.HardCorrect, "1.0m 以上硬纠正", hard.ErrorM.ToString("R"));
            SelfTest.Equal(1, reconciler.HardCorrectCount);
            SelfTest.True(reconciler.MaxErrorM >= reconciler.LastErrorM, "MaxErrorM 单调", reconciler.MaxErrorM.ToString("R"));
            reconciler.Reset();
            SelfTest.Equal(0, reconciler.HardCorrectCount);
            SelfTest.True(reconciler.MaxErrorM == 0.0, "Reset 清计数", reconciler.MaxErrorM.ToString("R"));
        }

        // §5(b)：0.65924/tick 衰减与 0.001m 归零（平滑器是 C04 的 View 层实现）
        private static void ChecksSmoothingDecay()
        {
            var smoother = new ErrorSmoother();
            smoother.Apply(0.5, 0.0, 0.0);
            SelfTest.True(Math.Abs(smoother.Magnitude - 0.5) < 1e-12, "偏移 0.5m", smoother.Magnitude.ToString("R"));
            smoother.Decay(LocalStep.StepDtMs);
            var expected = 0.5 * Math.Exp(-0.05 / ErrorSmoother.TimeConstantSeconds);
            SelfTest.True(Math.Abs(smoother.Magnitude - expected) < 1e-12, "一 tick 衰减到 0.65924 倍", smoother.Magnitude.ToString("R"));
            SelfTest.True(Math.Abs(expected / 0.5 - 0.65924) < 1e-5, "0.65924 与 τ=0.12s 一致", (expected / 0.5).ToString("R"));

            var ticks = 0;
            while (smoother.Magnitude > 0.0 && ticks < 1000)
            {
                smoother.Decay(LocalStep.StepDtMs);
                ticks += 1;
            }
            SelfTest.True(smoother.Magnitude == 0.0, "低于 0.001m 归零", smoother.Magnitude.ToString("R"));
            SelfTest.True(ticks < 100, "归零在有限 tick 内", ticks.ToString());

            // 硬纠正那一路由 View 层 Reset：> 1.0m 的差值走 Snap 而不是平滑
            SelfTest.Equal((long)SmoothingAction.Snap, (long)smoother.Apply(1.5, 0.0, 0.0));
            SelfTest.Equal(1, smoother.SnapCount);
            SelfTest.True(smoother.Magnitude == 0.0, "硬纠正不留下偏移", smoother.Magnitude.ToString("R"));
        }

        // §5(c)：当帧扣弹、只降不升、停火 500ms 自愈、周期重置、开火闸门、过期计入
        private static void ChecksAmmoLedger()
        {
            var ledger = new AmmoLedger();
            ledger.NoteLocalShot(1, 0, 0);
            var first = ledger.Reconcile(12, 30, 0, 0);
            SelfTest.Equal(11, first.Mag);
            SelfTest.Equal(11, first.GateMag);
            SelfTest.Equal(30, first.Reserve);

            ledger.NoteLocalShot(2, 0, 10);
            var second = ledger.Reconcile(12, 30, 0, 10);
            SelfTest.Equal(10, second.Mag);
            SelfTest.Equal(10, second.GateMag);

            // 权威下降 1 发 → 消掉最早一条；乐观值回到 11，但显示值只降不升
            ledger.NoteServerAck(2);
            var afterAck = ledger.Reconcile(11, 30, 0, 20);
            SelfTest.Equal(10, afterAck.Mag);
            SelfTest.Equal(10, afterAck.GateMag);
            SelfTest.Equal(1, ledger.PendingShots);

            // 权威再降 1 发 → 第二条也消账，此后停火 500ms 静默对齐权威
            ledger.Reconcile(10, 30, 0, 30);
            SelfTest.Equal(0, ledger.PendingShots);
            var healed = ledger.Reconcile(10, 30, 0, 520);
            SelfTest.Equal(10, healed.Mag);
            SelfTest.Equal(10, healed.GateMag);

            // 周期重置：换弹使权威上升 → 显示值回到乐观值
            var reloaded = ledger.Reconcile(25, 30, 0, 600);
            SelfTest.Equal(25, reloaded.Mag);
            SelfTest.Equal(25, reloaded.GateMag);

            // 开火闸门：权威 0 时不允许产生曳光
            var empty = ledger.Reconcile(0, 30, 0, 700);
            SelfTest.Equal(0, empty.Mag);
            SelfTest.Equal(0, empty.GateMag);

            // 过期：AckedSeq − 20 > seq 仍未对账 → 计入 RejectedTotal，且不回弹显示值
            var stale = new AmmoLedger();
            stale.NoteLocalShot(5, 0, 0);
            stale.NoteServerAck(30);
            var staleView = stale.Reconcile(9, 30, 0, 0);
            SelfTest.Equal(8, staleView.Mag);
            stale.Reconcile(9, 30, 0, 20);
            SelfTest.Equal(1, stale.RejectedTotal);
            SelfTest.Equal(0, stale.PendingShots);
            SelfTest.Equal(8, stale.Reconcile(9, 30, 0, 40).Mag);
        }

        private static StepCommand Step(ushort seq, double yawRad, sbyte moveX)
        {
            var command = default(StepCommand);
            command.Seq = seq;
            command.Yaw = Quantize.QuantizeAngle(yawRad);
            command.MoveX = Quantize.QuantizeAxis(moveX);   // StepCommand 的轴是线上域（±127）
            return command;
        }
    }
}
