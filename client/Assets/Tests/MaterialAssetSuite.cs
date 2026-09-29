using System;
using System.Collections.Generic;
using System.IO;
using Ac.Core;

namespace Ac.Tests
{
    // 运行期材质的着色器来源守卫（ADR-014 §后果-1 的回归门）。
    //
    // 背景：呈现层的 6 个材质原先靠 `Shader.Find("Universal Render Pipeline/Lit")` 按名字取，编译期没有任何
    // 资产引用它 ⇒ 出包时被剥离，player 里 `materialsReady=false`、一张材质都造不出来。正式修法是把着色器
    // 换成**对材质资产的引用**（Ac.View.ArenaMaterials 读 Assets/Resources/*.mat）。本用例盯住这条引用链
    // 的盘上事实：资产在不在、引用的还是不是同一个 URP Lit shader、变体标志对不对。资产被删/被换 shader
    // 时这里立刻红，不必等到出包。
    public static class MaterialAssetSuite
    {
        // 仓库已知的 URP Lit shader guid（与 Resources/FrameBenchUrpLit.mat 逐字相同 —— 那张是 ADR-014
        // 记录在案的"已知可打进包"的资产）
        private const string UrpLitShaderGuid = "933532a4fcc9baf4fa0491de14d08ed7";

        public static void Register()
        {
            SelfTest.Add("assets.material_shader_reference", ChecksShaderReferences);
            SelfTest.Add("assets.material_shader_runtime", ChecksRuntimeResolution);
        }

        // 盘上引用链对了，还要真的**解析得出来**：出包剥离的原始症状就是"盘上有资产、运行期取不到"。
        // 无头（-nographics）下 Resources.Load 与 shader 资产照常可用，所以这条不会因"没有显示设备"而假红。
        private static void ChecksRuntimeResolution()
        {
            SelfTest.True(Ac.View.ArenaMaterials.Shader != null,
                "运行期必须能取到着色器（Resources.Load 资产引用）", "null");
            SelfTest.True(Ac.View.ArenaMaterials.ResolvedFromAsset,
                "着色器必须来自材质资产引用，不能退到按名字查", "退到 Shader.Find 了");
        }

        private static void ChecksShaderReferences()
        {
            var resources = ResourcesDir();
            if (resources == null) { SelfTest.True(false, "找不到 client/Assets/Resources", "目录不存在"); return; }

            // ArenaMaterials 只认这两个资源名：文件名对不上就等于资产"不存在"（Resources.Load 返回 null）
            var litPath = Path.Combine(resources, Ac.View.ArenaMaterials.LitResourcePath + ".mat");
            var instancedPath = Path.Combine(resources, Ac.View.ArenaMaterials.InstancedResourcePath + ".mat");
            SelfTest.True(File.Exists(litPath), "普通材质的持有资产必须在盘上", litPath);
            SelfTest.True(File.Exists(instancedPath), "实例化材质的持有资产必须在盘上", instancedPath);
            if (!File.Exists(litPath) || !File.Exists(instancedPath)) return;

            var litGuid = MetaGuid(litPath + ".meta");
            var instancedGuid = MetaGuid(instancedPath + ".meta");
            SelfTest.True(litGuid != null, "持有资产必须有合法的 32 位 hex guid", litGuid ?? "(空)");
            SelfTest.True(instancedGuid != null, "实例化持有资产必须有合法的 32 位 hex guid", instancedGuid ?? "(空)");
            SelfTest.True(litGuid != instancedGuid, "两张资产的 guid 不能相同", "同一个 guid");

            // 两张资产引用的必须是同一个 URP Lit shader：血缘一断，出包剥离的修复就退回去了
            var litShader = ShaderGuid(litPath);
            var instancedShader = ShaderGuid(instancedPath);
            SelfTest.True(litShader == UrpLitShaderGuid, "普通材质必须引用 URP Lit（" + UrpLitShaderGuid + "）", litShader ?? "(未找到 m_Shader)");
            SelfTest.True(instancedShader == UrpLitShaderGuid, "实例化材质必须引用 URP Lit（" + UrpLitShaderGuid + "）", instancedShader ?? "(未找到 m_Shader)");

            // 实例化绘制（DrawMeshInstanced）要求实例化变体开着；普通材质不该乱开
            SelfTest.True(InstancingEnabled(instancedPath), "实例化材质必须开 m_EnableInstancingVariants", "0");
            SelfTest.True(!InstancingEnabled(litPath), "普通材质不该开 m_EnableInstancingVariants", "1");

            // 血缘：帧基准那张临时补丁与这两张必须同 shader，否则"ADR-014 说 FrameBenchUrpLit 已知可打包"这条
            // 依据就不再适用于生产材质
            var benchPath = Path.Combine(resources, "FrameBenchUrpLit.mat");
            if (File.Exists(benchPath))
            {
                SelfTest.True(ShaderGuid(benchPath) == UrpLitShaderGuid,
                    "FrameBenchUrpLit 也必须引用同一个 URP Lit", ShaderGuid(benchPath) ?? "(未找到 m_Shader)");
            }
        }

        // 两种工作目录都可能出现：编辑器批处理下是工程目录（client/），从仓库根跑就是仓库根。
        private static string ResourcesDir()
        {
            var candidates = new List<string>
            {
                Path.Combine("Assets", "Resources"),
                Path.Combine("client", "Assets", "Resources"),
                Path.Combine("..", "client", "Assets", "Resources"),
            };
            for (var i = 0; i < candidates.Count; i++)
            {
                if (Directory.Exists(candidates[i])) return candidates[i];
            }
            return null;
        }

        private static string MetaGuid(string metaPath)
        {
            if (!File.Exists(metaPath)) return null;
            foreach (var line in File.ReadLines(metaPath))
            {
                if (!line.StartsWith("guid: ", StringComparison.Ordinal)) continue;
                var guid = line.Substring(6).Trim();
                return IsHex32(guid) ? guid : null;
            }
            return null;
        }

        private static string ShaderGuid(string materialPath)
        {
            foreach (var line in File.ReadLines(materialPath))
            {
                var at = line.IndexOf("m_Shader:", StringComparison.Ordinal);
                if (at < 0) continue;
                var guidAt = line.IndexOf("guid: ", at, StringComparison.Ordinal);
                if (guidAt < 0) return null;
                var start = guidAt + 6;
                var end = start;
                while (end < line.Length && line[end] != ',' && line[end] != '}' && line[end] != ' ') end += 1;
                var guid = line.Substring(start, end - start);
                return IsHex32(guid) ? guid : null;
            }
            return null;
        }

        private static bool InstancingEnabled(string materialPath)
        {
            foreach (var line in File.ReadLines(materialPath))
            {
                if (!line.StartsWith("  m_EnableInstancingVariants:", StringComparison.Ordinal)) continue;
                return line.EndsWith("1", StringComparison.Ordinal);
            }
            return false;
        }

        private static bool IsHex32(string text)
        {
            if (text == null || text.Length != 32) return false;
            for (var i = 0; i < text.Length; i++)
            {
                var c = text[i];
                var hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
                if (!hex) return false;
            }
            return true;
        }
    }
}
