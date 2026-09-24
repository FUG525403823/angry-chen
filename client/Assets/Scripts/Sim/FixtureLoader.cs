using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using Ac.Core;
using UnityEngine;

namespace Ac.Sim
{
    public sealed class FixtureVector
    {
        internal FixtureVector(string name, string path, JsonValue root)
        {
            Name = name;
            Path = path;
            Root = root;
            JsonValue seed;
            Seed = root.TryGet("seed", out seed) ? seed.AsInt() : 0;
            JsonValue hash;
            ConfigHash = root.TryGet("configHash", out hash) ? hash.AsString() : string.Empty;
        }

        public string Name { get; private set; }
        public string Path { get; private set; }
        public JsonValue Root { get; private set; }
        public int Seed { get; private set; }
        public string ConfigHash { get; private set; }

        public int TickCount { get { return Root.Get("ticks").Count; } }
    }

    // ADR-010 的对拍 fixture（docs/evidence/fixtures/*.json）加载与逐位比较。
    // 比较按文件名升序、数值比 IEEE754 原始位；目录缺失或没有 fixture 形状的文件时打印 [fixture] none 且不算失败。
    public static class FixtureLoader
    {
        public const string RelativeDirectory = "docs/evidence/fixtures";
        public const string TicksKey = "ticks";
        public const string ExpectedKey = "expected";

        public sealed class Difference
        {
            public string Path { get; internal set; }
            public string Expected { get; internal set; }
            public string Actual { get; internal set; }
        }

        public static string DefaultDirectory()
        {
            return RepoPaths.Locate(RelativeDirectory);
        }

        public static List<FixtureVector> Load(string directory)
        {
            var vectors = new List<FixtureVector>();
            if (string.IsNullOrEmpty(directory) || !Directory.Exists(directory)) return vectors;

            var files = Directory.GetFiles(directory, "*.json");
            Array.Sort(files, StringComparer.Ordinal);
            foreach (var file in files)
            {
                JsonValue root;
                try
                {
                    root = MiniJson.Parse(File.ReadAllText(file));
                }
                catch (JsonException)
                {
                    continue;  // 同目录下的其它共享产物（如 trig-table.json）不是 fixture
                }
                if (!IsShaped(root)) continue;
                vectors.Add(new FixtureVector(Path.GetFileNameWithoutExtension(file), file, root));
            }
            return vectors;
        }

        public static bool IsShaped(JsonValue root)
        {
            if (root == null || root.Kind != JsonKind.Object) return false;
            JsonValue ignored;
            return root.TryGet(TicksKey, out ignored) && root.TryGet(ExpectedKey, out ignored);
        }

        public static Difference FirstDifference(JsonValue expected, JsonValue actual)
        {
            return Walk(expected, actual, "$");
        }

        public static bool Compare(string name, int tick, JsonValue expected, JsonValue actual)
        {
            var difference = FirstDifference(expected, actual);
            if (difference == null)
            {
                Debug.Log("[fixture] " + name + " OK");  // §5.7 冻结的通过行
                return true;
            }
            Debug.Log("[fixture] " + name + " tick=" + TickOfPath(difference.Path) + " field=" + difference.Path +
                " expected=" + difference.Expected + " actual=" + difference.Actual);
            return false;
        }

        // actualOf 提供被测实现的输出文档；返回不一致的 fixture 数（目录为空时返回 0）。
        public static int CompareAll(string directory, Func<FixtureVector, JsonValue> actualOf)
        {
            var vectors = Load(directory);
            if (vectors.Count == 0)
            {
                Debug.Log("[fixture] none");
                return 0;
            }

            var mismatches = 0;
            foreach (var vector in vectors)
            {
                var expected = vector.Root.Get(ExpectedKey);
                var actual = actualOf(vector);
                if (!Compare(vector.Name, 0, expected, actual)) mismatches++;
            }
            return mismatches;
        }

        private static Difference Walk(JsonValue expected, JsonValue actual, string path)
        {
            if (actual == null || actual.Kind != expected.Kind)
            {
                return Mismatch(path, expected, actual);
            }

            switch (expected.Kind)
            {
                case JsonKind.Null:
                    return null;
                case JsonKind.Bool:
                    return expected.AsBool() == actual.AsBool() ? null : Mismatch(path, expected, actual);
                case JsonKind.Number:
                    return BitConverter.DoubleToInt64Bits(expected.AsDouble()) ==
                        BitConverter.DoubleToInt64Bits(actual.AsDouble())
                        ? null
                        : Mismatch(path, expected, actual);
                case JsonKind.String:
                    return string.Equals(expected.AsString(), actual.AsString(), StringComparison.Ordinal)
                        ? null
                        : Mismatch(path, expected, actual);
                case JsonKind.Array:
                    if (expected.Count != actual.Count) return Mismatch(path, expected, actual);
                    for (var i = 0; i < expected.Count; i++)
                    {
                        var difference = Walk(expected[i], actual[i], path + "[" + i + "]");
                        if (difference != null) return difference;
                    }
                    return null;
                default:
                    if (expected.Count != actual.Count) return Mismatch(path, expected, actual);
                    for (var i = 0; i < expected.Count; i++)
                    {
                        var member = expected.MemberAt(i);
                        JsonValue other;
                        if (!actual.TryGet(member.Key, out other))
                        {
                            return new Difference { Path = path + "." + member.Key, Expected = Describe(member.Value), Actual = "<缺失>" };
                        }
                        var difference = Walk(member.Value, other, path + "." + member.Key);
                        if (difference != null) return difference;
                    }
                    return null;
            }
        }

        private static Difference Mismatch(string path, JsonValue expected, JsonValue actual)
        {
            return new Difference { Path = path, Expected = Describe(expected), Actual = Describe(actual) };
        }

        private static string Describe(JsonValue value)
        {
            if (value == null) return "<缺失>";
            switch (value.Kind)
            {
                case JsonKind.Null: return "null";
                case JsonKind.Bool: return value.AsBool() ? "true" : "false";
                case JsonKind.Number: return value.AsDouble().ToString("R", CultureInfo.InvariantCulture);
                case JsonKind.String: return value.AsString();
                case JsonKind.Array: return "数组(" + value.Count + ")";
                default: return "对象(" + value.Count + ")";
            }
        }

        // 只有落在 $.ticks[i] 上的差异才能反推出 tick 序号；entities / events 的下标是实体与事件序号，
        // 不是 tick，所以那种路径一律退回调用方给的 tick（整篇比较时为 0）。
        private static int TickOfPath(string path)
        {
            var prefix = "$." + TicksKey + "[";
            if (!path.StartsWith(prefix, StringComparison.Ordinal)) return 0;
            var close = path.IndexOf(']', prefix.Length);
            if (close < 0) return 0;
            int index;
            return int.TryParse(path.Substring(prefix.Length, close - prefix.Length), NumberStyles.Integer,
                CultureInfo.InvariantCulture, out index) ? index : 0;
        }
    }
}
