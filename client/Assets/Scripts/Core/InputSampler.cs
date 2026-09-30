using System;
using UnityEngine;

namespace Ac.Core
{
    // C05 §5.1：采样出来的意图。Ac.Core 不引用 Ac.Net/Ac.Sim（ContractSuite 白名单），
    // 这里的字段与 `CommandPayload` 一一对应，映射由 `CommandCodec.IntentToPayload` 负责。
    public struct InputIntent
    {
        public sbyte MoveX;
        public sbyte MoveY;
        public ushort Yaw;
        public ushort Pitch;
        public byte Buttons;
        public byte SwitchTo;
        public ushort Seq;
        public uint ClientTick;
    }

    // C05 §5.1/§5.2/§5.6：内置 Input 采样 + 30Hz 上行 + 积压合并 + 失焦清理。
    public sealed class InputSampler
    {
        public const int UpRateHz = 30;
        public const double UpIntervalMs = 1000.0 / UpRateHz;   // 33.333ms
        public const int MaxBacklog = 2;
        public const byte ButtonFire = 1;
        public const byte ButtonSprint = 2;
        public const byte ButtonJump = 4;
        public const byte ButtonReload = 8;
        public const byte ButtonInteract = 16;
        public const byte ButtonRage = 32;
        public const byte ButtonSwitchWeapon = 64;
        public const byte ButtonReady = 128;
        public const double PitchLimitRad = 1.5707963267948966;   // π/2
        // 2026-09-30 实跑修正：0.001 rad/px 在 1440p 下“鼠标灵敏度巨低、转不动”（玩家实测）。
        // 现在 0.0035 rad/px × 默认 1.2 ≈ 0.24°/点：200 点快拉 ≈ 48°，接近常见 FPS 手感；
        // 具体倍率仍然走设置里的灵敏度（C13），这里只把基数调到合理区间。
        public const double MouseRadPerPixel = 0.0035;
        public const double MinSensitivity = SettingsDefaults.SensitivityMin;
        public const double MaxSensitivity = SettingsDefaults.SensitivityMax;   // 上限一起抬：不然“最高灵敏度”也不够用
        public const int AngleUnitsPerTurn = 65536;               // 与 Quantize.AngleUnits 同值
        public const sbyte AxisFull = 127;                        // 与 Quantize.QuantizeAxis(±1) 同值
        // 待发队列就是 §5.1 的"未确认命令"，上限 MaxBacklog；FIFO，取走方向固定为 _pending[0]。
        private readonly InputIntent[] _pending = new InputIntent[MaxBacklog];
        private int _pendingCount;

        private bool _injected;
        private readonly bool[] _keys = new bool[12];
        private double _mouseDx;
        private double _mouseDy;
        private bool _confirmDown;
        private bool _confirmWasDown;

        public InputSampler()
        {
            SensitivityValue = SettingsDefaults.Sensitivity;
            ClientTick = 0;
            Focused = true;
            PitchRad = 0.0;
            YawRad = 0.0;
        }

        public ushort Seq { get; private set; }
        public uint ClientTick { get; private set; }
        // §5.4 的 Ready 位（0x80）：**大厅里的持续状态**，不是"按住的按钮"——所以它由界面的"准备"动作
        // （或按键翻一次）设置，不参与失焦清零。服务端只在大厅相位读它（ADR-013）。
        public bool ReadyHeld { get; private set; }
        public void SetReadyHeld(bool ready) { ReadyHeld = ready; }
        public double SensitivityValue { get; private set; }
        public double YawRad { get; private set; }
        public double PitchRad { get; private set; }
        // 本帧的**当前**按钮位（不消耗待发队列、不推进 seq）：本地武器镜像按它逐帧决定"响没响"。
        // 它必须和上行命令的按钮位同源同值，否则会出现"画面上开火了、服务端那一格没有 Fire 位"
        // （上行按 30Hz 采样，本地按帧判定）。
        public byte CurrentButtons { get { return SampleButtons(); } }
        public byte CurrentSwitchTo { get { return _switchTo; } }

