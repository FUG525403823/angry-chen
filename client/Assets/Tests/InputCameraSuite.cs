using System;
using Ac.Core;
using Ac.Net;
using Ac.Sim;
using Ac.View;
using UnityEngine;

namespace Ac.Tests
{
    // C05 §6 的四组用例（外加 sim.local_step，用于覆盖 §7 对局部步进与预测的验收行）。
    public static class InputCameraSuite
    {
        public static void Register()
        {
            SelfTest.Add("input.weapon_slots", ChecksWeaponSlots);
            SelfTest.Add("input.weapon_request_suspended", ChecksWeaponRequestSuspended);
            SelfTest.Add("input.rate_30hz", ChecksRate30Hz);
            SelfTest.Add("input.backlog_merge", ChecksBacklogMerge);
            SelfTest.Add("input.lock_flush", ChecksLockFlush);
            SelfTest.Add("input.ready_bit", ChecksReadyBit);
            SelfTest.Add("input.ready_key", ChecksReadyKey);
            SelfTest.Add("camera.pitch_clamp", ChecksPitchClamp);
            SelfTest.Add("camera.recoil_punch", ChecksRecoilPunch);
            SelfTest.Add("sim.local_step", ChecksLocalStep);
            SelfTest.Add("sim.predictor_fractional_time", ChecksPredictorFractionalTime);
        }

        private static void ChecksWeaponSlots()
        {
            var sampler = new InputSampler();
            InputIntent intent;
            for (var i = 1; i <= 3; i++)
            {
                sampler.SetKey(KeyCode.Q, true);
                sampler.Update(1);
                sampler.Update(1);
                SelfTest.Equal(i % 3, (long)sampler.CurrentSwitchTo);
                sampler.SetKey(KeyCode.Q, false);
                sampler.Update(1);
                SelfTest.Equal(i % 3, (long)sampler.CurrentSwitchTo);
                sampler.Flush();
                SelfTest.True(sampler.TryTakeCommand(out intent), "switch command", "missing");
                SelfTest.Equal(i % 3, (long)intent.SwitchTo);
                SelfTest.True((intent.Buttons & InputSampler.ButtonSwitchWeapon) != 0, "latched short tap", "lost");
                SelfTest.True((sampler.CurrentButtons & InputSampler.ButtonSwitchWeapon) != 0, "same-frame prediction after dequeue", "lost");
                sampler.Update(0);
                SelfTest.Equal(0, (long)(sampler.CurrentButtons & InputSampler.ButtonSwitchWeapon));
            }
            sampler.SetKey(KeyCode.Alpha3, true);
            sampler.Update(1);
            SelfTest.Equal(2, (long)sampler.CurrentSwitchTo);
            sampler.Update(1);
            SelfTest.Equal(2, (long)sampler.CurrentSwitchTo);
            sampler.SetKey(KeyCode.Alpha3, false);
            sampler.SetKey(KeyCode.Alpha1, true);
            sampler.Update(1);
            SelfTest.Equal(0, (long)sampler.CurrentSwitchTo);
            sampler.SetKey(KeyCode.Alpha1, false);
            sampler.SetKey(KeyCode.Alpha2, true);
            sampler.Update(1);
            SelfTest.Equal(1, (long)sampler.CurrentSwitchTo);
            SelfTest.True(!sampler.RequestWeaponSlot(-1) && !sampler.RequestWeaponSlot(3), "invalid slots rejected", "accepted");
        }

