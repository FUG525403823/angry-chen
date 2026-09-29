using System;
using System.Collections.Generic;
using Ac.Core;
using Ac.Sim;
using UnityEngine;

namespace Ac.Tests
{
    // C02 §5.7 的 fixture 输出格式：通过打印 [fixture] <name> OK，
    // 不一致打印 [fixture] <name> tick=<t> field=<path> expected=<v> actual=<v>，无 fixture 打印 [fixture] none。
    // v2（ADR-010 §8 / fixtures/README.md §7）：比较点是 keyframes[]，tick 取自该关键帧的 `tick` 字段。
    internal static class FixtureSuite
    {
        // 盘上 14 份（README §1 的清单）；低于这个数说明 IsShaped 的判据又把真向量滤掉了。
        private const int ExpectedVectors = 14;
        private const int ExpectedTicks = 5560;
        private const int MinimumKeyframes = 40;

        public static void Register()
        {
            SelfTest.Add("fixtures.loader", LoadsAndCompares);
        }

        private static void LoadsAndCompares()
        {
            var directory = FixtureLoader.DefaultDirectory();

            // 审计 M9：目录为空或被全部判成"不是 fixture"时 CompareAll 返回 0 ⇒ 恒绿，所以先钉住加载本身。
            // v2 的判据是 `version == 2` + `hashChain.length == ticks`；旧判据（ticks 是数组且 ticks[0].expected）
            // 在盘上已无匹配文件 ⇒ 0 份 ⇒ 下面第一条断言就红。
            var vectors = FixtureLoader.Load(directory);
            SelfTest.True(vectors.Count >= ExpectedVectors, "至少加载到 " + ExpectedVectors + " 份 v2 向量", vectors.Count.ToString());

            var ticks = 0;
            var keyframes = 0;
            foreach (var vector in vectors)
            {
                ticks += vector.TickCount;
                keyframes += vector.KeyframeCount;
                SelfTest.True(vector.SchemaVersion == 2, vector.Name + " 必须是 schema 2", vector.SchemaVersion.ToString());
                SelfTest.True(vector.HashChain.Count == vector.TickCount, vector.Name + " 链长必须等于 ticks",
                    vector.HashChain.Count + "/" + vector.TickCount);
                SelfTest.Equal(0, vector.HashChain.Count - vector.TickCount);
            }
            SelfTest.True(ticks >= 5560, "14 份合计 tick 数（5560）", ticks.ToString());
            SelfTest.True(keyframes >= MinimumKeyframes, "至少 " + MinimumKeyframes + " 个关键帧", keyframes.ToString());

            // 形状：链节点十六进制、脚本段单调不重叠、关键帧实体/事件/三流 RNG、快照四键。
            // §7.3 ④ 本批只把 hashChain/snapshot 校验到形状，全量投影就绪后再接链——这条断言就是这个下限。
            foreach (var vector in vectors)
            {
                var problem = FixtureLoader.ShapeProblem(vector);
                SelfTest.True(problem == null, vector.Name + " 形状合法", problem);
            }

            // 自比：关键帧自己当 actual ⇒ 一处不一致都不该有（证明管线真的走过了每一份的每个关键帧）。
            var mismatches = FixtureLoader.CompareAll(directory, (vector, keyframe) => keyframe);
            SelfTest.Equal(0, mismatches);

            // 判别性：每份只把**第一个**关键帧的"实际输出"抹掉，必须恰好报"向量数"个不一致。
            // 旧实现（0 份向量、或比较根恒为 null）在这里恒返回 0，必红。
            var dropped = FixtureLoader.CompareAll(directory, (vector, keyframe) =>
                keyframe.Get("tick").AsInt() == vector.Keyframes[0].Get("tick").AsInt() ? null : keyframe);
            SelfTest.Equal((long)vectors.Count, (long)dropped);

            // 反向探针：扰动一个数字必须被定位成首个差异（路径用 v2 关键帧的形状：pos 是三元组）。
            var expected = MiniJson.Parse("{\"tick\":30,\"entities\":[{\"id\":3,\"kind\":\"player\",\"pos\":[1,0,150]}]," +
                "\"events\":[],\"rngState\":{\"ai\":1,\"spawn\":2,\"fx\":3}}");
            var actual = MiniJson.Parse("{\"tick\":30,\"entities\":[{\"id\":3,\"kind\":\"player\",\"pos\":[1,0,151]}]," +
                "\"events\":[],\"rngState\":{\"ai\":1,\"spawn\":2,\"fx\":3}}");
            var difference = FixtureLoader.FirstDifference(expected, actual);
            SelfTest.True(difference != null, "扰动被检出", "无差异");
            SelfTest.Equal("$.entities[0].pos[2]", difference.Path);
            SelfTest.Equal("150", difference.Expected);
            SelfTest.Equal("151", difference.Actual);

            // 冻结的输出行形状（tick 由调用方给出，v2 里就是关键帧的 tick）。
            var logged = CaptureFixtureLog(delegate
            {
                SelfTest.True(FixtureLoader.Compare("probe", 42, expected, expected), "同一文档逐位相等", "报差异");
                SelfTest.True(!FixtureLoader.Compare("probe-mismatch", 42, expected, actual), "探针报差异", "报通过");
            });
            SelfTest.Equal(2, logged.Count);
            SelfTest.Equal("[fixture] probe OK", logged[0]);
            SelfTest.Equal("[fixture] probe-mismatch tick=42 field=$.entities[0].pos[2] expected=150 actual=151", logged[1]);
        }

        // 只收 [fixture] 开头的行：这段窗口里引擎或别的用例打印什么都不该影响断言。
        private static List<string> CaptureFixtureLog(Action action)
        {
            var lines = new List<string>();
            Application.LogCallback handler = delegate(string condition, string stackTrace, LogType type)
            {
                if (condition.StartsWith("[fixture] ", StringComparison.Ordinal)) lines.Add(condition);
            };
            Application.logMessageReceived += handler;
            try
            {
                action();
            }
            finally
            {
                Application.logMessageReceived -= handler;
            }
            return lines;
        }
    }
}
