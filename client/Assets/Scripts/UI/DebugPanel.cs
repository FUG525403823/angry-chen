using System.Text;

namespace Ac.UI
{
    // C13 §5 调试面板：F3 切换、250ms 刷新、12 个字段固定顺序、缺数据源显示 n/a、超阈追加 " !"。
    public struct DebugSample
    {
        public int PlayerTick;
        public int ServerTick;
        public bool HasNetwork;
        public float PingMs;
        public float LossPercent;
        public float InboundBytesPerSec;
        public float OutboundBytesPerSec;
        public bool HasFrameTimes;
        public float FrameTimeP95Ms;
        public float FrameTimeMaxMs;
        public bool HasViewCounts;
        public int EntityCount;
        public int PooledCount;
        public bool HasSnapshotRate;
        public float SnapshotRateHz;
        public string VersionLine;
    }

    public sealed class DebugPanel
    {
        public const float RefreshMs = 250f;
        public const int FieldCount = 12;
        public const float PingWarnMs = 120f;
        public const float LossWarnPercent = 5f;
        public const float InboundWarnBytesPerSec = 40960f;
        public const float OutboundWarnBytesPerSec = 8192f;
        public const float P95WarnMs = 20f;
        public const float MaxWarnMs = 33f;
        public const float SnapshotRateWarnHz = 10f;

        private readonly string[] _lines = new string[FieldCount];
        private float _elapsedMs;

        public bool Visible { get; private set; }
        public int RefreshCount { get; private set; }
        public int SampleCount { get; private set; }
        public float ElapsedMs { get { return _elapsedMs; } }

        public void Toggle() { Visible = !Visible; if (Visible) _elapsedMs = RefreshMs; }

        public void Tick(float dtMs, in DebugSample sample)
        {
            if (!Visible) return;                       // 不可见时不采样：别让面板自己拉高帧时间
            _elapsedMs += dtMs;
            if (_elapsedMs < RefreshMs) return;
            _elapsedMs = 0f;
            SampleCount += 1;
            Build(in sample);
            RefreshCount += 1;
        }

        public string[] Lines { get { return _lines; } }
        public string LineAt(int index) { return _lines[index]; }

        public void Build(in DebugSample sample)
        {
            _lines[0] = "playerTick: " + sample.PlayerTick;
            _lines[1] = "serverTick: " + sample.ServerTick;
            _lines[2] = "ping: " + (sample.HasNetwork ? Fmt1(sample.PingMs) + " ms" : "n/a") + Mark(sample.HasNetwork && sample.PingMs > PingWarnMs);
            _lines[3] = "lossPercent: " + (sample.HasNetwork ? Fmt1(sample.LossPercent) + " %" : "n/a") + Mark(sample.HasNetwork && sample.LossPercent > LossWarnPercent);
            _lines[4] = "inboundBytesPerSec: " + (sample.HasNetwork ? ((int)sample.InboundBytesPerSec).ToString() + " B/s" : "n/a") + Mark(sample.HasNetwork && sample.InboundBytesPerSec > InboundWarnBytesPerSec);
            _lines[5] = "outboundBytesPerSec: " + (sample.HasNetwork ? ((int)sample.OutboundBytesPerSec).ToString() + " B/s" : "n/a") + Mark(sample.HasNetwork && sample.OutboundBytesPerSec > OutboundWarnBytesPerSec);
            _lines[6] = "frameTimeP95Ms: " + (sample.HasFrameTimes ? Fmt1(sample.FrameTimeP95Ms) + " ms" : "n/a") + Mark(sample.HasFrameTimes && sample.FrameTimeP95Ms > P95WarnMs);
            _lines[7] = "frameTimeMaxMs: " + (sample.HasFrameTimes ? Fmt1(sample.FrameTimeMaxMs) + " ms" : "n/a") + Mark(sample.HasFrameTimes && sample.FrameTimeMaxMs > MaxWarnMs);
            _lines[8] = "entityCount: " + (sample.HasViewCounts ? sample.EntityCount.ToString() : "n/a");
            _lines[9] = "pooledCount: " + (sample.HasViewCounts ? sample.PooledCount.ToString() : "n/a");
            _lines[10] = "snapshotRateHz: " + (sample.HasSnapshotRate ? Fmt1(sample.SnapshotRateHz) + " /s" : "n/a") + Mark(sample.HasSnapshotRate && sample.SnapshotRateHz < SnapshotRateWarnHz);
            _lines[11] = "versionLine: " + (string.IsNullOrEmpty(sample.VersionLine) ? "n/a" : sample.VersionLine);
        }

        public static string Fmt1(float value) { return value.ToString("0.0", System.Globalization.CultureInfo.InvariantCulture); }

        private static string Mark(bool over) { return over ? " !" : string.Empty; }

        public static string FieldName(int index)
        {
            return FieldNames[index];
        }

        public static readonly string[] FieldNames =
        {
            "playerTick", "serverTick", "ping", "lossPercent", "inboundBytesPerSec", "outboundBytesPerSec",
            "frameTimeP95Ms", "frameTimeMaxMs", "entityCount", "pooledCount", "snapshotRateHz", "versionLine",
        };
    }
}