        private static void ChecksWeaponRequestSuspended()
        {
            var sampler = new InputSampler();
            sampler.SetKey(KeyCode.Mouse0, true);
            sampler.SetKey(KeyCode.W, true);
            sampler.Suspend();
            SelfTest.True(sampler.RequestWeaponSlot(2), "settings selection accepted", "rejected");
            sampler.Suspend();
            sampler.Flush();
            InputIntent intent;
            sampler.TryTakeCommand(out intent);
            SelfTest.True(sampler.TryTakeCommand(out intent), "selection queued", "missing");
            SelfTest.Equal(InputSampler.ButtonSwitchWeapon, (long)intent.Buttons);
            SelfTest.Equal(2, (long)intent.SwitchTo);
            SelfTest.Equal(0, (long)intent.MoveX);
            SelfTest.Equal(0, (long)intent.MoveY);
            sampler.RequestWeaponSlot(1);
            sampler.Resume();
            sampler.Update(0);
            sampler.Flush();
            SelfTest.True(sampler.TryTakeCommand(out intent), "selection survives immediate settings close", "missing");
            SelfTest.Equal(1, (long)intent.SwitchTo);
            SelfTest.Equal(InputSampler.ButtonSwitchWeapon, (long)intent.Buttons);
            SelfTest.Equal(0, (long)intent.MoveX);
            sampler.OnFocusChanged(false);
            SelfTest.True(!sampler.RequestWeaponSlot(1), "unfocused request rejected", "accepted");
            sampler.TryTakeCommand(out intent);
            SelfTest.Equal(0, (long)intent.Buttons);
        }

        // C09 §5 的后坐注入面：开火当帧顶视角（0.35° 俯仰 + ±0.2° 偏航交替），按 7.5°/s 回正。
        // 回正只还"后坐抬起来的那部分"——玩家自己拖出来的视角不能被吃掉。
        private static void ChecksRecoilPunch()
        {
            var sampler = new InputSampler();
            var pitch0 = sampler.PitchRad;
            var yaw0 = sampler.YawRad;

            sampler.ApplyAimPunch(0.35, 0.2);
            var pitchRad = 0.35 * InputSampler.DegToRad;
            var yawRad = 0.2 * InputSampler.DegToRad;
            SelfTest.True(Math.Abs(sampler.PitchRad - (pitch0 + pitchRad)) < 1e-9, "俯仰顶上去 0.35°",
                sampler.PitchRad.ToString("R"));
            SelfTest.True(Math.Abs(sampler.YawRad - (yaw0 + yawRad)) < 1e-9, "第一发偏航向右 +0.2°",
                sampler.YawRad.ToString("R"));
            SelfTest.True(Math.Abs(sampler.RecoilPitchRad - pitchRad) < 1e-9, "记下要回正的量",
                sampler.RecoilPitchRad.ToString("R"));

            // 交替：第二发往左
            sampler.ApplyAimPunch(0.35, 0.2);
            SelfTest.True(Math.Abs(sampler.YawRad - yaw0) < 1e-9, "第二发偏航回中线（左右交替）",
                sampler.YawRad.ToString("R"));

            // 7.5°/s 回正：0.35° 需要约 47ms（一帧 16.7ms 只还一小部分）
            sampler.Update(16.7);
            SelfTest.True(sampler.RecoilPitchRad > 0.0 && sampler.RecoilPitchRad < 2.0 * pitchRad,
                "回正在进行但没做完", sampler.RecoilPitchRad.ToString("R"));
            for (var i = 0; i < 20; i++) sampler.Update(16.7);
            SelfTest.True(Math.Abs(sampler.RecoilPitchRad) < 1e-9, "后坐回正清零", sampler.RecoilPitchRad.ToString("R"));
            SelfTest.True(Math.Abs(sampler.PitchRad - pitch0) < 1e-9, "俯仰回到开火前", sampler.PitchRad.ToString("R"));

            // 玩家自己的鼠标输入不被回正吃掉：先拖一段视角，再打一发，回正之后仍停在拖到的那个角度
            var turned = new InputSampler();
            turned.AddMouse(300.0, 0.0);
            turned.Update(1.0);
            var turnedYaw = turned.YawRad;
            SelfTest.True(Math.Abs(turnedYaw) > 1e-6, "鼠标真的转了视角", turnedYaw.ToString("R"));
            turned.ApplyAimPunch(0.35, 0.2);
            for (var i = 0; i < 40; i++) turned.Update(16.7);
            SelfTest.True(Math.Abs(turned.YawRad - turnedYaw) < 1e-9, "回正不吃玩家的鼠标输入",
                turned.YawRad.ToString("R"));
        }

