using System;
using System.Collections.Generic;
using Ac.Core;
using Ac.Sim;
using UnityEngine;

namespace Ac.Tests
{
    // C02 §5.7 的 fixture 输出格式：通过打印 [fixture] <name> OK，
    // 不一致打印 [fixture] <name> tick=<t> field=<path> expected=<v> actual=<v>，无 fixture 打印 [fixture] none。
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

            // 反向探针：扰动一个数字必须被定位成首个差异，且两个方向的输出行逐字符合 §5.7。
            var expected = MiniJson.Parse("{\"entities\":[{\"id\":3,\"xCm\":150}],\"rngState\":\"0x11223344\"}");
            var actual = MiniJson.Parse("{\"entities\":[{\"id\":3,\"xCm\":151}],\"rngState\":\"0x11223344\"}");
            var difference = FixtureLoader.FirstDifference(expected, actual);
            SelfTest.True(difference != null, "扰动被检出", "无差异");
            SelfTest.Equal("$.entities[0].xCm", difference.Path);
            SelfTest.Equal("150", difference.Expected);
            SelfTest.Equal("151", difference.Actual);

            var logged = CaptureLog(delegate
            {
                SelfTest.True(FixtureLoader.Compare("probe", 7, expected, expected), "同一文档逐位相等", "报差异");
                SelfTest.True(!FixtureLoader.Compare("probe-mismatch", 0, expected, actual), "探针报差异", "报通过");
            });
            SelfTest.Equal(2, logged.Count);
            SelfTest.Equal("[fixture] probe OK", logged[0]);
            SelfTest.Equal("[fixture] probe-mismatch tick=0 field=$.entities[0].xCm expected=150 actual=151", logged[1]);
        }

        // 冻结的输出行必须逐字可断言，所以在探针里截一段日志出来比对（探针行也照常进日志）。
        private static List<string> CaptureLog(Action action)
        {
            var lines = new List<string>();
            Application.LogCallback handler = delegate(string condition, string stackTrace, LogType type)
            {
                lines.Add(condition);
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
