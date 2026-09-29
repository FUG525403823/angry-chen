using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using Ac.Core;
using UnityEngine;

namespace Ac.Sim
{
    // v2 向量（docs/evidence/fixtures/README.md §7.2）：存"怎么跑"而不是"跑出来的每一帧"。
    //   name, version, seed, dtMs, configHash, ticks,
    //   setup{players[],sheep[]}, director{startWave},
    //   script[{from,to,commands[]}], keyframes[{tick,entities[],events[],rngState}],
    //   snapshot[{tick,records,encodeHash,decodeHash}], hashChain[每 tick 一个 16 位十六进制]
    public sealed class FixtureVector
    {
        internal FixtureVector(string name, string path, JsonValue root)
        {
            Name = name;
            Path = path;
            Root = root;
            var seed = FixtureLoader.Member(root, "seed");
            Seed = seed == null ? 0 : seed.AsInt();
            var hash = FixtureLoader.Member(root, "configHash");
            ConfigHash = hash == null ? string.Empty : hash.AsString();
        }

        public string Name { get; private set; }
        public string Path { get; private set; }
        public JsonValue Root { get; private set; }
        public int Seed { get; private set; }
        public string ConfigHash { get; private set; }

        // v2：ticks 是**数字**（tick 数），不再是每帧一个 expected 的数组。
        public int TickCount
        {
            get { var ticks = FixtureLoader.Member(Root, FixtureLoader.TicksKey); return ticks == null ? 0 : (int)ticks.AsLong(); }
        }

        public int SchemaVersion
        {
            get { var version = FixtureLoader.Member(Root, FixtureLoader.VersionKey); return version == null ? 0 : version.AsInt(); }
        }

        public uint DtMs
        {
            get { var dtMs = FixtureLoader.Member(Root, "dtMs"); return dtMs == null ? 0u : (uint)dtMs.AsLong(); }
        }

        public JsonValue Setup { get { return FixtureLoader.Member(Root, FixtureLoader.SetupKey); } }
        public JsonValue Director { get { return FixtureLoader.Member(Root, FixtureLoader.DirectorKey); } }

        // RLE 命令脚本：每段 [from,to] 闭区间共用同一批命令；`commands: []` 是"全缺"分支（§4）。
        public JsonValue Script { get { return FixtureLoader.Member(Root, FixtureLoader.ScriptKey); } }
        public JsonValue Keyframes { get { return FixtureLoader.Member(Root, FixtureLoader.KeyframesKey); } }
        public int KeyframeCount { get { var frames = Keyframes; return frames == null ? 0 : frames.Count; } }
        public JsonValue Snapshot { get { return FixtureLoader.Member(Root, FixtureLoader.SnapshotKey); } }

        // 每个 tick 一个链节点（h_i = fnv1a64(投影文本_i, h_{i-1})），长度恒等于 TickCount。
        public JsonValue HashChain { get { return FixtureLoader.Member(Root, FixtureLoader.HashChainKey); } }

        // 覆盖 tick 的那一段的下标；没有被任何段覆盖返回 -1。
        public int SegmentIndexAt(int tick)
        {
            var script = Script;
            if (script == null) return -1;
            for (var i = 0; i < script.Count; i++)
            {
                var segment = script[i];
                var from = FixtureLoader.Member(segment, "from");
                var to = FixtureLoader.Member(segment, "to");
                if (from == null || to == null) return -1;
                if (tick < from.AsInt()) return -1;   // 段按 from 升序 ⇒ 后面更不可能覆盖
                if (tick <= to.AsInt()) return i;
            }
            return -1;
        }

        // 覆盖 tick 的那一段的命令数组（可能是空数组）；没有段覆盖时返回 null。
        // 注意与"段存在但里面没有本机命令"的区别：前者 null，后者是长度为 0 或没有 id 的数组。
        public JsonValue CommandsAt(int tick)
        {
            var index = SegmentIndexAt(tick);
            return index < 0 ? null : FixtureLoader.Member(Script[index], "commands");
        }
    }

