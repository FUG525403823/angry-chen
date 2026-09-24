using System;
using System.IO;
using Ac.Core;
using Ac.Sim;
using Ac.View;
using UnityEngine;

namespace Ac.Tests
{
    // C07 §7：程序化场地的尺寸、预算、确定性与光照参数。
    public static class ArenaSuite
    {
        public static void Register()
        {
            SelfTest.Add("arena.mesh_budget", ChecksBudget);
            SelfTest.Add("arena.mesh_determinism", ChecksDeterminism);
            SelfTest.Add("arena.fence_barn_values", ChecksFrozenValues);
            SelfTest.Add("arena.colliders", ChecksColliders);
            SelfTest.Add("arena.materials", ChecksMaterials);
            SelfTest.Add("arena.lighting", ChecksLighting);
        }

        private static void ChecksBudget()
        {
            var mesh = new ArenaMesh(ArenaParams.Default());
            var stats = mesh.Build();
            SelfTest.Equal(441, mesh.GroundMesh.vertexCount);
            SelfTest.Equal(800, mesh.GroundMesh.triangles.Length / 3);
            SelfTest.Equal(1514, stats.Vertices);
            SelfTest.Equal(1400, stats.Triangles);
            SelfTest.True(mesh.OuterRingMesh.vertexCount == 1000 && mesh.OuterRingMesh.vertexCount <= ArenaParams.OuterRingVerticesCap, "环带 1000 顶点且 ≤ 1024", mesh.OuterRingMesh.vertexCount.ToString());
            SelfTest.True(stats.Vertices <= 4096, "顶点预算 ≤ 4096", stats.Vertices.ToString());
            SelfTest.True(stats.Triangles <= 3072, "三角形预算 ≤ 3072", stats.Triangles.ToString());
            SelfTest.True(stats.Materials <= 6 && stats.Materials == Materials.MaterialCount, "材质 ≤ 6 且取自材质表", stats.Materials.ToString());
            SelfTest.True(stats.DrawCalls <= 8, "绘制调用预算 ≤ 8", stats.DrawCalls.ToString());
        }

        private static void ChecksDeterminism()
        {
            var first = new ArenaMesh(ArenaParams.Default());
            first.Build();
            var second = new ArenaMesh(ArenaParams.Default());
            second.Build();
            var a = first.GroundMesh.vertices;
            var b = second.GroundMesh.vertices;
            var same = a.Length == b.Length;
            for (var i = 0; i < a.Length && same; i++) same = a[i] == b[i];
            SelfTest.True(same, "地面同种子逐顶点相同", "有差异");
            var ringA = first.OuterRingMesh.vertices;
            var ringB = second.OuterRingMesh.vertices;
            same = ringA.Length == ringB.Length;
            for (var i = 0; i < ringA.Length && same; i++) same = ringA[i] == ringB[i];
            SelfTest.True(same, "环带同种子逐顶点相同", "有差异");

            // 可走区域严格平面；起伏只在 ±40m 以外的环带且幅值 ≤ 1.5m
            var flat = true;
            for (var i = 0; i < a.Length && flat; i++) flat = a[i].y == 0f;
            SelfTest.True(flat, "地面严格 y=0", "有起伏");
            var half = (float)ArenaParams.Default().ArenaHalfSize;
            var amplitude = (float)ArenaParams.Default().OuterRingAmplitudeM;
            var inside = 0;
            var over = 0;
            for (var i = 0; i < ringA.Length; i++)
            {
                var vertex = ringA[i];
                if (Mathf.Max(Mathf.Abs(vertex.x), Mathf.Abs(vertex.z)) < half) inside += 1;
                if (Mathf.Abs(vertex.y) > amplitude) over += 1;
            }
            SelfTest.Equal(0, inside);
            SelfTest.Equal(0, over);
            // 内缘必须贴住 40m（不能留 40–44m 的空洞）
            var nearest = float.MaxValue;
            for (var i = 0; i < ringA.Length; i++)
            {
                var distance = Mathf.Max(Mathf.Abs(ringA[i].x), Mathf.Abs(ringA[i].z));
                if (distance < nearest) nearest = distance;
            }
            SelfTest.True(nearest == half, "环带内缘正好 40m", nearest.ToString("R"));
        }