        // §6 第 2 条：以 1ms 步进模拟 1000ms → 恰好 30 条（±1）。
        private static void ChecksRate30Hz()
        {
            var sampler = new InputSampler();
            sampler.SetKey(KeyCode.W, true);
            var sent = 0;
            InputIntent last = default(InputIntent);
            for (var i = 0; i < 1000; i++)
            {
                sampler.Update(1.0);
                InputIntent intent;
                if (!sampler.TryTakeCommand(out intent)) continue;
                sent += 1;
                last = intent;
            }
            SelfTest.True(sent >= 29 && sent <= 31, "1000ms 发出 30±1 条", sent.ToString());
            SelfTest.Equal(InputSampler.AxisFull, last.MoveX);
            SelfTest.Equal(0, last.MoveY);
            SelfTest.Equal(0, last.Buttons);

            // 未到间隔不发：单次 1ms 不产生命令。
            var idle = new InputSampler();
            SelfTest.True(!idle.Update(1.0), "未到间隔不发", "发了");
            SelfTest.Equal(0, idle.PendingCount);

            // 采样结果 → 命令载荷 → 字节 → 回读（跨层映射与 §5.2 字节布局）
            var payload = CommandCodec.IntentToPayload(last);
            var stepFromIntent = CommandCodec.ToStepCommand(payload);
            SelfTest.Equal(last.MoveX, stepFromIntent.MoveX);
            SelfTest.Equal(last.Yaw, stepFromIntent.Yaw);
            var roundTrip = CommandCodec.Decode(CommandCodec.Encode(payload), out var decoded);
            SelfTest.Equal(0, (long)roundTrip);
            SelfTest.Equal(last.MoveX, decoded.MoveX);
            SelfTest.Equal(last.Seq, decoded.Seq);
            SelfTest.Equal(InputSampler.UpRateHz, 30);
            SelfTest.True(Math.Abs(InputSampler.UpIntervalMs - 33.3333) < 0.001, "间隔 33.333ms", InputSampler.UpIntervalMs.ToString("R"));
        }

        // §6 第 3 条：连续 5 次采样都不取走 → 队列不超过 MaxBacklog，合并计数为丢掉的条数。
        private static void ChecksBacklogMerge()
        {
            var sampler = new InputSampler();
            for (var i = 0; i < 5; i++) sampler.Update(34.0);
            SelfTest.Equal(InputSampler.MaxBacklog, sampler.PendingCount);
            SelfTest.Equal(3, sampler.CommandsMerged);

            // 队尾一定是最新采样：最后取出的 seq 等于采样器的 Seq。
            ushort lastSeq = 0;
            InputIntent intent;
            while (sampler.TryTakeCommand(out intent)) lastSeq = intent.Seq;
            SelfTest.Equal(sampler.Seq, lastSeq);
            SelfTest.Equal(0, sampler.PendingCount);
        }

        // §6/§5.6：失焦清空全部按键位，flush 出的零意图命令 moveX/moveY/buttons 全 0，且此后不再采样。
        private static void ChecksLockFlush()
        {
            var sampler = new InputSampler();
            sampler.SetKey(KeyCode.W, true);
            sampler.SetKey(KeyCode.A, true);
            sampler.SetKey(KeyCode.Mouse0, true);
            sampler.SetKey(KeyCode.LeftShift, true);
            sampler.Update(40.0);
            InputIntent before;
            SelfTest.True(sampler.TryTakeCommand(out before), "聚焦时能发出命令", "没发出");
            SelfTest.True(before.Buttons != 0, "按键位非零", "全零");
            SelfTest.Equal(InputSampler.ButtonFire | InputSampler.ButtonSprint, before.Buttons);

            sampler.OnFocusChanged(false);
            InputIntent flushed;
            SelfTest.True(sampler.TryTakeCommand(out flushed), "失焦必须 flush 一条", "没 flush");
            SelfTest.Equal(0, flushed.MoveX);
            SelfTest.Equal(0, flushed.MoveY);
            SelfTest.Equal(0, flushed.Buttons);

            // 失焦期间不再采样：再推 100ms 也没有新命令。
            sampler.Update(100.0);
            InputIntent none;
            SelfTest.True(!sampler.TryTakeCommand(out none), "失焦期间不采样", "又采样了");
            sampler.OnFocusChanged(true);
            SelfTest.True(sampler.Update(40.0), "重获焦点后恢复采样", "没恢复");
        }

