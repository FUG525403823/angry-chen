using System;
using System.Collections.Generic;
using System.IO;
using UnityEditor;
using UnityEditor.Build;
using UnityEngine;
using UnityEngine.Rendering;

namespace Ac.Tests
{
    // C01 §5.1/§5.2 冻结契约的自检：工程标识、文本序列化、脚本后端与程序集引用方向。
    internal static class ContractSuite
    {
        private static readonly Dictionary<string, string[]> _allowedReferences = new Dictionary<string, string[]>
        {
            { "Ac.Core", new string[0] },
            { "Ac.Sim", new[] { "Ac.Core" } },
            { "Ac.Net", new[] { "Ac.Core", "Ac.Sim" } },
            { "Ac.View", new[] { "Ac.Core", "Ac.Sim", "Ac.Net" } },
            { "Ac.UI", new[] { "Ac.Core", "Ac.Sim", "Ac.Net", "Ac.View" } },
            { "Ac.Audio", new[] { "Ac.Core" } },
            { "Ac.Editor", new[] { "Ac.Core", "Ac.Sim", "Ac.Net", "Ac.View", "Ac.UI", "Ac.Audio", "Unity.RenderPipelines.Universal.Runtime", "Unity.RenderPipelines.Core.Runtime" } },
            { "Ac.Tests", new[] { "Ac.Core", "Ac.Sim", "Ac.Net", "Ac.View", "Ac.UI", "Ac.Audio" } },
        };

        public static void Register()
        {
            SuiteRegistry.Register("c01.assemblies.references", CheckAssemblyReferences);
            SuiteRegistry.Register("c01.project.identity", CheckIdentity);
            SuiteRegistry.Register("c01.project.serialization", CheckSerialization);
            SuiteRegistry.Register("c01.build.backend", CheckScriptingBackend);
            SuiteRegistry.Register("c01.render.pipeline", CheckRenderPipeline);
        }

        private static void CheckAssemblyReferences()
        {
            var projectRoot = Directory.GetParent(Application.dataPath).FullName;
            var found = new HashSet<string>();
            foreach (var file in Directory.GetFiles(Path.Combine(projectRoot, "Assets"), "*.asmdef", SearchOption.AllDirectories))
            {
                var relative = file.Substring(projectRoot.Length + 1).Replace('\\', '/');
                var definition = JsonUtility.FromJson<AssemblyDefinition>(File.ReadAllText(file));
                Assert(definition != null && !string.IsNullOrEmpty(definition.name), relative + " 不是合法 asmdef（缺 name）");
                Assert(_allowedReferences.ContainsKey(definition.name), relative + " 的程序集名不在 §5.2 表内：" + definition.name);
                var allowed = _allowedReferences[definition.name];
                var references = definition.references ?? new string[0];
                foreach (var reference in references)
                {
                    Assert(Array.IndexOf(allowed, reference) >= 0, relative + " 引用了 §5.2 不允许的程序集：" + reference);
                }
                found.Add(definition.name);
            }

            foreach (var name in _allowedReferences.Keys)
            {
                Assert(found.Contains(name), "缺少程序集定义：" + name);
            }
        }

        private static void CheckIdentity()
        {
            Assert(PlayerSettings.productName == "angry-chen", "productName 应为 angry-chen，实际 " + PlayerSettings.productName);
            Assert(PlayerSettings.companyName == "AngryChen", "companyName 应为 AngryChen，实际 " + PlayerSettings.companyName);
            Assert(PlayerSettings.bundleVersion == "0.1.0", "bundleVersion 应为 0.1.0，实际 " + PlayerSettings.bundleVersion);
        }

        private static void CheckSerialization()
        {
            Assert(EditorSettings.serializationMode == SerializationMode.ForceText, "m_SerializationMode 应为 ForceText（2）");
            Assert(VersionControlSettings.mode == "Visible Meta Files", "m_Mode 应为 Visible Meta Files，实际 " + VersionControlSettings.mode);
        }

        private static void CheckScriptingBackend()
        {
            var backend = PlayerSettings.GetScriptingBackend(NamedBuildTarget.Standalone);
            Assert(backend == ScriptingImplementation.Mono2x, "Standalone 脚本后端应为 Mono2x，实际 " + backend);
        }

        private static void CheckRenderPipeline()
        {
            const string pipelinePath = "Assets/Settings/UniversalRenderPipeline.asset";
            var expected = AssetDatabase.LoadAssetAtPath<RenderPipelineAsset>(pipelinePath);
            Assert(expected != null, "缺少 §5.1 冻结的管线资产：" + pipelinePath);
            Assert(GraphicsSettings.defaultRenderPipeline == expected, "GraphicsSettings 默认管线未指向 " + pipelinePath);
        }

        private static void Assert(bool condition, string message)
        {
            if (!condition) throw new Exception(message);
        }

        [Serializable]
        private class AssemblyDefinition
        {
            public string name;
            public string[] references;
        }
    }
}