        private static void ChecksFrozenValues()
        {
            var arena = ArenaParams.Default();
            var move = MoveConfig.Default();
            SelfTest.True(arena.ArenaHalfSize == 40.0 && arena.GridCellM == 4.0, "80m×80m / 4m 单元", arena.ArenaHalfSize.ToString("R"));
            SelfTest.True(arena.FenceHeightM == 3.0 && arena.FenceThicknessM == 0.5, "围栏 3.0×0.5", arena.FenceThicknessM.ToString("R"));
            SelfTest.True(arena.FenceInnerM == 39.75, "围栏内表面 39.75m", arena.FenceInnerM.ToString("R"));
            SelfTest.True(arena.WalkableLimitM == 39.35, "可走上界 39.35m（与 S06 的 limit 同值）", arena.WalkableLimitM.ToString("R"));
            SelfTest.True(arena.BarnMinX == move.BarnMinX && arena.BarnMaxX == move.BarnMaxX, "谷仓 x 与 S06 同值", arena.BarnMinX.ToString("R"));
            SelfTest.True(arena.BarnMinZ == move.BarnMinZ && arena.BarnMaxZ == move.BarnMaxZ, "谷仓 z 与 S06 同值", arena.BarnMinZ.ToString("R"));
            SelfTest.True(arena.BarnMaxY == move.BarnMaxY && move.BarnMaxY == 5.0, "谷仓高度与 S06 同值", arena.BarnMaxY.ToString("R"));
            SelfTest.True(move.BarnMinX == -4.0 && move.BarnMaxX == 4.0 && move.BarnMinZ == -4.0 && move.BarnMaxZ == 4.0, "S06 谷仓 AABB 六值", move.BarnMinX.ToString("R"));
            SelfTest.True(move.RadiusMeters == 0.4, "玩家半径 0.4（S06）", move.RadiusMeters.ToString("R"));
        }

        private static void ChecksColliders()
        {
            var set = ArenaColliders.Build();
            SelfTest.Equal(ArenaColliders.BoxCount, set.Boxes.Length);
            SelfTest.Equal(ArenaColliders.PlayerSpawnCount, set.PlayerSpawns.Length);
            SelfTest.Equal(ArenaColliders.SpawnPointCount, set.SpawnPoints.Length);
            var inner = set.FenceInnerM;
            SelfTest.True(set.Boxes[0].MaxZ == -inner && set.Boxes[1].MinZ == inner, "围栏南北内表面 = 39.75", set.Boxes[0].MaxZ.ToString("R"));
            SelfTest.True(set.Boxes[2].MaxX == -inner && set.Boxes[3].MinX == inner, "围栏东西内表面 = 39.75", set.Boxes[2].MaxX.ToString("R"));
            SelfTest.True(set.Boxes[0].MaxY == 3.0 && set.Boxes[3].MinY == 0.0, "围栏碰撞盒 0..3.0", set.Boxes[0].MaxY.ToString("R"));
            var barn = set.Boxes[4];
            SelfTest.True(barn.MinX == -4.0 && barn.MaxX == 4.0 && barn.MinZ == -4.0 && barn.MaxZ == 4.0 && barn.MinY == 0.0 && barn.MaxY == 5.0, "谷仓碰撞盒六值与 S06 相同", barn.MaxX.ToString("R"));
            SelfTest.True(set.PlayerSpawns[0].x == -4.5f && set.PlayerSpawns[0].y == 7f && set.PlayerSpawns[3].x == 4.5f, "4 个玩家出生点 (-4.5..4.5, 7)", set.PlayerSpawns[3].x.ToString("R"));

            var minimum = double.MaxValue;
            for (var i = 0; i < set.SpawnPoints.Length; i++)
            {
                var point = set.SpawnPoints[i];
                var radius = Mathf.Sqrt(point.x * point.x + point.y * point.y);
                SelfTest.True(Mathf.Abs(radius - (float)ArenaColliders.SpawnPointRadiusM) < 0.01f, "生成点半径 38m", radius.ToString("R"));
                for (var j = i + 1; j < set.SpawnPoints.Length; j++)
                {
                    var dx = point.x - set.SpawnPoints[j].x;
                    var dz = point.y - set.SpawnPoints[j].y;
                    var distance = Mathf.Sqrt(dx * dx + dz * dz);
                    if (distance < minimum) minimum = distance;
                }
            }
            SelfTest.True(set.SpawnPoints[0].x == 0f && set.SpawnPoints[0].y == 38f, "生成点含 (0,38)", set.SpawnPoints[0].y.ToString("R"));
            SelfTest.True(set.SpawnPoints[3].x == 38f && set.SpawnPoints[3].y == 0f, "生成点含 (38,0)", set.SpawnPoints[3].x.ToString("R"));
            SelfTest.True(set.SpawnPoints[6].x == 0f && set.SpawnPoints[6].y == -38f, "生成点含 (0,-38)", set.SpawnPoints[6].y.ToString("R"));
            SelfTest.True(set.SpawnPoints[9].x == -38f && set.SpawnPoints[9].y == 0f, "生成点含 (-38,0)", set.SpawnPoints[9].x.ToString("R"));
            SelfTest.True(minimum >= 15.0, "生成点最小间距 ≥ 15m", minimum.ToString("R"));
        }