    // ADR-010 的对拍 fixture（docs/evidence/fixtures/*.json）加载与逐位比较。
    // 比较按文件名升序、数值比 IEEE754 原始位；目录缺失或没有 fixture 形状的文件时打印 [fixture] none 且不算失败。
    public static class FixtureLoader
    {
        public const string RelativeDirectory = "docs/evidence/fixtures";
        public const int SchemaVersion = 2;
        public const string VersionKey = "version";
        public const string TicksKey = "ticks";
        public const string SetupKey = "setup";
        public const string DirectorKey = "director";
        public const string ScriptKey = "script";
        public const string KeyframesKey = "keyframes";
        public const string SnapshotKey = "snapshot";
        public const string HashChainKey = "hashChain";

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

        // v2 的形状判据：`version == 2`、`ticks` 是正整数、`hashChain.length == ticks`、有非空 `keyframes`。
        // 「有 ticks 数组且 ticks[0].expected」是 v1 的判据，本仓库盘上已无此类文件。
        // 注意：JsonValue.Get 在缺键时**抛异常**（MiniJson.cs:92），而本函数要对同目录下的
        // 非向量产物（trig-table.json 没有 version）判假而不是炸掉整轮自检 ⇒ 一律走 TryGet。
        public static bool IsShaped(JsonValue root)
        {
            if (root == null || root.Kind != JsonKind.Object) return false;
            var version = Member(root, VersionKey);
            if (version == null || version.Kind != JsonKind.Number || version.AsInt() != SchemaVersion) return false;
            var ticks = Member(root, TicksKey);
            if (ticks == null || ticks.Kind != JsonKind.Number || ticks.AsLong() <= 0) return false;
            var chain = Member(root, HashChainKey);
            if (chain == null || chain.Kind != JsonKind.Array || chain.Count != ticks.AsLong()) return false;
            var frames = Member(root, KeyframesKey);
            return frames != null && frames.Kind == JsonKind.Array && frames.Count > 0;
        }

        // 缺键返回 null 的成员读取（JsonValue.Get 会抛异常，形状校验不能靠它）。
        public static JsonValue Member(JsonValue value, string key)
        {
            if (value == null || value.Kind != JsonKind.Object) return null;
            JsonValue found;
            return value.TryGet(key, out found) ? found : null;
        }

