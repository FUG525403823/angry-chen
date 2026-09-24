using System;
using System.Collections.Generic;
using Ac.Core;
using Ac.Sim;
using UnityEngine;

namespace Ac.Tests
{
    // C02 §5.7 的 fixture 输出格式：通过打印 [fixture] <name> OK，
    // 不一致打印 [fixture] <name> tick=<t> field=<path> expected=<v> actual=<v>，无 fixture 打印 [fixture] none。
    // tick 只在差异路径落在 $.ticks[i] 上时反推，其余情况用调用方给的上下文 tick。
    internal static class FixtureSuite
    {
        public static void Register()
        {
            SelfTest.Add("fixtures.loader", LoadsAndCompares);
        }

        private static void LoadsAndCompares()
        {
            var directory = FixtureLoader.DefaultDirectory();

            // C02 尚无模拟器，用 fixture 自身的 expected 当 actual：验证加载、逐帧比较与首个差异定位这条管线。
            // C06 起 actualOfTick 换成模拟器输出，其余不变。
            // 审计 M9：目录为空或被全部判成"不是 fixture"时 CompareAll 返回 0 ⇒ 恒绿，所以先钉住加载本身。
            var vectors = FixtureLoader.Load(directory);
            SelfTest.True(vectors.Count >= 4, "至少加载到 4 份 fixture 向量", vectors.Count.ToString());
            var frames = 0;
            foreach (var vector in vectors) frames += vector.TickCount;
            SelfTest.True(frames >= 1000, "四份 fixture 合计帧数（400+400+60+240）", frames.ToString());
            var mismatches = FixtureLoader.CompareAll(directory, (vector, tick) => vector.ExpectedOfTick(tick));
            SelfTest.Equal(0, mismatches);
            // 判别性：每份向量只把第 1 帧的"实际输出"抹掉，必须恰好报"向量数"帧不一致。
            // 旧实现（0 份向量，或顶层 expected 恒为 null）在这里恒返回 0，必红。
            SelfTest.Equal((long)vectors.Count, (long)FixtureLoader.CompareAll(directory, (vector, tick) => tick == 1 ? null : vector.ExpectedOfTick(tick)));

            // 反向探针：扰动一个数字必须被定位成首个差异。
            var expected = MiniJson.Parse("{\"entities\":[{\"id\":3,\"xCm\":150}],\"rngState\":\"0x11223344\"}");
            var actual = MiniJson.Parse("{\"entities\":[{\"id\":3,\"xCm\":151}],\"rngState\":\"0x11223344\"}");
            var difference = FixtureLoader.FirstDifference(expected, actual);
            SelfTest.True(difference != null, "扰动被检出", "无差异");
            SelfTest.Equal("$.entities[0].xCm", difference.Path);
            SelfTest.Equal("150", difference.Expected);
            SelfTest.Equal("151", difference.Actual);

            // $.ticks[i] 上的差异反推出真实 tick 序号；entities/events 的下标不是 tick，用调用方的上下文 tick。
            var ticksExpected = MiniJson.Parse("{\"ticks\":[{\"x\":1},{\"x\":2}],\"expected\":{\"x\":1}}");
            var ticksActual = MiniJson.Parse("{\"ticks\":[{\"x\":1},{\"x\":9}],\"expected\":{\"x\":1}}");
            var ticksDifference = FixtureLoader.FirstDifference(ticksExpected, ticksActual);
            SelfTest.True(ticksDifference != null, "ticks 扰动被检出", "无差异");
            SelfTest.Equal("$.ticks[1].x", ticksDifference.Path);

            var logged = CaptureFixtureLog(delegate
            {
                SelfTest.True(FixtureLoader.Compare("probe", 42, expected, expected), "同一文档逐位相等", "报差异");
                SelfTest.True(!FixtureLoader.Compare("probe-mismatch", 42, expected, actual), "探针报差异", "报通过");
                SelfTest.True(!FixtureLoader.Compare("probe-ticks", 7, ticksExpected, ticksActual), "ticks 路径也报差异", "报通过");
            });
            SelfTest.Equal(3, logged.Count);
            SelfTest.Equal("[fixture] probe OK", logged[0]);
            SelfTest.Equal("[fixture] probe-mismatch tick=42 field=$.entities[0].xCm expected=150 actual=151", logged[1]);
            SelfTest.Equal("[fixture] probe-ticks tick=1 field=$.ticks[1].x expected=2 actual=9", logged[2]);
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
