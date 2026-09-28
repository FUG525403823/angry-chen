using System;
using System.Collections.Generic;
using System.IO;
using System.Text.RegularExpressions;
using Ac.Core;

namespace Ac.Tests
{
    // B6：上游计划（docs/plans-v2/client/*.md）里点名的测试文件与批处理入口，有一批与最终实现不一致
    // （计划写 *_test.cs 与 XxxTest.Run()，实现统一成 *Suite.cs + SuiteRegistry.RunAll）。
    // 按规矩不改上游计划，改为在这里登记别名表 + 机器守门：计划提到的每个测试文件/入口必须
    // 要么真实存在、要么在表里；表里每条目标必须真实存在，映射的用例前缀必须真的有已注册用例。
    // 明细与理由见 docs/evidence/client-plan-alias.md。
    public static class PlanAliasSuite
    {
        // "计划里的名字|实际文件（相对 client/Assets/Tests）|实际用例前缀（空 = 不是用例分组）"
        private static readonly string[] Aliases =
        {
            "audio_test.cs|AudioSuite.cs|audio.",
            "AudioTest|SuiteRegistry|",
            "lobby_flow_test.cs|LobbySuite.cs|lobby.",
            "LobbyFlowTest|SuiteRegistry|",
            "settings_test.cs|SettingsSuite.cs|settings.",
            "SettingsTest|SuiteRegistry|",
            "arena_mesh_test.cs|ArenaSuite.cs|arena.",
            "interpolation_test.cs|InterpolationSuite.cs|view.",
        };

        // 计划里本来就写对、无需别名的入口类型
        private static readonly string[] KnownEntryPoints = { "SuiteRegistry", "SelfTest", "FrameBench", "CoreCases" };

        public static void Register()
        {
            SelfTest.Add("plan.alias_table_covers_plans", ChecksAliasTable);
        }

        private static string RepoRoot()
        {
            if (Directory.Exists(Path.Combine("docs", "plans-v2", "client"))) return ".";
            if (Directory.Exists(Path.Combine("..", "docs", "plans-v2", "client"))) return "..";
            return null;
        }

        private static void ChecksAliasTable()
        {
            var root = RepoRoot();
            SelfTest.True(root != null, "必须能找到 docs/plans-v2/client", root == null ? "没找到" : root);
            if (root == null) return;

            var planText = new System.Text.StringBuilder();
            foreach (var file in Directory.GetFiles(Path.Combine(root, "docs", "plans-v2", "client"), "*.md"))
            {
                planText.Append(File.ReadAllText(file));
            }
            var text = planText.ToString();

            var aliasByName = new Dictionary<string, string[]>(StringComparer.Ordinal);
            foreach (var entry in Aliases) aliasByName[entry.Split('|')[0]] = entry.Split('|');

            // 1) 计划点名的测试文件要么真实存在，要么在别名表里
            var files = new List<string>();
            foreach (Match m in Regex.Matches(text, @"client/Assets/Tests/([A-Za-z0-9_]+)\.cs"))
            {
                var name = m.Groups[1].Value + ".cs";
                if (files.Contains(name)) continue;
                files.Add(name);
                var exists = File.Exists(Path.Combine(root, "client", "Assets", "Tests", name));
                SelfTest.True(exists || aliasByName.ContainsKey(name), "计划点名的测试文件必须存在或在别名表里", name);
            }
            SelfTest.True(files.Count > 0, "必须从计划里抓到测试文件引用", "抓到 " + files.Count + " 个");

            // 2) 计划点名的批处理入口要么是已知入口，要么在别名表里
            var entries = new List<string>();
            foreach (Match m in Regex.Matches(text, @"Ac\.Tests\.([A-Za-z0-9_]+)\.Run"))
            {
                var name = m.Groups[1].Value;
                if (entries.Contains(name)) continue;
                entries.Add(name);
                var known = false;
                for (var i = 0; i < KnownEntryPoints.Length; i++) if (KnownEntryPoints[i] == name) known = true;
                SelfTest.True(known || aliasByName.ContainsKey(name), "计划点名的入口必须存在或在别名表里", name);
            }
            SelfTest.True(entries.Count > 0, "必须从计划里抓到入口引用", "抓到 " + entries.Count + " 个");

            // 3) 表里每条目标必须真实存在，映射的用例前缀必须真的有已注册用例
            var caseIds = Ac.Core.SelfTest.CaseIds();
            foreach (var pair in aliasByName)
            {
                var target = pair.Value[1];
                var prefix = pair.Value[2];
                if (target.EndsWith(".cs", StringComparison.Ordinal))
                {
                    SelfTest.True(File.Exists(Path.Combine(root, "client", "Assets", "Tests", target)), "别名目标文件必须存在", target);
                }
                else
                {
                    var known = false;
                    for (var i = 0; i < KnownEntryPoints.Length; i++) if (KnownEntryPoints[i] == target) known = true;
                    SelfTest.True(known, "别名目标入口必须是已知入口", target);
                }
                if (prefix.Length == 0) continue;
                var hits = 0;
                for (var i = 0; i < caseIds.Length; i++) if (caseIds[i].StartsWith(prefix, StringComparison.Ordinal)) hits += 1;
                SelfTest.True(hits > 0, "别名映射的用例前缀必须有已注册用例", prefix + " 命中 " + hits);
            }

            // 4) 表本身不许有重复键
            SelfTest.Equal((long)Aliases.Length, (long)aliasByName.Count);
        }
    }
}