        // 后坐注入面（C09 §5：俯仰 0.35°/发、偏航 ±0.2°/发、衰减 7.5/s）。开火当帧把视角顶上去
        // （下一帧上行就带上这个偏移 ⇒ 服务端射线方向跟着后坐走，跟 CS 一样），随后按 7.5/s 回正。
        // 回正的只是"后坐抬起来的那部分"：玩家自己拖的鼠标不会被回正吃掉。
        public const double RecoilDecayPerSecond = 7.5;
        public const double DegToRad = 0.017453292519943295;   // π/180（与服务端 kDegToRad 同值）
        private double _recoilRecoverPitchRad;
        private double _recoilRecoverYawRad;
        private int _punchYawSign = 1;

        // 偏航左右交替、幅度固定——与 WeaponAnim 同一约定（不用随机数，回放可复现）。
        public void ApplyAimPunch(double pitchDeg, double yawJitterDeg)
        {
            var pitchRad = pitchDeg * DegToRad;
            var yawRad = yawJitterDeg * _punchYawSign * DegToRad;
            _punchYawSign = -_punchYawSign;
            PitchRad += pitchRad;
            if (PitchRad > PitchLimitRad) PitchRad = PitchLimitRad;
            else if (PitchRad < -PitchLimitRad) PitchRad = -PitchLimitRad;
            YawRad = WrapRadians(YawRad + yawRad);
            _recoilRecoverPitchRad += pitchRad;
            _recoilRecoverYawRad += yawRad;
        }

        public double RecoilPitchRad { get { return _recoilRecoverPitchRad; } }
        public double RecoilYawRad { get { return _recoilRecoverYawRad; } }
        public bool Focused { get; private set; }
        public int CommandsMerged { get; private set; }
        public int PendingCount { get { return _pendingCount; } }
        // §5.6：只有指针锁定时鼠标增量才计入视角（引擎路径；注入路径不受影响）。
        public bool PointerLocked { get; private set; }

        // 大厅"准备"确认键（ADR-013）：采样器只把**按下沿**报出去，切不切换准备位、在哪个相位生效由
        // GameLoop 定（采样器不知道相位）。按住不重复报 —— 重复报会让准备位被连点来回翻转。
        public bool ConfirmPressed { get; private set; }

        // 键位来自 SettingsDefaults.KeyBindings[ActionReady]（Boot 侧解析后写进来），默认表那条就是 Return。
        public KeyCode ConfirmKey = KeyCode.Return;

        public void SetPointerLocked(bool locked) { PointerLocked = locked; }

        private double _sinceLastSendMs;
        private byte _switchTo;
        private bool _switchWasDown;
        // §5.6：失焦清理之后，按键位与鼠标增量一律按 0 处理，直到重新获得焦点。
        // 不能只清注入的 _keys：真实播放态读的是引擎 Input，按键仍然是按住的。
        private bool _intentCleared;

        public void SetClientTick(uint clientTick) { ClientTick = clientTick; }

        public void SetSensitivity(double value)
        {
            if (double.IsNaN(value)) value = SettingsDefaults.Sensitivity;
            if (value < MinSensitivity) value = MinSensitivity;
            if (value > MaxSensitivity) value = MaxSensitivity;
            SensitivityValue = value;
        }

        // 注入面：用例与回放用；调用过就完全脱离引擎 Input。
        public void SetKey(KeyCode code, bool down)
        {
            _injected = true;
            var index = IndexOf(code);
            if (index < 0) return;
            _keys[index] = down;
        }

        public void AddMouse(double dx, double dy)
        {
            _injected = true;
            _mouseDx += dx;
            _mouseDy += dy;
        }

        // 确认键的注入面（用例/回放）。不塞进 §5.1 的键位表：那张表是局内意图，准备位不是局内意图。
        public void SetConfirm(bool down)
        {
            _injected = true;
            _confirmDown = down;
        }

