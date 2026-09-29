using System;
using System.Collections.Generic;
using System.IO;
using Ac.Core;

namespace Ac.Tests
{
    // B2/B3 的守门用例：ProjectSettings/*.asset 里引用的资产 guid 必须真的存在。
    // 实测背景：GraphicsSettings 曾指向 09b520d1…（URP 管线）与 e42a45fa…（SRP 默认设置），
    // 而实际资产的 guid 是 56 字符 base64 形式，两处引用全部悬空。
    public static class AssetGateSuite
    {
        // 合法的"没有 meta"引用：全零、Unity 内建资源、以及包内脚本（meta 不在 Assets/ 下）。
        private static readonly string[] Benign =
        {
            "00000000000000000000000000000000",
            "0000000000000000f000000000000000",
            "de02f9e1d18f588468e474319d09a723",   // ShaderGraphSettings 的包内 m_Script
            "247994e1f5a72c2419c26a37e9334c01",   // URPProjectSettings（URP 自动生成）的包内 m_Script
        };

        public static void Register()
        {
            SelfTest.Add("assets.guid_references_resolve", ChecksGuidReferences);
        }

        private static string ClientRoot()
        {
            // 编辑器进程的工作目录是工程目录（client/），但单元测试也可能从仓库根跑：两个候选都试。
            if (Directory.Exists(Path.Combine("ProjectSettings"))) return ".";
            if (Directory.Exists(Path.Combine("..", "client", "ProjectSettings"))) return Path.Combine("..", "client");
            return null;
        }

        private static void ChecksGuidReferences()
        {
            var root = ClientRoot();
            if (root == null) { SelfTest.True(false, "找不到客户端工程根", "ProjectSettings 不存在"); return; }

            var known = new HashSet<string>(StringComparer.Ordinal);
            foreach (var meta in Directory.GetFiles(Path.Combine(root, "Assets"), "*.meta", SearchOption.AllDirectories))
            {
                foreach (var line in File.ReadLines(meta))
                {
                    if (!line.StartsWith("guid: ", StringComparison.Ordinal)) continue;
                    known.Add(line.Substring(6).Trim());
                    break;
                }
            }
            SelfTest.True(known.Count > 0, "必须能读到资产 guid", "0 个 meta");

            var settingsDir = Path.Combine(root, "ProjectSettings");
            foreach (var asset in Directory.GetFiles(settingsDir, "*.asset"))
            {
                var name = Path.GetFileName(asset);
                var text = File.ReadAllText(asset);
                var lines = text.Split('\n');
                for (var i = 0; i < lines.Length; i++)
                {
                    var line = lines[i];
                    var at = line.IndexOf("guid: ", StringComparison.Ordinal);
                    if (at < 0) continue;
                    // productGUID 是工程自己的标识，不是资产引用
                    if (line.Contains("productGUID")) continue;
                    var start = at + 6;
                    var end = start;
                    while (end < line.Length && line[end] != ',' && line[end] != '}' && line[end] != ' ') end += 1;
                    var guid = line.Substring(start, end - start);
                    if (guid.Length != 32) continue;                  // 非 32 位十六进制的（本仓库的资产用 base64 形式）不是这类引用
                    var benign = false;
                    for (var b = 0; b < Benign.Length; b++) if (Benign[b] == guid) { benign = true; break; }
                    if (benign) continue;
                    SelfTest.True(known.Contains(guid), name + ":" + (i + 1) + " 的资产引用必须存在", guid);
                }
            }
        }
    }
}
