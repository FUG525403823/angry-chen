using Ac.Net;
using UnityEngine;

namespace Ac.UI
{
    // 事件视图：只带 HUD 需要的字段，避免为每条事件建对象。类型用 Ac.Net.EventType（同值协议码只有一份真源）。
    public struct HudEvent
    {
        public Ac.Net.EventType Type;
        public int HitFlags;      // HIT_FLAG：headshot=1 / downed=2 / killed=4
        public int Wave;
        public int ReviveRatio255;
        public int WaveSize;
    }

    // §5(b)：每帧复用的采样对象。字段与 §5(a) 的元素一一对应。
    public struct HudSample
    {
        public int HpRatio255;         // 血量（0..255）
        public int Mag;                // 弹药（C06 AmmoLedger.mag / S10 单播 mag）
        public int MagSize;            // 该武器的弹匣容量（低弹判定用，步枪 30 / 霰弹 6 / 手枪 12）
        public int Reserve;            // 备弹
        public int ReloadLeft10Ms;     // 换弹环（1/10 ms）
        public int Rage;               // 怒气（满 100）
        public int RageLeft100Ms;      // 狂暴剩余（1/100 ms）
        public int ReviveRatio255;     // 救援进度
        public float NearestAllyDistanceM;   // 2.0m 内可救援
        public int Wave;
        public int IntermissionMs;     // 波间倒计时（权威值，不插值）
        public byte Phase;             // 见 Phase* 常量（S10 MatchPhase）
        public bool Downed;
        public bool RageMode;
        public bool Reloading;
        public bool Charging;
        public bool TargetInSight;     // 准星可命中目标
    }

    // C10 §5(d)：字号四档 + 安全区 + 系统字体回退链；§5(a)/§5(e) 的时长常量也在这里。
    public sealed class Hud
    {
        public const float SafeAreaPercent = 0.04f;
        public const int FontTitlePx = 48;     // 波次横幅
        public const int FontNumericPx = 32;   // 主数值
        public const int FontLabelPx = 18;     // 标签
        public const int FontFeedPx = 16;      // 击杀记录
        public static readonly string[] FontFallback = { "Microsoft YaHei", "Segoe UI", "Arial" };

        // S10 MatchPhase 的权威码（services/../match/phase.hpp：loading=1 / playing=2 / intermission=3 / ended=4）
        public const byte PhaseLobby = 0;
        public const byte PhaseLoading = 1;
        public const byte PhasePlaying = 2;
        public const byte PhaseIntermission = 3;
        public const byte PhaseEnded = 4;

        // 调色板只有一份：Crosshair / AmmoCounter / RageBar / KillFeed 都从这里取
        public const int ColorNormal = 0xE8E8F0;
        public const int ColorTarget = 0xFFD24D;
        public const int ColorHurt = 0xD2402F;
        public const int ColorLowAmmo = 0xD2402F;
        public const int ColorRage = 0x7AD1FF;
        public const int ColorRageFull = 0xFFD24D;

        public const float HurtFlashMs = 150f;         // 与 C09 红闪同值，本层独立计时
        public const float DamageNumberMs = 900f;      // §5(e)：伤害数字
        public const float HpTrailMs = 800f;           // §5(e)：血量尾影
        public const float ChargeWarningMs = 1600f;    // §5(e)：冲锋警戒（由 Charging 驱动）

        private const int HpPrecision = 100;           // 显示精度：血量按 1% 取整
        private const int IntermissionPrecision = 100; // 波间倒计时按 100ms 取整

        private readonly UiThrottle _throttle = new UiThrottle();
        private readonly Crosshair _crosshair = new Crosshair();
        private readonly AmmoCounter _ammo = new AmmoCounter();
        private readonly RageBar _rage = new RageBar();
        private readonly DownedOverlay _downed = new DownedOverlay();
        private readonly RevivePrompt _revive = new RevivePrompt();
        private readonly WaveBanner _banner = new WaveBanner();
        private readonly KillFeed _feed = new KillFeed();

        private int _hpLast = int.MinValue;
        private int _magLast = int.MinValue;
        private int _reserveLast = int.MinValue;
        private int _rageLast = int.MinValue;
        private int _waveLast = int.MinValue;
        private int _intermissionLast = int.MinValue;
        private int _serial;
        private int _hpWritten = int.MinValue;
        private int _magWritten = int.MinValue;
        private int _reserveWritten = int.MinValue;
        private int _rageWritten = int.MinValue;
        private int _waveWritten = int.MinValue;
        private int _intermissionWritten = int.MinValue;

        public UiThrottle Throttle { get { return _throttle; } }
        public Crosshair Crosshair { get { return _crosshair; } }
        public AmmoCounter Ammo { get { return _ammo; } }
        public RageBar Rage { get { return _rage; } }
        public DownedOverlay Downed { get { return _downed; } }
        public RevivePrompt Revive { get { return _revive; } }
        public WaveBanner Banner { get { return _banner; } }
        public KillFeed Feed { get { return _feed; } }

        public float ChargeWarningRemainingMs { get; private set; }

        public static int FontPx(string role)
        {
            if (role == "title") return FontTitlePx;
            if (role == "label") return FontLabelPx;
            if (role == "feed") return FontFeedPx;
            return FontNumericPx;
        }

        public static float SafeAreaInsetPx(int viewportHeightPx) { return viewportHeightPx * SafeAreaPercent; }