        // §5.6：失焦立即清空全部按键位与移动轴，并 flush 一条零意图命令。
        public void OnFocusChanged(bool focused)
        {
            Focused = focused;
            if (focused)
            {
                // 聊天缓冲开着时重新获得焦点，意图仍然按 0 处理：恢复由 Resume() 负责，不由焦点负责
                _intentCleared = Suspended;
                return;
            }
            _intentCleared = true;
            PointerLocked = false;
            ClearIntentInternal();
            Enqueue(ZeroIntent());   // §5.6：失焦这条命令必须"零意图"，不能再读引擎 Input
            _sinceLastSendMs = 0.0;
        }

        // 局内聊天（键位表第 11 条 `chat`）：打字期间意图一律按 0 处理，但**不改 Focused** —— 30Hz 上行
        // 还要继续报零意图（服务端按"缺命令"处理会让玩家停在原地，按"零意图"处理语义更明确）。
        // 与失焦的区别：Suspend 不动 PointerLocked，指针锁定由 Driver 按缓冲开关单独管。
        public bool Suspended { get; private set; }

        public void Suspend()
        {
            if (Suspended) return;
            Suspended = true;
            _intentCleared = true;
            ClearIntentInternal();
            Enqueue(ZeroIntent());   // 立刻把"别再动"报出去，与失焦同一条口径
            _sinceLastSendMs = 0.0;
        }

        public void Resume()
        {
            if (!Suspended) return;
            Suspended = false;
            _intentCleared = false;
        }

        // §9/§5.6：绕过 30Hz 间隔立刻发一条（失焦时也照发，零意图命令就是"别再动"的唯一手段）。
        public void Flush()
        {
            Enqueue(BuildIntent());
            _sinceLastSendMs = 0.0;
        }

        public bool TryTakeCommand(out InputIntent intent)
        {
            intent = default(InputIntent);
            if (_pendingCount == 0) return false;
            intent = _pending[0];
            _pending[0] = _pending[1];
            _pendingCount -= 1;
            return true;
        }

        // §5.1：1ms 级步进调用；间隔未到不发。返回本帧是否发送/合并了一条。
        public bool Update(double dtMs)
        {
            if (Application.isPlaying && !Application.isFocused) OnFocusChanged(false);
            if (!Focused) return false;
            if (dtMs < 0.0) dtMs = 0.0;
            Sample();
            DecayRecoil(dtMs);
            _sinceLastSendMs += dtMs;
            if (_sinceLastSendMs < UpIntervalMs) return false;
            _sinceLastSendMs -= UpIntervalMs;
            if (_sinceLastSendMs < 0.0) _sinceLastSendMs = 0.0;
            Enqueue(BuildIntent());
            return true;   // 本帧到了上行间隔，出了一条（可能合并了旧条）命令
        }

        // §5.1：待发队列最多留 MaxBacklog(2) 条未确认命令；超过就丢最旧的一条（除最新外的都没必要留），
        // 每丢一条计一次 commandsMerged。丢的永远是旧意图，队列里始终有最新采样。
        private void Enqueue(in InputIntent intent)
        {
            if (_pendingCount == MaxBacklog)
            {
                _pending[0] = _pending[1];
                _pendingCount = 1;
                CommandsMerged += 1;
            }
            _pending[_pendingCount] = intent;
            _pendingCount += 1;
        }