        // §5.4 的 Ready 位（0x80）：它是**大厅里的状态**（界面"准备"动作设置），不是"按住"的按键，
        // 所以必须 ①真的进到命令载荷与字节里，②与其它按键位可共存，③不被局内失焦清零带走。
        private static void ChecksReadyBit()
        {
            SelfTest.Equal(128, (long)InputSampler.ButtonReady);

            var sampler = new InputSampler();
            sampler.Update(40.0);
            InputIntent idle;
            SelfTest.True(sampler.TryTakeCommand(out idle), "未准备时照发命令", "没发出");
            SelfTest.Equal(0, idle.Buttons);
            SelfTest.True(!sampler.ReadyHeld, "默认未准备", sampler.ReadyHeld.ToString());

            sampler.SetReadyHeld(true);
            sampler.Update(40.0);
            InputIntent ready;
            SelfTest.True(sampler.TryTakeCommand(out ready), "准备后发命令", "没发出");
            SelfTest.Equal(InputSampler.ButtonReady, ready.Buttons);
            // 跨层映射 + 字节布局：0x80 必须真的出现在 CommandPayload 与编码后的字节里（第 6 字节 = buttons）。
            var payload = CommandCodec.IntentToPayload(ready);
            SelfTest.Equal(InputSampler.ButtonReady, payload.Buttons);
            SelfTest.Equal(0, (long)CommandCodec.Decode(CommandCodec.Encode(payload), out var decoded));
            SelfTest.Equal(InputSampler.ButtonReady, decoded.Buttons);

            // 与其它按键位相或：服务端按位读，准备位不与开火/疾跑互斥。
            sampler.SetKey(KeyCode.Mouse0, true);
            sampler.Update(40.0);
            InputIntent both;
            SelfTest.True(sampler.TryTakeCommand(out both), "准备 + 开火", "没发出");
            SelfTest.Equal(InputSampler.ButtonReady | InputSampler.ButtonFire, both.Buttons);

            // 失焦：§5.6 要求那条 flush 命令是**零意图**（按钮全 0），但"已准备"这个状态不能被撤销
            // ——撤销了就等于"掉个焦点就不会开局的房间"，重获焦点后它必须随状态回来。
            sampler.OnFocusChanged(false);
            InputIntent flushed;
            SelfTest.True(sampler.TryTakeCommand(out flushed), "失焦必须 flush 一条", "没 flush");
            SelfTest.Equal(0, flushed.Buttons);
            SelfTest.True(sampler.ReadyHeld, "失焦不撤销已准备", sampler.ReadyHeld.ToString());
            sampler.OnFocusChanged(true);
            sampler.Update(40.0);
            InputIntent refocused;
            SelfTest.True(sampler.TryTakeCommand(out refocused), "重获焦点后继续发", "没发出");
            SelfTest.Equal(InputSampler.ButtonReady, refocused.Buttons);

            sampler.SetReadyHeld(false);
            sampler.Update(40.0);
            InputIntent cleared;
            SelfTest.True(sampler.TryTakeCommand(out cleared), "取消准备", "没发出");
            SelfTest.Equal(0, cleared.Buttons);
        }

