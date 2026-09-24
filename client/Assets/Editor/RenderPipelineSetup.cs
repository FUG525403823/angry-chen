using UnityEditor;
using UnityEngine;
using UnityEngine.Rendering;
using UnityEngine.Rendering.Universal;

namespace Ac.Editor
{
    // C01 §5.1 的渲染前置：URP 14 的 ShaderBuildPreprocessor（ShaderBuildPreprocessor.cs:543）在
    // 当前构建目标收集不到任何 URP Asset 时抛 BuildFailedException，直接顶掉出包；
    // 收集来源是 QualitySettings 的逐档覆盖或 GraphicsSettings 的默认管线。
    public static class RenderPipelineSetup
    {
        private const string SettingsFolder = "Assets/Settings";
        private const string PipelineAssetPath = SettingsFolder + "/UniversalRenderPipeline.asset";
        private const string RendererAssetPath = SettingsFolder + "/UniversalRenderer.asset";

        public static void Run()
        {
            var pipeline = Ensure();
            Debug.Log("[urp] pipeline=" + AssetDatabase.GetAssetPath(pipeline));
            EditorApplication.Exit(0);
        }

        // 幂等：已经指派 URP 资产时原样返回，不新建文件。
        public static UniversalRenderPipelineAsset Ensure()
        {
            if (GraphicsSettings.defaultRenderPipeline is UniversalRenderPipelineAsset assigned) return assigned;

            // 已入库的管线资产只重新指派，绝不覆盖（覆盖会把幂等语义写坏）。
            var existing = AssetDatabase.LoadAssetAtPath<UniversalRenderPipelineAsset>(PipelineAssetPath);
            if (existing != null)
            {
                GraphicsSettings.defaultRenderPipeline = existing;
                AssetDatabase.SaveAssets();
                Debug.LogWarning("[urp] 默认管线被清空，已重新指派 " + PipelineAssetPath);
                return existing;
            }

            if (!AssetDatabase.IsValidFolder(SettingsFolder)) AssetDatabase.CreateFolder("Assets", "Settings");
            var renderer = ScriptableObject.CreateInstance<UniversalRendererData>();
            AssetDatabase.CreateAsset(renderer, RendererAssetPath);
            ResourceReloader.ReloadAllNullIn(renderer, UniversalRenderPipelineAsset.packagePath);
            var pipeline = UniversalRenderPipelineAsset.Create(renderer);
            AssetDatabase.CreateAsset(pipeline, PipelineAssetPath);
            GraphicsSettings.defaultRenderPipeline = pipeline;
            AssetDatabase.SaveAssets();
            Debug.LogWarning("[urp] 新建并指派了 URP 资产：" + PipelineAssetPath + "（新增文件需要入库）");
            return pipeline;
        }
    }
}
