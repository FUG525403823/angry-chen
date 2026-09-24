using System;
using System.Globalization;
using System.IO;
using System.Text;

namespace Ac.Core
{
    // C15 §5：JSON 单行日志落盘 `logs/client-<yyyyMMdd>.log`，8 MiB 轮转保留 5 份，
    // 未捕获异常另写 `crash-<yyyyMMdd-HHmmss>.log`（前 200 行 + 版本行）。
    // 根目录由调用方给（运行期是 Application.persistentDataPath/logs），因此可以无头测试。
    public sealed class LogSink
    {
        public const long DefaultMaxBytes = 8L * 1024 * 1024;
        public const int RotationKeep = 5;
        public const int CrashMaxLines = 200;

        private readonly string _root;
        private readonly long _maxBytes;
        private readonly Func<DateTime> _nowUtc;
        private string _day;
        private long _bytes;

        public LogSink(string rootDir) : this(rootDir, DefaultMaxBytes, null) { }
        public LogSink(string rootDir, long maxBytes, Func<DateTime> nowUtc)
        {
            _root = rootDir;
            _maxBytes = maxBytes > 0 ? maxBytes : DefaultMaxBytes;
            _nowUtc = nowUtc ?? (delegate { return DateTime.UtcNow; });
        }

        public int WrittenLines { get; private set; }
        public int Rotations { get; private set; }

        public string CurrentPath
        {
            get { return Path.Combine(_root, "client-" + Day + ".log"); }
        }

        public void Write(string level, string message)
        {
            var line = "{\"ts\":\"" + _nowUtc().ToString("yyyy-MM-ddTHH:mm:ssZ", CultureInfo.InvariantCulture)
                + "\",\"level\":\"" + Escape(level) + "\",\"msg\":\"" + Escape(message)
                + "\",\"version\":\"" + Escape(VersionInfo.VersionLine) + "\"}";
            AppendToCurrent(line, true);
            WrittenLines += 1;
        }

        public void WriteCrash(string text)
        {
            EnsureRoot();
            var stamp = _nowUtc().ToString("yyyyMMdd-HHmmss", CultureInfo.InvariantCulture);
            var path = Path.Combine(_root, "crash-" + stamp + ".log");
            var sb = new StringBuilder();
            sb.Append(VersionInfo.VersionLine).Append('\n');
            if (!string.IsNullOrEmpty(text))
            {
                var lines = text.Replace("\r\n", "\n").Split('\n');
                var take = lines.Length < CrashMaxLines ? lines.Length : CrashMaxLines;
                for (var i = 0; i < take; i++) sb.Append(lines[i]).Append('\n');
            }
            File.WriteAllText(path, sb.ToString());
        }

        // 超过上限就轮转：client-<day>.log → .1 → .2 …，保留 RotationKeep 份备份。
        private void AppendToCurrent(string line, bool countBytes)
        {
            EnsureRoot();
            var path = CurrentPath;
            var payload = line + "\n";
            var size = Encoding.UTF8.GetByteCount(payload);
            var existing = File.Exists(path) ? new FileInfo(path).Length : 0;
            if (existing + size > _maxBytes && existing > 0)
            {
                Rotate(path);
                existing = 0;
            }
            File.AppendAllText(path, payload, Encoding.UTF8);
            if (countBytes) _bytes = existing + size;
        }

        private void Rotate(string path)
        {
            var oldest = path + "." + RotationKeep;
            if (File.Exists(oldest)) File.Delete(oldest);
            for (var i = RotationKeep - 1; i >= 1; i--)
            {
                var from = path + "." + i;
                if (File.Exists(from)) File.Move(from, path + "." + (i + 1));
            }
            if (File.Exists(path)) File.Move(path, path + ".1");
            Rotations += 1;
        }

        private string Day { get { return _nowUtc().ToString("yyyyMMdd", CultureInfo.InvariantCulture); } }

        private void EnsureRoot()
        {
            if (!Directory.Exists(_root)) Directory.CreateDirectory(_root);
        }

        private static string Escape(string value)
        {
            if (string.IsNullOrEmpty(value)) return "";
            return value.Replace("\\", "\\\\").Replace("\"", "\\\"").Replace("\n", " ");
        }
    }
}
