namespace Ac.UI
{
    // C10 §5(e)：倒地遮罩。条件 = 自己倒地 且 阶段在 playing。
    public sealed class DownedOverlay
    {
        public bool Visible { get; private set; }

        public void Update(bool downed, byte phase)
        {
            Visible = downed && phase == Hud.PhasePlaying;
        }
    }
}
