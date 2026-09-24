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

        public bool CanPrompt(bool selfDowned, float nearestAllyDistanceM)
        {
            return !selfDowned && nearestAllyDistanceM <= RangeM;
        }
    }
}
