namespace Ac.UI
{
    // C10 §5(a)/§4 任务 4：弹药 / 备弹 / 换弹环。低弹比按**该武器的**弹匣容量算。
    public sealed class AmmoCounter
    {
        public const float AmmoLowRatio = 0.3f;

        public int Mag { get; private set; }
        public int MagSize { get; private set; }
        public int Reserve { get; private set; }
        public int ReloadLeft10Ms { get; private set; }
        public bool Reloading { get; private set; }

        public void Set(int mag, int reserve, int reloadLeft10Ms, int magSize, bool reloading)
        {
            Mag = mag < 0 ? 0 : mag;
            MagSize = magSize < 1 ? 1 : magSize;
            Reserve = reserve < 0 ? 0 : reserve;
            ReloadLeft10Ms = reloadLeft10Ms < 0 ? 0 : reloadLeft10Ms;
            Reloading = reloading || ReloadLeft10Ms > 0;
        }

        // 用整数比较：0.3f 是 0.30000001192，30 × 0.3f = 9.000000357，会让 9/30 被判成低弹
        public bool IsLow { get { return Mag * 10 < MagSize * 3; } }
        public int Color { get { return IsLow ? Hud.ColorLowAmmo : Hud.ColorNormal; } }
        public float ReloadRingMs { get { return ReloadLeft10Ms / 10f; } }   // 1/10 ms → ms
    }
}
