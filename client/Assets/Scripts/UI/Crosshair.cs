using Ac.Core;

namespace Ac.UI
{
    public enum CrosshairState : byte { Normal = 0, Target = 1, Hurt = 2, Hidden = 3 }

    // C10 §5(c)：四段十字，尺寸按散布线性映射，颜色取 Hud 的调色板。
    public sealed class Crosshair
    {
        public const int SegmentCount = 4;
        public const float MinSpreadDeg = 0.5f;
        public const float MaxSpreadDeg = 5.0f;
        public const float MinSizePx = 2f;
        public const float MaxSizePx = 24f;

        public float Scale { get; private set; } = SettingsDefaults.CrosshairScale;
        public float ThicknessPx { get; private set; } = SettingsDefaults.CrosshairThickness;
        public float GapPx { get; private set; } = SettingsDefaults.CrosshairGap;
        public bool Dynamic { get; private set; } = SettingsDefaults.CrosshairDynamic;

        public void ApplySettings(in SettingsSnapshot settings)
        {
            Scale = SettingsDefaults.ClampCrosshairScale(settings.CrosshairScale);
            ThicknessPx = SettingsDefaults.ClampCrosshairThickness(settings.CrosshairThickness);
            GapPx = SettingsDefaults.ClampCrosshairGap(settings.CrosshairGap);
            Dynamic = settings.CrosshairDynamic;
            SetPalette(settings.CrosshairColor, settings.ColorblindSafe);
        }

        public float SpreadDeg { get; private set; }
        public int ColorRgb { get; private set; }
        public bool ColorblindSafe { get; private set; }
        public CrosshairState State { get; private set; }
        public float HurtRemainingMs { get; private set; }

        public Crosshair()
        {
            SpreadDeg = MinSpreadDeg;
            State = CrosshairState.Normal;
            ColorRgb = Ac.Core.SettingsDefaults.CrosshairColor;
        }

        // H5：设置里的 crosshairColor / colorblindSafe 此前只被序列化、没有任何行为读者。
        // 色盲安全不改调色板数据，而是在既有调色板里选对比最大的一档。
        public void SetPalette(int colorRgb, bool colorblindSafe)
        {
            ColorblindSafe = colorblindSafe;
            ColorRgb = colorblindSafe ? Ac.Core.SettingsDefaults.MostContrastingCrosshairColor() : colorRgb;
        }

        public void SetSpread(float spreadDeg)
        {
            if (float.IsNaN(spreadDeg)) spreadDeg = MinSpreadDeg;
            SpreadDeg = spreadDeg < MinSpreadDeg ? MinSpreadDeg : (spreadDeg > MaxSpreadDeg ? MaxSpreadDeg : spreadDeg);
        }

        public void SetState(CrosshairState state)
        {
            if (state == CrosshairState.Hurt) HurtRemainingMs = Hud.HurtFlashMs;
            State = state;
        }

        // 隐藏时受伤不复活（§5(c) 倒地/阵亡一律隐藏）
        public void MarkHurt()
        {
            if (State == CrosshairState.Hidden) return;
            SetState(CrosshairState.Hurt);
        }

        // 0.5° → 2px、5° → 24px；越界夹取
        public float SizePx
        {
            get
            {
                if (!Dynamic) return MinSizePx * Scale;
                var t = (SpreadDeg - MinSpreadDeg) / (MaxSpreadDeg - MinSpreadDeg);
                return (MinSizePx + (MaxSizePx - MinSizePx) * t) * Scale;
            }
        }

        public bool Visible { get { return State != CrosshairState.Hidden; } }

        public void Tick(float dtMs)
        {
            if (HurtRemainingMs <= 0f) return;
            HurtRemainingMs = HurtRemainingMs > dtMs ? HurtRemainingMs - dtMs : 0f;
            if (HurtRemainingMs == 0f && State == CrosshairState.Hurt) State = CrosshairState.Normal;
        }

        public int CurrentColor
        {
            get
            {
                if (State == CrosshairState.Target) return Hud.ColorTarget;
                if (State == CrosshairState.Hurt) return Hud.ColorHurt;
                return ColorRgb;
            }
        }
    }
}