        private void Sample()
        {
            if (!_injected && PointerLocked)
            {
                _mouseDx += Input.GetAxisRaw("Mouse X");
                _mouseDy += Input.GetAxisRaw("Mouse Y");
            }
            var scale = MouseRadPerPixel * SensitivityValue;
            // ADR-015：本仓 pitch 以**抬头为正**（相机 Euler(-pitch)、服务端 yawPitchToDirection 的 +Y 分量），
            // 而引擎的 Mouse X/Y 是 invert:0（右移为正、上移为正）⇒ 两轴取加号才是"鼠标方向 = 视角方向"。
            // C05 §5.5 原来的 -= 抄自"pitch 以低头为正"的经典片段，在本仓语义下等于双重取反（视角两轴全反）。
            YawRad += _mouseDx * scale;
            PitchRad += _mouseDy * scale;
            _mouseDx = 0.0;
            _mouseDy = 0.0;
            if (PitchRad > PitchLimitRad) PitchRad = PitchLimitRad;
            if (PitchRad < -PitchLimitRad) PitchRad = -PitchLimitRad;
            YawRad = WrapRadians(YawRad);
            // §5.1 的 switchTo 槽：Q 的按下沿才翻槽，按住不重复翻。槽值域与 WeaponSlot 对齐（0/1）。
            var switchDown = KeyDown(KeyCode.Q);
            if (switchDown && !_switchWasDown) _switchTo = (byte)(_switchTo == 0 ? 1 : 0);
            _switchWasDown = switchDown;
            // 大厅准备键的按下沿。引擎路径读真实按键（键位由 ConfirmKey 给，默认表的 ActionReady）；注入路径用 SetConfirm。
            var confirmDown = _injected ? _confirmDown : Input.GetKey(ConfirmKey);
            ConfirmPressed = confirmDown && !_confirmWasDown;
            _confirmWasDown = confirmDown;
        }

        private InputIntent ZeroIntent()
        {
            var intent = default(InputIntent);
            intent.Seq = NextSeq();
            intent.ClientTick = ClientTick;
            return intent;
        }

        private ushort NextSeq()
        {
            Seq = (ushort)((Seq + 1) & 0xFFFF);
            return Seq;
        }

        private InputIntent BuildIntent()
        {
            var intent = ZeroIntent();
            // 准备位先落，且**不受失焦清零影响**：失焦清的是移动/视角/开火那一套局内意图，而"已准备"
            // 是玩家在大厅做出的承诺，重获焦点前撤销它会让大厅永远开不了局。服务端只在大厅相位采用它。
            if (ReadyHeld) intent.Buttons |= ButtonReady;
            // 失焦清理之后一律零意图：引擎路径读的是真实按键，不能只看 _keys。
            if (_intentCleared) return intent;
            // §5.1 采样源：移动轴走 Input.GetAxisRaw("Vertical"/"Horizontal")，按钮走 Input.GetKey；
            // 注入路径（用例与回放）用 SetKey 的键位表代替轴，语义同为 ±1。
            intent.MoveX = _injected
                ? Axis(KeyDown(KeyCode.W), KeyDown(KeyCode.S))
                : AxisValue(Input.GetAxisRaw("Vertical"));
            intent.MoveY = _injected
                ? Axis(KeyDown(KeyCode.A), KeyDown(KeyCode.D))
                : AxisValue(Input.GetAxisRaw("Horizontal"));
            intent.Yaw = QuantizeRadians(YawRad);
            intent.Pitch = QuantizeRadians(PitchRad);
            // 与 ReadyHeld 相或：准备位是状态，不随按键采样消失（见 BuildIntent 顶部）。
            intent.Buttons = (byte)((SampleButtons() | intent.Buttons) & 0xff);
            intent.SwitchTo = _switchTo;
            return intent;
        }

        // §5.1 的局内按钮位（引擎路径读真实按键、注入路径读键位表；失焦/挂起后一律 0——KeyDown 自己
        // 看 _intentCleared）。Ready 位**不在这里**：它是状态不是按键（见 BuildIntent 顶部）。
        // 本地武器镜像每帧按它决定"响没响"，与上行命令用同一份判读。
        private byte SampleButtons()
        {
            var buttons = 0;
            if (KeyDown(KeyCode.Mouse0)) buttons |= ButtonFire;
            if (KeyDown(KeyCode.LeftShift) || KeyDown(KeyCode.RightShift)) buttons |= ButtonSprint;
            if (KeyDown(KeyCode.Space)) buttons |= ButtonJump;
            if (KeyDown(KeyCode.R)) buttons |= ButtonReload;
            if (KeyDown(KeyCode.E)) buttons |= ButtonInteract;
            if (KeyDown(KeyCode.F)) buttons |= ButtonRage;
            if (KeyDown(KeyCode.Q)) buttons |= ButtonSwitchWeapon;
            return (byte)buttons;
        }

