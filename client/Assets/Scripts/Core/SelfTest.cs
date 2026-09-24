using System;
using System.Collections.Generic;
using UnityEngine;

namespace Ac.Core
{
    // C02 §5.7 冻结的输出格式（两个自检入口共用）：
    //   SELFTEST START cases=<n>
    //   PASS <caseId>
    //   FAIL <caseId> expected=<e> actual=<a>
    //   SELFTEST OK cases=<n>
    //   SELFTEST FAIL failures=<n> cases=<n>
    public sealed class SelfTestFailure : Exception
    {
        public SelfTestFailure(string message) : base(message) { }
    }

    public static class SelfTest
    {
        private static readonly List<Case> _cases = new List<Case>();

        public static int CaseCount { get { return _cases.Count; } }

        // C01 §9 / C02 §6.2 冻结的入口名：Ac.Core.SelfTest.Run。
        // 只跑 Core 层用例（Ac.Core 的 references 为空，见 CoreCases 的说明）。
        public static void Run()
        {
            Reset();
            CoreCases.Register();
            RunAll(true);
        }

        public static void Reset()
        {
            _cases.Clear();
        }

        public static void Add(string caseId, Action body)
        {
            if (string.IsNullOrEmpty(caseId)) throw new ArgumentException("caseId 不能为空");
            if (body == null) throw new ArgumentNullException("body");
            _cases.Add(new Case(caseId, body));
        }

        // 返回失败数。throwOnFailure 供 -executeMethod 入口用：抛异常让批处理退出码为 1
        // （Ac.Core 不能引 UnityEditor，拿不到 EditorApplication.Exit）。
        public static int RunAll(bool throwOnFailure)
        {
            var total = _cases.Count;
            var failures = 0;
            Debug.Log("SELFTEST START cases=" + total);
            foreach (var testCase in _cases)
            {
                try
                {
                    testCase.Body();
                    Debug.Log("PASS " + testCase.Id);
                }
                catch (Exception error)
                {
                    failures++;
                    Debug.Log("FAIL " + testCase.Id + " " + Describe(error));
                    // 冻结的 FAIL 行只有 expected=/actual= 两个字段，堆栈另起一行（前缀 [selftest] 不属于门的判据）。
                    // 诊断行默认静默：C02 §5.7 的失败行是冻结契约，多打的行只在排查时用环境变量打开。
                if (Environment.GetEnvironmentVariable("AC_SELFTEST_STACK") == "1")
                {
                    Debug.Log("[selftest] stack " + testCase.Id + "\n" + error.StackTrace);
                }
                }
            }

            if (failures == 0)
            {
                Debug.Log("SELFTEST OK cases=" + total);
                return 0;
            }

            Debug.Log("SELFTEST FAIL failures=" + failures + " cases=" + total);
            if (throwOnFailure) throw new SelfTestFailure("自检失败 " + failures + "/" + total);
            return failures;
        }

        public static void Equal(long expected, long actual)
        {
            if (expected != actual) throw new SelfTestFailure("expected=" + expected + " actual=" + actual);
        }

        public static void Equal(string expected, string actual)
        {
            if (!string.Equals(expected, actual, StringComparison.Ordinal))
            {
                throw new SelfTestFailure("expected=" + expected + " actual=" + actual);
            }
        }

        // 位级比较：double 的任何容差都可能掩盖跨语言漂移（ADR-010）。
        public static void BitEqual(double expected, double actual)
        {
            var e = BitConverter.DoubleToInt64Bits(expected);
            var a = BitConverter.DoubleToInt64Bits(actual);
            if (e != a) throw new SelfTestFailure("expected=" + Hex(e) + " actual=" + Hex(a));
        }

        public static void True(bool condition, string expected)
        {
            if (!condition) throw new SelfTestFailure("expected=" + expected + " actual=false");
        }

        public static void True(bool condition, string expected, string actual)
        {
            if (!condition) throw new SelfTestFailure("expected=" + expected + " actual=" + actual);
        }

        public static void Fail(string message)
        {
            throw new SelfTestFailure(message);
        }

        private static string Describe(Exception error)
        {
            // §5.7 只冻结 expected=/actual= 一种形状：用例里抛出的非断言异常也包装成同一形状，
            // 否则 grep 类门禁必须为「另一种 FAIL 行」开特例。
            return error is SelfTestFailure
                ? error.Message
                : "expected=" + error.GetType().Name + " actual=" + error.Message;
        }

        private static string Hex(long value)
        {
            return "0x" + value.ToString("X16");
        }

        private struct Case
        {
            internal string Id { get; }
            internal Action Body { get; }

            internal Case(string id, Action body)
            {
                Id = id;
                Body = body;
            }
        }
    }
}