        // 大厅准备键（ADR-013）：采样器只报**按下沿**，按住不重复 —— 重复报会让 GameLoop 里的
        // "按一次翻转一次准备位"被连点来回翻。语义（相位判定、翻转）在 GameLoop，这里只钉边沿。
        private static void ChecksReadyKey()
        {
            var sampler = new InputSampler();
            sampler.SetConfirm(true);
            sampler.Update(50.0);
            SelfTest.True(sampler.ConfirmPressed, "按下的那一帧必须报边沿", "没报");
            sampler.Update(50.0);
            SelfTest.True(!sampler.ConfirmPressed, "按住不许重复报", "又报了");
            sampler.SetConfirm(false);
            sampler.Update(50.0);
            SelfTest.True(!sampler.ConfirmPressed, "松开那一帧不是按下沿", "报了");
            sampler.SetConfirm(true);
            sampler.Update(50.0);
            SelfTest.True(sampler.ConfirmPressed, "再按一次必须是新的边沿", "没报");
            sampler.Update(50.0);
            SelfTest.True(!sampler.ConfirmPressed, "第二次按住也不重复", "又报了");
        }

        // §5.5/§7：俯仰钳到 ±π/2（越界输入不产生滚动），FOV 钳在 60..100，视图模型参数逐条对表。
        private static void ChecksPitchClamp()
        {
            var sampler = new InputSampler();
            sampler.AddMouse(0.0, 100000.0);    // ADR-015：上移为正 ⇒ 巨大的上移，pitch 往上撞上限
            sampler.Update(40.0);
            InputIntent up;
            SelfTest.True(sampler.TryTakeCommand(out up), "采样出命令", "没发出");
            SelfTest.True(Math.Abs(Quantize.DequantizeAngle(up.Pitch) - InputSampler.PitchLimitRad) < 1e-3,
                "俯仰钳到 +π/2", Quantize.DequantizeAngle(up.Pitch).ToString("R"));
            SelfTest.True(Math.Abs(sampler.PitchRad - InputSampler.PitchLimitRad) < 1e-12, "命令姿态同步钳制", sampler.PitchRad.ToString("R"));
            SelfTest.True(sampler.Focused, "默认聚焦", "未聚焦");

            var down = new InputSampler();
            down.AddMouse(0.0, -100000.0);      // 巨大的下移：撞下限
            down.Update(40.0);
            InputIntent low;
            down.TryTakeCommand(out low);
            SelfTest.True(Math.Abs(Quantize.DequantizeAngle(low.Pitch) + InputSampler.PitchLimitRad) < 1e-3,
                "俯仰钳到 -π/2", Quantize.DequantizeAngle(low.Pitch).ToString("R"));

            // ADR-015：水平轴同向 —— 鼠标右移视角右转（yaw 增大），左移反向。
            var turnedRight = new InputSampler();
            turnedRight.AddMouse(300.0, 0.0);   // 灵敏度提高后 1000px 会绕圈（yaw 取模），用不绕圈的位移验方向
            turnedRight.Update(40.0);
            InputIntent turned;
            SelfTest.True(turnedRight.TryTakeCommand(out turned), "右移也要出命令", "没发出");
            SelfTest.True(turnedRight.YawRad > 0.0, "右移 ⇒ yaw 增大（视角右转）", turnedRight.YawRad.ToString("R"));
            var turnedLeft = new InputSampler();
            turnedLeft.AddMouse(-300.0, 0.0);
            turnedLeft.Update(40.0);
            SelfTest.True(turnedLeft.YawRad < 0.0, "左移 ⇒ yaw 减小（视角左转）", turnedLeft.YawRad.ToString("R"));
            SelfTest.True(Math.Abs(FpsCamera.ClampPitch(3.0) - FpsCamera.PitchLimitRad) < 1e-12, "相机俯仰上限", FpsCamera.ClampPitch(3.0).ToString("R"));
            SelfTest.True(Math.Abs(FpsCamera.ClampPitch(-3.0) + FpsCamera.PitchLimitRad) < 1e-12, "相机俯仰下限", FpsCamera.ClampPitch(-3.0).ToString("R"));

            // 灵敏度范围 0.2..3.0，超出即钳制；鼠标增量只按 GetAxisRaw 口径计入
            var sensitivity = new InputSampler();
            sensitivity.SetSensitivity(9.0);
            SelfTest.Equal((long)(InputSampler.MaxSensitivity * 1000.0),   // 不写死：上限从 3.0 提到 5.0（实跑手感）
                (long)(sensitivity.SensitivityValue * 1000.0));
            sensitivity.SetSensitivity(0.0);
            SelfTest.Equal(200, (long)(sensitivity.SensitivityValue * 1000.0 + 0.5));

            var camera = new FpsCamera();
            camera.ApplyFov(200.0);
            SelfTest.Equal((long)FpsCamera.MaxFov, (long)camera.Fov);
            camera.ApplyFov(10.0);
            SelfTest.Equal((long)FpsCamera.MinFov, (long)camera.Fov);
            camera.ApplyFov(75.0);
            SelfTest.Equal((long)FpsCamera.DefaultFov, (long)camera.Fov);
            camera.SetPose(1.0, 2.0, 3.0, 0.5, 9.0);
            SelfTest.True(Math.Abs(camera.Y - (2.0 + FpsCamera.EyeHeightMeters)) < 1e-12, "相机眼高", camera.Y.ToString("R"));
            SelfTest.True(Math.Abs(camera.PitchRad - FpsCamera.PitchLimitRad) < 1e-12, "相机姿态俯仰受限", camera.PitchRad.ToString("R"));
            camera.RequestPointerLock();
            SelfTest.True(camera.Locked, "锁定指针", "没锁定");
            sampler.SetPointerLocked(true);
            SelfTest.True(sampler.PointerLocked, "指针锁定状态可注入", "没记住");
            sampler.SetPointerLocked(false);
            SelfTest.True(!sampler.PointerLocked, "解锁后可注入", "仍锁定");
            camera.OnFocusChanged(false);
            SelfTest.True(!camera.Locked, "失焦解锁", "仍锁定");
            // §5.5 冻结值的交叉校验：三处各写一遍的 π/2 与两处近远裁必须同值
            SelfTest.True(Math.Abs(FpsCamera.PitchLimitRad - InputSampler.PitchLimitRad) < 1e-15, "π/2 常量同值", FpsCamera.PitchLimitRad.ToString("R"));
            SelfTest.True(Math.Abs(FpsCamera.NearClipMeters - 0.1) < 1e-12 && Math.Abs(FpsCamera.FarClipMeters - 300.0) < 1e-12, "相机近远裁", FpsCamera.NearClipMeters.ToString("R"));
            SelfTest.True(Math.Abs(ViewModelAnchor.NearClipMeters - 0.01) < 1e-12 && Math.Abs(ViewModelAnchor.FarClipMeters - 12.0) < 1e-12, "视模型近远裁", ViewModelAnchor.NearClipMeters.ToString("R"));
            SelfTest.Equal(62, (long)ViewModelAnchor.Fov);
            SelfTest.True(Math.Abs(ViewModelAnchor.OffsetX - 0.17) < 1e-12 && Math.Abs(ViewModelAnchor.OffsetY + 0.19) < 1e-12
                && Math.Abs(ViewModelAnchor.OffsetZ + 0.02) < 1e-12, "视图模型基座偏移", ViewModelAnchor.OffsetX.ToString("R"));
        }

