namespace Ac.UI
{
    // C10 §5(e)：救援提示。进度 = reviveRatio255 / 255（事件步长本来就是 5%，显示不再自行量化）。
    public sealed class RevivePrompt
    {
        public const float RangeM = 2.0f;
        public const float DurationMs = 3000f;
        public const float ProgressStep = 0.05f;

        public bool Visible { get; private set; }
        public float Progress { get; private set; }

        public void SetVisible(bool visible) { Visible = visible; }

        public void SetProgress(float ratio) { Progress = ratio < 0f ? 0f : (ratio > 1f ? 1f : ratio); }

        public void SetFromRatio255(int ratio255)
        {
            var clamped = ratio255 < 0 ? 0 : (ratio255 > 255 ? 255 : ratio255);
            SetProgress(clamped / 255f);
        }

        public float RemainingMs { get { return DurationMs * (1f - Progress); } }

        // 距离 0 表示"这一帧没有可用测量"（默认样本），不是"队友就贴在你脚下"。
        // 不加这条守卫时默认样本会让救援提示**常显**（审计 A4：HudSuite 必须显式写 1.2m 才拿得到"可见"）。
        public bool CanPrompt(bool selfDowned, float nearestAllyDistanceM)
        {
            return !selfDowned && nearestAllyDistanceM > 0f && nearestAllyDistanceM <= RangeM;
        }
    }
}
