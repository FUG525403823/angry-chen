using System;
using System.IO;
using Ac.Core;

namespace Ac.Tests
{
    // C15 §5 冻结契约：版本行唯一来源、双端核对规则、日志落盘与轮转。
    internal static class ReleaseSuite
    {
        public static void Register()
        {
            SelfTest.Add("c15.version_line", ChecksVersionLine);
            SelfTest.Add("c15.log_sink", ChecksLogSink);
        }

        private static void ChecksVersionLine()
        {
            SelfTest.True(VersionInfo.SemanticVersion == "0.1.0", "semver 首发 0.1.0", VersionInfo.SemanticVersion);
            SelfTest.Equal(1, (long)VersionInfo.ProtocolVersion);
            var line = VersionInfo.VersionLine;
            SelfTest.True(line.StartsWith("ac-client 0.1.0+", StringComparison.Ordinal), "版本行前缀冻结", line);
            SelfTest.True(line.EndsWith(" proto=1", StringComparison.Ordinal), "版本行 proto 后缀冻结", line);
            SelfTest.True(line == "ac-client 0.1.0+" + VersionInfo.BuildCommit + " proto=1", "版本行由常量拼出", line);

            // 服务器唯一版本行口径（S01/S15 冻结）
            string reason;
            SelfTest.True(VersionInfo.CompatibleWith("ac_server 0.1.0 protocol=1 tick=50ms", out reason), "与服务器首发版本行相容", reason);
            // +<sha7> 与 tick 不参与判定：服务器带 sha 也要认
            SelfTest.True(VersionInfo.CompatibleWith("ac_server 0.1.0+abcdef0 protocol=1 tick=50ms", out reason), "sha7 不参与判定", reason);
            // 协议不等必须拒绝（ADR-009 的 versionMismatch）
            SelfTest.True(!VersionInfo.CompatibleWith("ac_server 0.1.0 protocol=2 tick=50ms", out reason), "protocol 不等必须拒绝", reason);
            SelfTest.True(reason != null && reason.IndexOf("protocol", StringComparison.Ordinal) >= 0, "拒绝原因要指明 protocol", reason);
            // MAJOR.MINOR 不等必须拒绝
            SelfTest.True(!VersionInfo.CompatibleWith("ac_server 0.2.0 protocol=1 tick=50ms", out reason), "MAJOR.MINOR 不等必须拒绝", reason);
            // PATCH 递增不算不兼容
            SelfTest.True(VersionInfo.CompatibleWith("ac_server 0.1.7 protocol=1 tick=50ms", out reason), "PATCH 递增仍相容", reason);
            SelfTest.True(!VersionInfo.CompatibleWith("", out reason), "空版本行必须拒绝", reason);
            SelfTest.True(!VersionInfo.CompatibleWith("ac_server 0.1.0 tick=50ms", out reason), "缺 protocol= 必须拒绝", reason);
        }

        private static void ChecksLogSink()
        {
            var root = Path.Combine(Path.GetTempPath(), "ac-logsink-" + Guid.NewGuid().ToString("N").Substring(0, 8));
            var when = new DateTime(2026, 9, 24, 12, 0, 0, DateTimeKind.Utc);
            try
            {
                var sink = new LogSink(root, 512, delegate { return when; });
                sink.Write("info", "hello");
                var path = Path.Combine(root, "client-20260924.log");
                SelfTest.True(File.Exists(path), "日志按天命名落盘", path);
                var first = File.ReadAllLines(path)[0];
                // §6 第 3 条：日志里的 version 必须与 VersionInfo.VersionLine 相等，否则验收不可复核
                SelfTest.True(first.IndexOf("\"version\":\"" + VersionInfo.VersionLine + "\"", StringComparison.Ordinal) >= 0, "首行注入版本行", first);
                SelfTest.True(first.StartsWith("{\"ts\":\"", StringComparison.Ordinal) && first.EndsWith("}", StringComparison.Ordinal), "JSON 单行", first);

                // 512 B 上限：写够就轮转，且备份份数不超过 RotationKeep
                for (var i = 0; i < 40; i++) sink.Write("info", "pad-" + i);
                SelfTest.True(sink.Rotations >= 1, "超过上限必须轮转", sink.Rotations.ToString());
                SelfTest.True(File.Exists(path + ".1"), "轮转出 .1", "缺 .1");
                SelfTest.True(!File.Exists(path + "." + (LogSink.RotationKeep + 1)), "备份不超过 5 份", "超过 5 份了");

                sink.WriteCrash("line1\nline2");
                var crashes = Directory.GetFiles(root, "crash-*.log");
                SelfTest.Equal(1, (long)crashes.Length);
                var crashLines = File.ReadAllLines(crashes[0]);
                SelfTest.True(crashLines[0] == VersionInfo.VersionLine, "崩溃日志首行是版本行", crashLines[0]);
                SelfTest.True(crashLines.Length <= LogSink.CrashMaxLines + 1, "崩溃日志截断到 200 行", crashLines.Length.ToString());
            }
            finally
            {
                if (Directory.Exists(root)) Directory.Delete(root, true);
            }
        }
    }
}