        // 后坐回正：线性速率 7.5/s（与 WeaponAnim.Decay 同口径——那条是 View 层的后坐动画，
        // 这里回正的是**视角**）。回正量从视角里扣掉同样的弧度，玩家自己拖的鼠标不受影响。
        private void DecayRecoil(double dtMs)
        {
            if (_recoilRecoverPitchRad == 0.0 && _recoilRecoverYawRad == 0.0) return;
            // 7.5 是**度**/秒（C09 §5 的冻结值），而存下来的要回正的量是弧度：这里必须换算，
            // 否则回正会快 57 倍（一帧就把后坐还完，视角等于完全不抬）。
            var amount = RecoilDecayPerSecond * DegToRad * dtMs / 1000.0;
            var pitchBack = _recoilRecoverPitchRad > amount ? amount : _recoilRecoverPitchRad;
            var yawBack = _recoilRecoverYawRad > amount
                ? amount
                : (_recoilRecoverYawRad < -amount ? -amount : _recoilRecoverYawRad);
            _recoilRecoverPitchRad -= pitchBack;
            _recoilRecoverYawRad -= yawBack;
            PitchRad -= pitchBack;
            if (PitchRad > PitchLimitRad) PitchRad = PitchLimitRad;
            else if (PitchRad < -PitchLimitRad) PitchRad = -PitchLimitRad;
            YawRad = WrapRadians(YawRad - yawBack);
        }

        private void ClearIntentInternal()
        {
            for (var i = 0; i < _keys.Length; i++) _keys[i] = false;
            _mouseDx = 0.0;
            _mouseDy = 0.0;
        }

        private bool KeyDown(KeyCode code)
        {
            if (_intentCleared) return false;
            var index = IndexOf(code);
            if (index < 0) return false;
            return _injected ? _keys[index] : Input.GetKey(code);
        }

        private static sbyte AxisValue(double value)
        {
            if (value > 0.0) return AxisFull;
            if (value < 0.0) return (sbyte)(-AxisFull);
            return 0;
        }

        private static sbyte Axis(bool positive, bool negative)
        {
            if (positive && !negative) return AxisFull;
            if (negative && !positive) return (sbyte)(-AxisFull);
            return 0;
        }

        // §5.1：yaw/pitch 的量化和 Quantize 同规则（这里自己算，Ac.Core 不能引用 Ac.Sim）。
        private static ushort QuantizeRadians(double radians)
        {
            var turns = radians / (2.0 * Math.PI);
            var units = (long)Math.Floor(turns * AngleUnitsPerTurn + 0.5);
            return (ushort)(units & 0xFFFF);
        }

        private static double WrapRadians(double radians)
        {
            var turn = 2.0 * Math.PI;
            radians %= turn;
            if (radians < 0.0) radians += turn;
            if (radians >= Math.PI) radians -= turn;
            return radians;
        }

        private static int IndexOf(KeyCode code)
        {
            switch (code)
            {
                case KeyCode.W: return 0;
                case KeyCode.S: return 1;
                case KeyCode.A: return 2;
                case KeyCode.D: return 3;
                case KeyCode.LeftShift: return 4;
                case KeyCode.RightShift: return 5;
                case KeyCode.Space: return 6;
                case KeyCode.Mouse0: return 7;
                case KeyCode.R: return 8;
                case KeyCode.E: return 9;
                case KeyCode.F: return 10;
                case KeyCode.Q: return 11;      // 换武器：漏了它 SetKey(Q) 会被静默丢弃，switchTo 在注入/回放路径永不可达
                default: return -1;
            }
        }
    }
}
