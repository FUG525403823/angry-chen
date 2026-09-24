using System;

namespace Ac.Core
{
    // Core 层自检：C01 §5.2 冻结 Ac.Core 的 references 为空，因此这里只能覆盖受限 JSON 读取器；
    // 量化/解码/随机数三组需要 Ac.Sim/Ac.Net，统一由 Ac.Tests.SuiteRegistry.RunAll 驱动。
    public static class CoreCases
    {
        public static void Register()
        {
            SelfTest.Add("core.json.parse", ParseAcceptsFixtureSubset);
            SelfTest.Add("core.json.reject", ParseRejectsOutOfSubset);
        }

        private static void ParseAcceptsFixtureSubset()
        {
            const string text = @"{""seed"":42,""configHash"":""ab12"",""ticks"":[{""dtMs"":50,""commands"":[{""moveX"":127,""yaw"":16384}]},{""dtMs"":50,""commands"":[]}],""flag"":true,""none"":null,""neg"":-0.5,""exp"":1e3,""esc"":""A\u0041\uD83D\uDE00\n""}";
            var root = MiniJson.Parse(text);

            SelfTest.Equal(8, root.Count);
            SelfTest.Equal("seed", root.MemberAt(0).Key);
            SelfTest.Equal(42, root.Get("seed").AsInt());
            SelfTest.Equal("ab12", root.Get("configHash").AsString());
            SelfTest.Equal(2, root.Get("ticks").Count);
            SelfTest.Equal(50, root.Get("ticks")[0].Get("dtMs").AsInt());
            SelfTest.Equal(127, root.Get("ticks")[0].Get("commands")[0].Get("moveX").AsInt());
            SelfTest.Equal(16384, root.Get("ticks")[0].Get("commands")[0].Get("yaw").AsInt());
            SelfTest.Equal(0, root.Get("ticks")[1].Get("commands").Count);
            SelfTest.True(root.Get("flag").AsBool(), "true");
            SelfTest.True(root.Get("none").Kind == JsonKind.Null, "null");
            SelfTest.BitEqual(-0.5, root.Get("neg").AsDouble());
            SelfTest.BitEqual(1000.0, root.Get("exp").AsDouble());
            SelfTest.Equal(5, root.Get("esc").AsString().Length);
            SelfTest.Equal("A" + (char)0x41 + char.ConvertFromUtf32(0x1F600) + "\n", root.Get("esc").AsString());
        }

        private static void ParseRejectsOutOfSubset()
        {
            Rejects("{/*c*/}");
            Rejects("[1,]");
            Rejects("{\"a\":1} trailing");
            Rejects("\"\\uD83D\"");
            Rejects("\"\\uDE00\"");
            Rejects("01");
            Rejects("\"a\tb\"");
            Rejects("");
            Rejects("[1 2]");
        }

        private static void Rejects(string text)
        {
            try
            {
                MiniJson.Parse(text);
            }
            catch (JsonException)
            {
                return;
            }
            SelfTest.Fail("expected=JsonException actual=解析成功（输入 " + text + "）");
        }
    }
}