        // §5.3/§5.4/§7：步进顺序、归一化、冲刺速度上限、谷仓推离、栅栏夹取、子步进上限与渲染外推。
        private static void ChecksLocalStep()
        {
            var config = MoveConfig.Default();
            // 起点选在谷仓盒外（|z| > 4.4）且远离栅栏，才能观察纯积分。
            var state = At(0.0, 10.0);
            var forward = Command(1, 0, 0.0, 0);
            SelfTest.True(LocalStep.StepLocalPlayer(ref state, forward, config, 50), "50ms 步进成功", "被拒");
            SelfTest.True(Math.Abs(state.Z - (10.0 + 4.5 * 0.05)) < 1e-12, "前进一步 0.225m", state.Z.ToString("R"));
            SelfTest.True(Math.Abs(state.X) < 1e-12, "侧向不动", state.X.ToString("R"));
            SelfTest.True(state.Vy == 0.0, "不做垂直积分", state.Vy.ToString("R"));

            var sprintState = At(0.0, 10.0);
            LocalStep.StepLocalPlayer(ref sprintState, Command(1, 0, 0.0, InputSampler.ButtonSprint), config, 50);
            SelfTest.True(Math.Abs(sprintState.Z - (10.0 + 6.3 * 0.05)) < 1e-12, "冲刺一步 0.315m", sprintState.Z.ToString("R"));
            SelfTest.True(sprintState.Z - 10.0 <= 6.3 * 0.05 + 1e-12, "单步位移不超过 0.315m", (sprintState.Z - 10.0).ToString("R"));

            // 斜向归一化：vx=vz=1/√2
            var diagonal = At(0.0, 10.0);
            LocalStep.StepLocalPlayer(ref diagonal, Command(1, 1, 0.0, 0), config, 50);
            var inv = 1.0 / System.Math.Sqrt(2.0);
            SelfTest.True(Math.Abs(diagonal.X - inv * 4.5 * 0.05) < 1e-12, "斜向 x 归一化", diagonal.X.ToString("R"));
            SelfTest.True(Math.Abs(diagonal.Z - (10.0 + inv * 4.5 * 0.05)) < 1e-12, "斜向 z 归一化", diagonal.Z.ToString("R"));

            // 谷仓推离：从盒内出发必被推到外扩盒面上，该轴速度清零
            SelfTest.True(Math.Abs(config.BarnMaxX - 4.0) < 1e-12 && Math.Abs(config.BarnMinX + 4.0) < 1e-12
                && Math.Abs(config.BarnMaxZ - 4.0) < 1e-12 && Math.Abs(config.BarnMinZ + 4.0) < 1e-12
                && Math.Abs(config.BarnMaxY - 5.0) < 1e-12, "谷仓盒 x/z ±4、y 0..5", config.BarnMaxX.ToString("R"));
            var inside = At(0.0, 0.0);
            LocalStep.StepLocalPlayer(ref inside, Command(1, 0, 0.0, 0), config, 50);
            SelfTest.True(Math.Abs(inside.X - 4.4) < 1e-12 || Math.Abs(inside.Z - 4.4) < 1e-12
                || Math.Abs(inside.X + 4.4) < 1e-12 || Math.Abs(inside.Z + 4.4) < 1e-12,
                "盒内被推到 4 + 半径 = 4.4 的盒面", inside.X.ToString("R") + "," + inside.Z.ToString("R"));
            SelfTest.True(inside.Vx == 0.0 || inside.Vz == 0.0, "被推的轴速度清零", inside.Vx.ToString("R"));

            // 栅栏夹取：limit = 40 - 0.25 - 0.4 = 39.35
            var limit = config.HalfSizeMeters - config.FenceHalfThicknessMeters - config.RadiusMeters;
            SelfTest.True(Math.Abs(limit - 39.35) < 1e-12, "边界 limit 39.35", limit.ToString("R"));
            var fence = At(39.0, 10.0);
            for (var i = 0; i < 8; i++) LocalStep.StepLocalPlayer(ref fence, Command(0, 1, 0.0, InputSampler.ButtonSprint), config, 50);
            SelfTest.True(Math.Abs(fence.X - limit) < 1e-12, "夹到 limit", fence.X.ToString("R"));
            SelfTest.True(fence.Vx == 0.0, "夹取轴速度清零", fence.Vx.ToString("R"));

            // 步长不是 50 时拒绝（S06 §5.1：禁止变长 dt）
            var odd = At(0.0, 10.0);
            SelfTest.True(!LocalStep.StepLocalPlayer(ref odd, forward, config, 33), "非 50ms 拒绝", "接受了");

            // Predictor：子步进、累加器上限、最每帧 5 步、渲染外推
            var predictor = new Predictor();
            predictor.SetConfig(MoveConfig.Default());
            var command = Command(1, 0, 0.0, 0);
            SelfTest.Equal(0, predictor.Advance(20, command));
            SelfTest.Equal(1, predictor.Advance(40, command));
            SelfTest.Equal(10, predictor.AccumulatorMs);
            double rx;
            double ry;
            double rz;
            predictor.RenderPosition(out rx, out ry, out rz);
            SelfTest.True(Math.Abs(rz - (predictor.State.Z + predictor.State.Vz * 0.01)) < 1e-12, "渲染外推 = pos + vel*余量", rz.ToString("R"));
            SelfTest.Equal(5, predictor.Advance(1000, command));
            SelfTest.True(predictor.DroppedSubsteps > 0, "超出部分计入 droppedSubsteps", predictor.DroppedSubsteps.ToString());
            SelfTest.Equal(Predictor.MaxAccumulatorMs, predictor.AccumulatorMs);
            SelfTest.True(predictor.Steps > 0, "步进次数被记录", predictor.Steps.ToString());
            predictor.SetAuthoritative(1.0, 0.0, 2.0, 0, 0);
            SelfTest.True(Math.Abs(predictor.State.X - 1.0) < 1e-12, "权威姿态写入", predictor.State.X.ToString("R"));
        }