        private static void ChecksMaterials()
        {
            var palette = ArtPalette.Default();
            SelfTest.Equal(0x6F9C52, (long)palette.Grass.r << 16 | (long)palette.Grass.g << 8 | palette.Grass.b);
            SelfTest.Equal(0x8A7250, (long)palette.Dirt.r << 16 | (long)palette.Dirt.g << 8 | palette.Dirt.b);
            SelfTest.Equal(0xA9835A, (long)palette.Fence.r << 16 | (long)palette.Fence.g << 8 | palette.Fence.b);
            SelfTest.Equal(0xB45C4A, (long)palette.BarnWall.r << 16 | (long)palette.BarnWall.g << 8 | palette.BarnWall.b);
            SelfTest.Equal(0x59413C, (long)palette.BarnRoof.r << 16 | (long)palette.BarnRoof.g << 8 | palette.BarnRoof.b);
            SelfTest.Equal(0xD7C273, (long)palette.Hay.r << 16 | (long)palette.Hay.g << 8 | palette.Hay.b);
            SelfTest.Equal(6, (long)Materials.MaterialCount);
            SelfTest.True(ArenaParams.GrassTileM == 6.0 && ArenaParams.DirtTileM == 4.0 && ArenaParams.WoodTileM == 2.4, "铺贴单元 6/4/2.4m", ArenaParams.WoodTileM.ToString("R"));

            var materials = new Materials();
            materials.Create();
            SelfTest.True(materials.Roughness(0) == 0.92f && materials.Metallic(0) == 0.0f, "草 0.92 / 0", materials.Roughness(0).ToString("R"));
            SelfTest.True(materials.Roughness(1) == 0.95f, "土 0.95", materials.Roughness(1).ToString("R"));
            SelfTest.True(materials.Roughness(2) == 0.85f, "围栏 0.85", materials.Roughness(2).ToString("R"));
            SelfTest.True(materials.Roughness(3) == 0.80f, "谷仓墙 0.80", materials.Roughness(3).ToString("R"));
            SelfTest.True(materials.Roughness(4) == 0.70f && materials.Metallic(4) == 0.05f, "谷仓顶 0.70 / 0.05", materials.Roughness(4).ToString("R"));
            SelfTest.True(materials.Roughness(5) == 0.90f, "干草 0.90", materials.Roughness(5).ToString("R"));

            var textures = materials.CreateTextures();
            SelfTest.Equal(3, textures.Length);
            SelfTest.Equal(256, textures[0].width);
            SelfTest.Equal(256, textures[1].width);
            SelfTest.Equal(128, textures[2].width);
            // 代码生成贴图必须真挂在材质上（§5(c) 的贴图列）
            var table = materials.Create();
            var attached = 0;
            if (table.Grass != null && table.Grass.mainTexture == textures[0]) attached += 1;
            if (table.Dirt != null && table.Dirt.mainTexture == textures[1]) attached += 1;
            if (table.Fence != null && table.Fence.mainTexture == textures[2]) attached += 1;
            SelfTest.True(attached == 3 || table.Grass == null, "三张贴图挂在草/土/木材质上（着色器不可用时跳过）", attached.ToString());
        }

