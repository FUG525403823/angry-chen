using System;

namespace Ac.Core
{
    // C15 §5 冻结：客户端版本行的唯一来源（semver + commit + proto）。
    // 服务器对应行：ac_server 0.1.0 protocol=1 tick=50ms（S01/S15 冻结）。
    public static class VersionInfo
    {
        public const string ProductName = "ac-client";
        public const string SemanticVersion = "0.1.0";
        public const int ProtocolVersion = 1;

        public static string BuildCommit { get { return BuildInfo.Commit; } }
        public static string BuildTimeUtc { get { return BuildInfo.BuildTimeUtc; } }

        // ac-client 0.1.0+<sha7> proto=1
        public static string VersionLine
        {
            get { return ProductName + " " + SemanticVersion + "+" + BuildCommit + " proto=" + ProtocolVersion; }
        }

        public static string MajorMinor
        {
            get
            {
                var parts = SemanticVersion.Split('.');
                return parts.Length >= 2 ? parts[0] + "." + parts[1] : SemanticVersion;
            }
        }

        // §5 的核对规则：proto 必须等于服务器行的 protocol，且 MAJOR.MINOR 相等；
        // 客户端 +<sha7> 与服务器 tick 不参与判定。
        public static bool CompatibleWith(string serverLine, out string reason)
        {
            reason = null;
            if (string.IsNullOrEmpty(serverLine)) { reason = "server version line is empty"; return false; }
            var protocol = FieldValue(serverLine, "protocol=");
            if (protocol == null) { reason = "server version line has no protocol= field"; return false; }
            if (protocol != ProtocolVersion.ToString()) { reason = "protocol mismatch: client proto=" + ProtocolVersion + " server protocol=" + protocol; return false; }
            var serverVersion = VersionToken(serverLine);
            if (serverVersion == null) { reason = "server version line has no version token"; return false; }
            var serverMajorMinor = MajorMinorOf(serverVersion);
            if (serverMajorMinor != MajorMinor) { reason = "MAJOR.MINOR mismatch: client " + MajorMinor + " server " + serverMajorMinor; return false; }
            return true;
        }

        // "0.1.0+abc1234" → "0.1.0"
        public static string MajorMinorOf(string versionToken)
        {
            if (string.IsNullOrEmpty(versionToken)) return null;
            var plus = versionToken.IndexOf('+');
            var clean = plus >= 0 ? versionToken.Substring(0, plus) : versionToken;
            var parts = clean.Split('.');
            return parts.Length >= 2 ? parts[0] + "." + parts[1] : clean;
        }

        private static string VersionToken(string line)
        {
            var tokens = line.Split(' ');
            for (var i = 0; i < tokens.Length; i++)
            {
                if (tokens[i].Length > 0 && tokens[i][0] >= '0' && tokens[i][0] <= '9') return tokens[i];
            }
            return null;
        }

        private static string FieldValue(string line, string key)
        {
            var at = line.IndexOf(key, StringComparison.Ordinal);
            if (at < 0) return null;
            var start = at + key.Length;
            var end = start;
            while (end < line.Length && line[end] != ' ') end += 1;
            return line.Substring(start, end - start);
        }
    }
}