        private static void ChecksPredictorFractionalTime()
        {
            var command = Command(1, 0, 0.0, 0);
            foreach (var fps in new[] { 30, 60, 120, 144, 240 })
            {
                var predictor = new Predictor();
                predictor.SetAuthoritative(0.0, 0.0, 10.0, 0, 0);
                for (var frame = 0; frame < fps; frame++) predictor.Advance(1000.0 / fps, command);
                SelfTest.Equal(20, predictor.Steps);
                SelfTest.Equal(0, predictor.AccumulatorMs);
                SelfTest.Equal(0, predictor.DroppedSubsteps);
                SelfTest.True(Math.Abs(predictor.State.Z - 14.5) < 1e-9,
                    "一秒预测不随帧率丢时间", fps + ":" + predictor.State.Z.ToString("R"));
                double x, y, z;
                predictor.RenderPosition(out x, out y, out z);
                SelfTest.True(Math.Abs(z - 14.5) < 1e-9, "一秒整无残留外推", z.ToString("R"));
                predictor.Advance(0.5, command);
                SelfTest.Equal(0, predictor.AccumulatorMs);
                predictor.RenderPosition(out x, out y, out z);
                SelfTest.True(Math.Abs(z - 14.50225) < 1e-9, "半毫秒余量保留用于渲染", z.ToString("R"));
                foreach (var invalid in new[] { double.NaN, double.PositiveInfinity, double.NegativeInfinity, -1.0, 0.0 })
                    SelfTest.Equal(0, predictor.Advance(invalid, command));
                predictor.RenderPosition(out x, out y, out z);
                SelfTest.True(Math.Abs(z - 14.50225) < 1e-9, "非法时间不污染预测余量", z.ToString("R"));
                SelfTest.Equal(20, predictor.Steps);
            }
        }

        private static MoveState At(double x, double z)
        {
            var state = default(MoveState);
            state.X = x;
            state.Z = z;
            return state;
        }

        private static StepCommand Command(sbyte moveX, sbyte moveY, double yawRad, byte buttons)
        {
            var command = default(StepCommand);
            command.MoveX = (sbyte)(moveX * InputSampler.AxisFull);
            command.MoveY = (sbyte)(moveY * InputSampler.AxisFull);
            command.Yaw = Quantize.QuantizeAngle(yawRad);
            command.Buttons = buttons;
            return command;
        }
    }
}