        private static void ChecksLighting()
        {
            var palette = ArtPalette.Default();
            var sunObject = new GameObject("ArenaSun");
            var sun = sunObject.AddComponent<Light>();
            var cameraObject = new GameObject("ArenaCamera");
            var camera = cameraObject.AddComponent<Camera>();
            camera.fieldOfView = 75f;
            LightingRig.Apply(sun, camera);

            SelfTest.Equal((long)LightType.Directional, (long)sun.type);
            var euler = sun.transform.rotation.eulerAngles;
            SelfTest.True(Mathf.Abs(Mathf.DeltaAngle(euler.x, 50f)) < 0.01f, "主光俯角 50°", euler.x.ToString("R"));
            SelfTest.True(Mathf.Abs(Mathf.DeltaAngle(euler.y, -30f)) < 0.01f, "主光方位 -30°", euler.y.ToString("R"));
            SelfTest.True(sun.intensity == 1.1f, "主光强度 1.1", sun.intensity.ToString("R"));
            SelfTest.Equal((long)palette.Sun.r, (long)Mathf.Round(sun.color.r * 255f));
            SelfTest.Equal((long)LightShadows.Soft, (long)sun.shadows);

            SelfTest.True(RenderSettings.fog && RenderSettings.fogMode == FogMode.Linear, "线性雾开启", RenderSettings.fogMode.ToString());
            SelfTest.True(RenderSettings.fogStartDistance == 42f && RenderSettings.fogEndDistance == 138f, "雾 42–138m", RenderSettings.fogStartDistance.ToString("R"));
            SelfTest.True(RenderSettings.ambientIntensity == 0.45f, "环境光强度 0.45", RenderSettings.ambientIntensity.ToString("R"));
            SelfTest.True(RenderSettings.ambientMode == UnityEngine.Rendering.AmbientMode.Flat, "环境光为纯色", RenderSettings.ambientMode.ToString());
            SelfTest.Equal((long)CameraClearFlags.SolidColor, (long)camera.clearFlags);
            SelfTest.True(camera.fieldOfView == 75f, "不改 C05 冻结的 FOV", camera.fieldOfView.ToString("R"));

            UnityEngine.Object.DestroyImmediate(sunObject);
            UnityEngine.Object.DestroyImmediate(cameraObject);

            // §5(d) 的阴影预算落点在 URP 管线资产：URP 运行时只读资产，不读 QualitySettings
            var assetPath = Path.Combine(Application.dataPath, "Settings", "UniversalRenderPipeline.asset");
            SelfTest.True(File.Exists(assetPath), "URP 管线资产存在", assetPath);
            if (!File.Exists(assetPath)) return;
            var text = File.ReadAllText(assetPath);
            SelfTest.True(text.Contains("m_ShadowDistance: 60"), "阴影距离 60m 落在管线资产", "未命中");
            SelfTest.True(text.Contains("m_ShadowCascadeCount: 2"), "级联 2 级落在管线资产", "未命中");
            SelfTest.True(text.Contains("m_SoftShadowsSupported: 1"), "软阴影开启", "未命中");
            SelfTest.True(text.Contains("m_MainLightShadowmapResolution: 2048"), "阴影贴图 2048", "未命中");
        }
    }
}
