using System;
using System.Collections.Generic;
using UnityEditor;
using UnityEngine;

namespace Ac.Tests
{
    // C01 §5.3 冻结的分组自检入口：运行各计划注册进来的用例分组。
    // 输出契约：每条用例一行 PASS <分组>.<用例> 或 FAIL <分组>.<用例> <原因>；
    // 全部通过时末行 SELFTEST OK（退出码 0），有失败时末行 SELFTEST FAIL <失败数>（退出码 1）。
    public static class SuiteRegistry
    {
        private static readonly List<Case> _cases = new List<Case>();

        public static void Register(string name, Action body)
        {
            _cases.Add(new Case(name, body));
        }

        public static void RunAll()
        {
            RegisterAll();
            var failed = 0;
            foreach (var testCase in _cases)
            {
                try
                {
                    testCase.Body();
                    Debug.Log("PASS " + testCase.Name);
                }
                catch (Exception error)
                {
                    failed += 1;
                    Debug.Log("FAIL " + testCase.Name + " " + error.Message);
                }
            }

            if (failed == 0)
            {
                Debug.Log("SELFTEST OK");
                EditorApplication.Exit(0);
                return;
            }

            Debug.Log("SELFTEST FAIL " + failed);
            EditorApplication.Exit(1);
        }

        private static void RegisterAll()
        {
            _cases.Clear();
            ContractSuite.Register();
        }

        private struct Case
        {
            internal string Name { get; }
            internal Action Body { get; }

            internal Case(string name, Action body)
            {
                Name = name;
                Body = body;
            }
        }
    }
}
