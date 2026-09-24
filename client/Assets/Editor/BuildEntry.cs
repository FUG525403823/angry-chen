using System;
using System.IO;
using System.Linq;
using UnityEditor;
using UnityEditor.Build.Reporting;
using UnityEditor.SceneManagement;
using UnityEngine;

namespace Ac.Editor
{
    // C01 §5.3：唯一构建入口，由 client/build.ps1 以 -executeMethod 调用。
    // 参数：-target <名称>（当前只实现 Windows64）、-output <目录或 .exe 路径>（缺省 Build/Windows64，相对本工程根）。
    // 退出码：0 = 出包成功；1 = 构建失败。
    public static class BuildEntry
    {
        // C15 §5：IL2CPP（x64）为发布通道，Mono 仅兜底。Unity 在缺 IL2CPP 变体时会直接抛错，
        // 所以这里只负责"表达意图"并如实记录，实际可用性由调用方（build.ps1）探测。
        private static void ApplyBackend(string requested)
        {
            var wanted = string.IsNullOrEmpty(requested) ? "il2cpp" : requested.ToLowerInvariant();
            var impl = wanted == "mono" ? ScriptingImplementation.Mono2x : ScriptingImplementation.IL2CPP;
            try
            {
                PlayerSettings.SetScriptingBackend(BuildTargetGroup.Standalone, impl);
                Debug.Log("[build] backend=" + impl);
            }
            catch (Exception ex)
            {
                // 缺 win64_il2cpp 变体时退到 Mono，并让这一行出现在构建日志里以便验收核对。
                Debug.LogWarning("[build] backend=" + impl + " unavailable (" + ex.Message + "), falling back to Mono2x");
                PlayerSettings.SetScriptingBackend(BuildTargetGroup.Standalone, ScriptingImplementation.Mono2x);
                Debug.Log("[build] backend=" + ScriptingImplementation.Mono2x);
            }
        }

        private const string Windows64 = "Windows64";
        private const string DefaultOutput = "Build/Windows64";
        private const string ExecutableName = "angry-chen.exe";
        private const string BootSceneFolder = "Assets/__BuildEntryTemp";
        private const string BootScenePath = BootSceneFolder + "/Boot.unity";

        public static void BuildWindows64()
        {
            var projectRoot = Directory.GetParent(Application.dataPath).FullName;
            var target = GetArgument("-target") ?? Windows64;
            var exePath = ResolveExecutablePath(projectRoot, GetArgument("-output") ?? DefaultOutput);
            Debug.Log("[build] unity=" + Application.unityVersion + " target=" + target + " exe=" + exePath);

            if (!string.Equals(target, Windows64, StringComparison.OrdinalIgnoreCase))
            {
                Debug.LogError("[build] 未实现的 -target：" + target);
                EditorApplication.Exit(1);
                return;
            }

            ApplyBackend(GetArgument("-backend"));

            var pipeline = RenderPipelineSetup.Ensure();
            Debug.Log("[build] urp=" + AssetDatabase.GetAssetPath(pipeline));

            var scenes = EditorBuildSettings.scenes.Where(s => s.enabled).Select(s => s.path).ToArray();
            var generatedBootScene = scenes.Length == 0;
            var result = BuildResult.Unknown;
            var errors = 0;
            try
            {
                // C01 尚未交付场景资产，首次构建用一个临时空场景充当引导场景，构建结束即从工程删除。
                if (generatedBootScene) scenes = new[] { CreateBootScene() };
                Directory.CreateDirectory(Path.GetDirectoryName(exePath));
                var report = BuildPipeline.BuildPlayer(new BuildPlayerOptions
                {
                    scenes = scenes,
                    locationPathName = exePath,
                    target = BuildTarget.StandaloneWindows64,
                    options = BuildOptions.None,
                });
                result = report.summary.result;
                errors = report.summary.totalErrors;
            }
            finally
            {
                if (generatedBootScene) AssetDatabase.DeleteAsset(BootSceneFolder);
            }

            if (result == BuildResult.Succeeded && File.Exists(exePath))
            {
                Debug.Log("[build] exe=" + exePath + " bytes=" + new FileInfo(exePath).Length);
                Debug.Log("Build succeeded");
                EditorApplication.Exit(0);
                return;
            }

            Debug.LogError("[build] Build failed result=" + result + " errors=" + errors + " exeExists=" + File.Exists(exePath));
            EditorApplication.Exit(1);
        }

        private static string CreateBootScene()
        {
            // 上一次构建被打断时可能残留同一个临时目录，先清掉再重建。
            if (AssetDatabase.IsValidFolder(BootSceneFolder)) AssetDatabase.DeleteAsset(BootSceneFolder);
            AssetDatabase.CreateFolder("Assets", "__BuildEntryTemp");
            var scene = EditorSceneManager.NewScene(NewSceneSetup.EmptyScene, NewSceneMode.Single);
            EditorSceneManager.SaveScene(scene, BootScenePath);
            return BootScenePath;
        }

        private static string ResolveExecutablePath(string projectRoot, string output)
        {
            var rooted = Path.IsPathRooted(output) ? output : Path.Combine(projectRoot, output);
            return output.EndsWith(".exe", StringComparison.OrdinalIgnoreCase)
                ? Path.GetFullPath(rooted)
                : Path.GetFullPath(Path.Combine(rooted, ExecutableName));
        }

        private static string GetArgument(string name)
        {
            var args = Environment.GetCommandLineArgs();
            for (var i = 0; i < args.Length - 1; i++)
            {
                if (string.Equals(args[i], name, StringComparison.OrdinalIgnoreCase)) return args[i + 1];
            }
            return null;
        }
    }
}