        public static bool CombatUiVisible(byte phase) { return phase == PhasePlaying; }
        public static bool CrosshairVisible(in HudSample sample) { return sample.Phase == PhasePlaying && !sample.Downed; }
        public static bool AmmoVisible(in HudSample sample) { return sample.Phase == PhasePlaying && !sample.Downed; }

        // 系统字体：唯一来源。数组重载本身就是回退链语义；批处理下可能取不到，返回 null 是允许的。
        public static Font CreateFont(int sizePx) { return Font.CreateDynamicFontFromOSFont(FontFallback, sizePx); }

        // 数值类走 10Hz 节流 + 按显示精度取整的脏检查；状态类在下面几条路径里立即生效
        public void Apply(in HudSample sample)
        {
            // 脏检查：先按显示精度取整，再逐字段比较；不拼哈希，也不看每帧都在变的原始量化值
            var hp = sample.HpRatio255 * HpPrecision / 255;
            var intermission = sample.IntermissionMs / IntermissionPrecision;
            var changed = hp != _hpLast || sample.Mag != _magLast || sample.Reserve != _reserveLast
                || sample.Rage != _rageLast || sample.Wave != _waveLast || intermission != _intermissionLast;
            // 窗口由 ShouldWrite 管，脏检查已由上面的字段比较做完，所以喂一个每次自增的序号。
            // 关键：只有真的写下去才更新基准值——否则窗口内发生的最后一次变化会被丢掉，
            // 显示会永远停在旧值上（数值类允许慢 100ms，不允许丢）。
            if (changed && _throttle.ShouldWrite(++_serial))
            {
            _hpLast = hp;
            _magLast = sample.Mag;
            _reserveLast = sample.Reserve;
            _rageLast = sample.Rage;
            _waveLast = sample.Wave;
            _intermissionLast = intermission;
                _hpWritten = hp;
                _magWritten = sample.Mag;
                _reserveWritten = sample.Reserve;
                _rageWritten = sample.Rage;
                _waveWritten = sample.Wave;
                _intermissionWritten = sample.IntermissionMs;
            }

            // 状态类：每帧都跟（不建对象、不写文本，只写结构体字段）
            _ammo.Set(sample.Mag, sample.Reserve, sample.ReloadLeft10Ms, sample.MagSize, sample.Reloading);
            _rage.Set(sample.Rage, sample.RageLeft100Ms, sample.RageMode);
            _downed.Update(sample.Downed, sample.Phase);

            var combat = CombatUiVisible(sample.Phase);
            if (combat && CrosshairVisible(sample))
            {
                _crosshair.SetSpread(_crosshair.SpreadDeg);
                _crosshair.SetState(sample.TargetInSight ? CrosshairState.Target : CrosshairState.Normal);
            }
            else
            {
                _crosshair.SetState(CrosshairState.Hidden);
            }

            _revive.SetVisible(combat && !sample.Downed && _revive.CanPrompt(false, sample.NearestAllyDistanceM));
            _revive.SetFromRatio255(sample.ReviveRatio255);

            if (sample.Charging && ChargeWarningRemainingMs <= 0f) ChargeWarningRemainingMs = ChargeWarningMs;
        }

        // §5(b)：状态类事件立即刷新（命中/击杀/波次/救援），不等 100ms 窗口
        public bool PushEvent(in HudEvent hudEvent)
        {
            switch (hudEvent.Type)
            {
                case Ac.Net.EventType.PlayerHit:
                    _crosshair.MarkHurt();
                    if ((hudEvent.HitFlags & 4) != 0) PushKill(hudEvent);
                    _throttle.NoteEventWrite();
                    return true;
                case Ac.Net.EventType.SheepKilled:
                    PushKill(hudEvent);
                    _throttle.NoteEventWrite();
                    return true;
                case Ac.Net.EventType.WaveStart:
                    _banner.ShowWave(hudEvent.Wave);
                    _throttle.NoteEventWrite();
                    return true;
                case Ac.Net.EventType.ReviveProgress:
                    _revive.SetFromRatio255(hudEvent.ReviveRatio255);
                    _revive.SetVisible(hudEvent.ReviveRatio255 > 0);
                    _throttle.NoteEventWrite();
                    return true;
                case Ac.Net.EventType.ReviveDone:
                    _revive.SetVisible(false);
                    _throttle.NoteEventWrite();
                    return true;
                default:
                    return false;
            }
        }

        private void PushKill(in HudEvent hudEvent)
        {
            var entry = default(KillEntry);
            entry.VictimId = hudEvent.Wave;
            entry.Headshot = (hudEvent.HitFlags & 1) != 0;
            _feed.Push(entry);
        }

        public void Tick(float dtMs)
        {
            _throttle.Tick(dtMs);
            _crosshair.Tick(dtMs);
            _banner.Tick(dtMs);
            _feed.Tick(dtMs);
            if (ChargeWarningRemainingMs > 0f)
                ChargeWarningRemainingMs = ChargeWarningRemainingMs > dtMs ? ChargeWarningRemainingMs - dtMs : 0f;
        }

        public int DisplayedHp { get { return _hpWritten; } }
        public int DisplayedMag { get { return _magWritten; } }
        public int DisplayedReserve { get { return _reserveWritten; } }
        public int DisplayedRage { get { return _rageWritten; } }
        public int DisplayedWave { get { return _waveWritten; } }
        public int DisplayedIntermissionMs { get { return _intermissionWritten; } }
    }
}