        // 逐项形状校验：本批（§7.3 ④）只把 hashChain / snapshot 校验到"长度与十六进制形状"，
        // 全量投影就绪后再接链。返回 null 表示形状没问题，否则是**第一处**问题的描述。
        // 一律走 Member（缺键返回 null）——形状校验要判"不合法"，不能靠会抛异常的 Get。
        public static string ShapeProblem(FixtureVector vector)
        {
            var chain = vector.HashChain;
            for (var i = 0; i < chain.Count; i++)
            {
                var node = chain[i];
                if (node.Kind != JsonKind.String || node.AsString().Length != 16 || !IsHex(node.AsString()))
                    return "hashChain[" + i + "] 必须是 16 位十六进制";
            }

            var script = vector.Script;
            if (script == null || script.Kind != JsonKind.Array) return "script 必须是数组";
            var previousTo = 0;
            for (var i = 0; i < script.Count; i++)
            {
                var segment = script[i];
                var from = Number(segment, "from");
                var to = Number(segment, "to");
                if (from < 0 || to < 0) return "script[" + i + "] 缺 from/to";
                if (from > to) return "script[" + i + "] from > to";
                if (from <= previousTo) return "script[" + i + "] 段必须按 from 升序且不重叠";
                if (to > vector.TickCount) return "script[" + i + "] to 超出 ticks";
                if (Member(segment, "commands") == null) return "script[" + i + "] 缺 commands";
                previousTo = to;
            }

            var frames = vector.Keyframes;
            var previousTick = 0;
            for (var i = 0; i < frames.Count; i++)
            {
                var frame = frames[i];
                var tick = Number(frame, "tick");
                if (tick <= previousTick || tick > vector.TickCount) return "keyframes[" + i + "].tick 非法：" + tick;
                previousTick = tick;
                var entities = Member(frame, "entities");
                if (entities == null || entities.Kind != JsonKind.Array) return "keyframes[" + i + "].entities 必须是数组";
                for (var e = 0; e < entities.Count; e++)
                {
                    var entity = entities[e];
                    if (Member(entity, "id") == null || Member(entity, "kind") == null)
                        return "keyframes[" + i + "].entities[" + e + "] 缺 id/kind";
                    var pos = Member(entity, "pos");
                    if (pos == null || pos.Kind != JsonKind.Array || pos.Count != 3)
                        return "keyframes[" + i + "].entities[" + e + "].pos 必须是三元组";
                }
                var events = Member(frame, "events");
                if (events == null || events.Kind != JsonKind.Array) return "keyframes[" + i + "].events 必须是数组";
                var rng = Member(frame, "rngState");
                if (rng == null || Member(rng, "ai") == null || Member(rng, "spawn") == null || Member(rng, "fx") == null)
                    return "keyframes[" + i + "].rngState 必须有三流";
            }

            var snapshot = vector.Snapshot;
            if (snapshot == null || snapshot.Kind != JsonKind.Array) return "snapshot 必须是数组";
            for (var i = 0; i < snapshot.Count; i++)
            {
                var entry = snapshot[i];
                if (Member(entry, "tick") == null || Member(entry, "records") == null ||
                    Member(entry, "encodeHash") == null || Member(entry, "decodeHash") == null)
                    return "snapshot[" + i + "] 缺 tick/records/encodeHash/decodeHash";
            }
            return null;
        }

        // 缺键返回 -1 的数值读取。
        private static int Number(JsonValue value, string key)
        {
            var member = Member(value, key);
            return member == null || member.Kind != JsonKind.Number ? -1 : member.AsInt();
        }

        private static bool IsHex(string text)
        {
            for (var i = 0; i < text.Length; i++)
            {
                var c = text[i];
                var hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
                if (!hex) return false;
            }
            return true;
        }

        public static Difference FirstDifference(JsonValue expected, JsonValue actual)
        {
            return Walk(expected, actual, "$");
        }

        // tick 由调用方给出（v2 的比较根是关键帧文档，tick 就在文档里）。
        public static bool Compare(string name, int tick, JsonValue expected, JsonValue actual)
        {
            var difference = FirstDifference(expected, actual);
            if (difference == null)
            {
                Debug.Log("[fixture] " + name + " OK");  // §5.7 冻结的通过行
                return true;
            }
            Debug.Log("[fixture] " + name + " tick=" + tick + " field=" + difference.Path +
                " expected=" + difference.Expected + " actual=" + difference.Actual);
            return false;
        }

        // actualOfKeyframe 按**关键帧**给出被测实现的输出文档；返回不一致的关键帧数。
        // 目录为空或没有关键帧时返回 0 —— 调用方**必须**另断言加载到几份向量、共多少个关键帧，
        // 否则 0 个关键帧会被读成"全部一致"（审计 M9）。
        public static int CompareAll(string directory, Func<FixtureVector, JsonValue, JsonValue> actualOfKeyframe)
        {
            var vectors = Load(directory);
            if (vectors.Count == 0)
            {
                Debug.Log("[fixture] none");
                return 0;
            }

            var mismatches = 0;
            var compared = 0;
            foreach (var vector in vectors)
            {
                var frames = vector.Keyframes;
                for (var i = 0; i < frames.Count; i++)
                {
                    var keyframe = frames[i];
                    compared += 1;
                    var actual = actualOfKeyframe(vector, keyframe);
                    if (!Compare(vector.Name, keyframe.Get("tick").AsInt(), keyframe, actual)) mismatches++;
                }
            }
            Debug.Log("[fixture] " + vectors.Count + " 份 / " + compared + " 关键帧");
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
    }
}
