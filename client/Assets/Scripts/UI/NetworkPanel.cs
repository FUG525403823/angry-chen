using System.Globalization;
using Ac.Net;

namespace Ac.UI
{
    // C03 §4 任务 7 / §9：网络面板的数据面（视觉部分不属本份）。四行文本供面板与调试命令复用。
    public static class NetworkPanel
    {
        public static string[] BuildLines(NetStatsSnapshot stats)
        {
            return new[]
            {
                "rtt " + stats.RttMs.ToString("F2", CultureInfo.InvariantCulture) + " ms",
                "p99 " + stats.RttP99Ms.ToString("F2", CultureInfo.InvariantCulture) + " ms",
                "丢包 " + stats.PacketLossPermille.ToString(CultureInfo.InvariantCulture) + " / 1000",
                "收/发 " + stats.BytesInPerSec.ToString("F1", CultureInfo.InvariantCulture) + " / " +
                    stats.BytesOutPerSec.ToString("F1", CultureInfo.InvariantCulture) + " B/s",
            };
        }
    }
}
