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

            // C02 尚无模拟器，用 fixture 自身的 expected 当 actual：验证加载、逐位比较与首个差异定位这条管线。
            // C06 起 actualOf 换成模拟器输出，其余不变。
            var mismatches = FixtureLoader.CompareAll(directory, vector => vector.Root.Get(FixtureLoader.ExpectedKey));
            SelfTest.Equal(0, mismatches);
            SelfTest.Equal(mismatches, FixtureLoader.CompareAll(directory, vector => vector.Root.Get(FixtureLoader.ExpectedKey)));

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
