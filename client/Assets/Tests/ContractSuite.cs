using System;
using System.Collections.Generic;
using System.IO;
using Ac.Core;
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
            SelfTest.Add("c01.assemblies.references", CheckAssemblyReferences);
            SelfTest.Add("c01.project.identity", CheckIdentity);
            SelfTest.Add("c01.project.serialization", CheckSerialization);
            SelfTest.Add("c01.build.backend", CheckScriptingBackend);
            SelfTest.Add("c01.render.pipeline", CheckRenderPipeline);
        }

        private static void CheckAssemblyReferences()
        {
            var projectRoot = Directory.GetParent(Application.dataPath).FullName;
            var found = new HashSet<string>();
            foreach (var file in Directory.GetFiles(Path.Combine(projectRoot, "Assets"), "*.asmdef", SearchOption.AllDirectories))
            {
                var relative = file.Substring(projectRoot.Length + 1).Replace('\\', '/');
                var definition = JsonUtility.FromJson<AssemblyDefinition>(File.ReadAllText(file));
                SelfTest.True(definition != null && !string.IsNullOrEmpty(definition.name), "合法 asmdef（含 name）", relative);
                SelfTest.True(_allowedReferences.ContainsKey(definition.name), "程序集名在 §5.2 表内",
                    definition.name == null ? "<null>" : definition.name);
                var allowed = _allowedReferences[definition.name];
                var references = definition.references ?? new string[0];
                foreach (var reference in references)
                {
                    SelfTest.True(Array.IndexOf(allowed, reference) >= 0, "§5.2 允许的引用", relative + " → " + reference);
                }
                found.Add(definition.name);
            }

            foreach (var name in _allowedReferences.Keys)
            {
                SelfTest.True(found.Contains(name), "程序集定义存在", name);
            }
        }

        private static void CheckIdentity()
        {
            SelfTest.Equal("angry-chen", PlayerSettings.productName);
            SelfTest.Equal("AngryChen", PlayerSettings.companyName);
            SelfTest.Equal("0.1.0", PlayerSettings.bundleVersion);
        }

        private static void CheckSerialization()
        {
            SelfTest.Equal((long)SerializationMode.ForceText, (long)EditorSettings.serializationMode);
            SelfTest.Equal("Visible Meta Files", VersionControlSettings.mode);
        }

        private static void CheckScriptingBackend()
        {
            SelfTest.Equal((long)ScriptingImplementation.Mono2x,
                (long)PlayerSettings.GetScriptingBackend(NamedBuildTarget.Standalone));
        }

        private static void CheckRenderPipeline()
        {
            const string pipelinePath = "Assets/Settings/UniversalRenderPipeline.asset";
            var expected = AssetDatabase.LoadAssetAtPath<RenderPipelineAsset>(pipelinePath);
            SelfTest.True(expected != null, pipelinePath + " 存在", "缺失");
            SelfTest.True(GraphicsSettings.defaultRenderPipeline == expected, "GraphicsSettings 指向 " + pipelinePath,
                "指向其它管线");
        }

        [Serializable]
        private class AssemblyDefinition
        {
            public string name;
            public string[] references;
        }
    }
}
