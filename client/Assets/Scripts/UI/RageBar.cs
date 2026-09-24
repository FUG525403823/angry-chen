namespace Ac.UI
{
    // C10 §4 任务 5：怒气条与狂暴倒计时。
    public sealed class RageBar
    {
        public const int RageFull = 100;

        public int Rage { get; private set; }
        public int RageLeft100Ms { get; private set; }
        public bool RageMode { get; private set; }

        public void Set(int rage, int rageLeft100Ms, bool rageMode)
        {
            Rage = rage < 0 ? 0 : (rage > RageFull ? RageFull : rage);
            RageLeft100Ms = rageLeft100Ms < 0 ? 0 : rageLeft100Ms;
            RageMode = rageMode;
        }

        public bool CanActivate { get { return Rage >= RageFull && !RageMode; } }
        public float RageLeftMs { get { return RageLeft100Ms / 100f; } }
        public int Color { get { return CanActivate ? Hud.ColorRageFull : Hud.ColorRage; } }
        public float Fill01 { get { return Rage / (float)RageFull; } }
    }
}
