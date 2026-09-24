using Ac.Core;
using UnityEditor;

namespace Ac.Tests
{
    // C01 §5.3 与 C02 §8 的分组自检入口：注册 C01 与 C02 的非 Core 层用例分组，
    // 输出格式由 Ac.Core.SelfTest 冻结（C02 §5.7）；Core 层用例只能由 Ac.Core.SelfTest.Run 跑（Ac.Core 无引用）。
    // 有失败时返回退出码 1；Ac.Core.SelfTest.Run 那边因为不能引 UnityEditor，靠抛异常表达失败。
    public static class SuiteRegistry
    {
        public static void RunAll()
        {
            SelfTest.Reset();
            ContractSuite.Register();
            RngSuite.Register();
            QuantizeSuite.Register();
            CodecSuite.Register();
            FixtureSuite.Register();
            var failures = SelfTest.RunAll(false);
            EditorApplication.Exit(failures == 0 ? 0 : 1);
        }
    }
}
