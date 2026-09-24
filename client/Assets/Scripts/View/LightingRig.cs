using UnityEngine;
using UnityEngine.Rendering;

namespace Ac.View
{
    // C07 §5(d)：光照与阴影预算。只按冻结值设置全局渲染状态与传入的光源/相机，不改相机自身参数。
    public static class LightingRig
    {
        public const double SunPitchDeg = 50.0;
        public const double SunYawDeg = -30.0;
        public const float SunIntensity = 1.1f;
        public const float AmbientIntensity = 0.45f;
        public const float FogStartM = 42f;
        public const float FogEndM = 138f;
        public const float ShadowDistanceM = 60f;
        public const int ShadowResolutionPixels = 2048;
        public const int ShadowCascades = 2;
        public const int RealtimeLightCount = 1;   // §5(d)：只有一盏方向光，无点光/聚光
        public const float Cascade2Split = 0.25f;

        // §9 冻结接口：三段合一。三段各自独立可测，用例只碰对象与场景态，
        // 不碰 QualitySettings（它是全局态，在编辑器里会被序列化回 ProjectSettings）。
        public static void Apply(Light sun, Camera camera)
        {
            ApplySun(sun);
            ApplyEnvironment(camera);
        }

        public static void ApplySun(Light sun)
        {
            var palette = ArtPalette.Default();
            if (sun != null)
            {
                sun.type = LightType.Directional;
                sun.transform.rotation = Quaternion.Euler((float)SunPitchDeg, (float)SunYawDeg, 0f);
                sun.intensity = SunIntensity;
                sun.color = palette.Sun;
                sun.shadows = LightShadows.Soft;
            }
        }

        public static void ApplyEnvironment(Camera camera)
        {
            var palette = ArtPalette.Default();
            RenderSettings.ambientMode = AmbientMode.Flat;
            RenderSettings.ambientLight = palette.Ambient;
            RenderSettings.ambientIntensity = AmbientIntensity;
            RenderSettings.fog = true;
            RenderSettings.fogMode = FogMode.Linear;
            RenderSettings.fogStartDistance = FogStartM;
            RenderSettings.fogEndDistance = FogEndM;
            RenderSettings.fogColor = palette.Ambient;

            if (camera == null) return;
            camera.clearFlags = CameraClearFlags.SolidColor;
            camera.backgroundColor = palette.Ambient;
            // §5(d)：FOV/近远裁沿用 C05 的冻结值，这里不动
        }

        // 阴影预算（60m / 2 级级联 / 软阴影 / 2048）不在这里设：URP 运行时只读管线资产，
        // 不读 QualitySettings。冻结落点是 client/Assets/Settings/UniversalRenderPipeline.asset，
        // 由 arena.lighting 用例直接断言该资产的三行值。
    }
}
