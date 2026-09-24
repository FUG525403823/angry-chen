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
        public const double MouseRadPerPixel = 0.001;
        public const double MinSensitivity = 0.2;
        public const double MaxSensitivity = 3.0;
        public const int AngleUnitsPerTurn = 65536;               // 与 Quantize.AngleUnits 同值
        public const sbyte AxisFull = 127;                        // 与 Quantize.QuantizeAxis(±1) 同值
        // 待发队列就是 §5.1 的"未确认命令"，上限 MaxBacklog；FIFO，取走方向固定为 _pending[0]。
        private readonly InputIntent[] _pending = new InputIntent[MaxBacklog];
        private int _pendingCount;

        private bool _injected;
        private readonly bool[] _keys = new bool[12];
        private double _mouseDx;
        private double _mouseDy;

        public InputSampler()
        {
            SensitivityValue = 1.0;
            ClientTick = 0;
            Focused = true;
            PitchRad = 0.0;
            YawRad = 0.0;
        }

        public ushort Seq { get; private set; }
        public uint ClientTick { get; private set; }
        public double SensitivityValue { get; private set; }
        public double YawRad { get; private set; }
        public double PitchRad { get; private set; }
        public bool Focused { get; private set; }
        public int CommandsMerged { get; private set; }
        public int PendingCount { get { return _pendingCount; } }
        // §5.6：只有指针锁定时鼠标增量才计入视角（引擎路径；注入路径不受影响）。
        public bool PointerLocked { get; private set; }

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

        // §5.6：失焦立即清空全部按键位与移动轴，并 flush 一条零意图命令。
        public void OnFocusChanged(bool focused)
        {
            Focused = focused;
            if (focused)
            {
                _intentCleared = false;
                return;
            }
            _intentCleared = true;
            PointerLocked = false;
            ClearIntentInternal();
            Enqueue(ZeroIntent());   // §5.6：失焦这条命令必须"零意图"，不能再读引擎 Input
            _sinceLastSendMs = 0.0;
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
            YawRad -= _mouseDx * scale;
            PitchRad -= _mouseDy * scale;
            _mouseDx = 0.0;
            _mouseDy = 0.0;
            if (PitchRad > PitchLimitRad) PitchRad = PitchLimitRad;
            if (PitchRad < -PitchLimitRad) PitchRad = -PitchLimitRad;
            YawRad = WrapRadians(YawRad);
            // §5.1 的 switchTo 槽：Q 的按下沿才翻槽，按住不重复翻。槽值域与 WeaponSlot 对齐（0/1）。
            var switchDown = KeyDown(KeyCode.Q);
            if (switchDown && !_switchWasDown) _switchTo = (byte)(_switchTo == 0 ? 1 : 0);
            _switchWasDown = switchDown;
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
            var buttons = 0;
            if (KeyDown(KeyCode.Mouse0)) buttons |= ButtonFire;
            if (KeyDown(KeyCode.LeftShift) || KeyDown(KeyCode.RightShift)) buttons |= ButtonSprint;
            if (KeyDown(KeyCode.Space)) buttons |= ButtonJump;
            if (KeyDown(KeyCode.R)) buttons |= ButtonReload;
            if (KeyDown(KeyCode.E)) buttons |= ButtonInteract;
            if (KeyDown(KeyCode.F)) buttons |= ButtonRage;
            if (KeyDown(KeyCode.Q)) buttons |= ButtonSwitchWeapon;
            intent.Buttons = (byte)(buttons & 0xff);
            intent.SwitchTo = _switchTo;
            return intent;
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
