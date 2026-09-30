using System;
using System.IO;
using Ac.Core;
using Ac.View;
using UnityEngine;

namespace Ac.Tests
{
    // C08 §7：四形几何、实例池、零分配、额标曲线与贴图禁令、13 个状态映射、三级剔除。
    public static class SheepSuite
    {
        public static void Register()
        {
            SelfTest.Add("sheep.geometry", ChecksGeometry);
            SelfTest.Add("sheep.surface_quality", ChecksSurfaceQuality);
            SelfTest.Add("sheep.scale_and_emblem_attachment", ChecksScaleAndEmblemAttachment);
            SelfTest.Add("sheep.pool", ChecksPool);
            SelfTest.Add("sheep.zero_alloc", ChecksZeroAlloc);
            SelfTest.Add("sheep.emblem", ChecksEmblem);
            SelfTest.Add("sheep.state_map", ChecksStateMap);
            SelfTest.Add("sheep.culling", ChecksCulling);
            SelfTest.Add("sheep.pool_reset_multiform", ChecksPoolResetMultiForm);
            SelfTest.Add("sheep.visuals_begin_frame", ChecksVisualsBeginFrame);
        }

        // 审计 A8：下标是 form*256+cursor，Reset 只清连续前缀会留下跨步区间的幽灵实例。
        // 审计 M6：池的"每帧 Reset"契约此前没有任何调用者 —— 同形态累计到 PerFormCapacity(256)
        // 之后 TryAcquire 恒返 -1，羊会静默消失。BeginFrame 必须把游标复位。
        private static void ChecksVisualsBeginFrame()
        {
            var pool = new SheepInstancePool();
            var visuals = new SheepVisuals(pool);
            for (var frame = 0; frame < 3; frame++)
            {
                visuals.BeginFrame();
                for (var i = 0; i < 200; i++) { int index; pool.TryAcquire(0, out index); }
                // 旧实现（BeginFrame 不 Reset）到这里游标会是 200/400/600，第二帧起就红。
                SelfTest.Equal(200, (long)pool.CursorOf(0));
                SelfTest.Equal(200, (long)pool.Count);
            }
            SelfTest.Equal(0, (long)pool.OverflowCount);
            SelfTest.Equal(0, (long)visuals.SkippedCount);
            SelfTest.Equal(0, (long)visuals.CorpseCount);

            // 反证：不逐帧复位时，单形态累计 256 之后必定饱和 —— 这正是 M6 里羊会静默消失的结局。
            var saturated = new SheepInstancePool();
            for (var i = 0; i < 300; i++) { int index; saturated.TryAcquire(0, out index); }
            SelfTest.True(saturated.OverflowCount > 0, "不复位必然饱和", saturated.OverflowCount.ToString());
        }

        private static void ChecksPoolResetMultiForm()
        {
            var pool = new SheepInstancePool();
            SelfTest.Equal(256, (long)SheepInstancePool.PerFormCapacity);
            SelfTest.Equal(1024, (long)SheepInstancePool.EntityCapacity);
            // 第一帧：10 只 grunt + 100 只 elite
            for (var i = 0; i < 10; i++) { int idx; pool.TryAcquire(0, out idx); pool.Instances[idx].Visible = true; }
            for (var i = 0; i < 100; i++) { int idx; pool.TryAcquire(2, out idx); pool.Instances[idx].Visible = true; }
            SelfTest.Equal(110, (long)pool.Count);
            SelfTest.True(pool.Instances[512].Visible && pool.Instances[611].Visible, "elite 落在跨步区间 512..611", "没落进去");
            pool.Reset();
            SelfTest.Equal(0, (long)pool.Count);
            var ghost = 0;
            for (var i = 0; i < SheepInstancePool.EntityCapacity; i++) if (pool.Instances[i].Visible) ghost += 1;
            SelfTest.Equal(0, (long)ghost);
            // 第二帧只有 10 只 grunt：elite 区间的旧记录不许复活
            for (var i = 0; i < 10; i++) { int idx; pool.TryAcquire(0, out idx); pool.Instances[idx].Visible = true; }
            SelfTest.Equal(10, (long)pool.Count);
            var stillGhost = 0;
            for (var i = 256; i < SheepInstancePool.EntityCapacity; i++) if (pool.Instances[i].Visible) stillGhost += 1;
            SelfTest.Equal(0, (long)stillGhost);
        }

        private static void ChecksGeometry()
        {
            var expected = new[]
            {
                new[] { 0.9, 0.50, 1.00, 8.0, 0.22 },
                new[] { 1.0, 0.55, 1.06, 12.0, 0.22 },
                new[] { 1.1, 0.60, 1.12, 12.0, 0.22 },
                new[] { 2.4, 1.60, 1.60, 12.0, 0.36 },
            };
            for (var k = 0; k < SheepMesh.FormCount; k++)
            {
                var form = SheepMesh.Form((SheepKind)k);
                SelfTest.True(form.HeightM == expected[k][0], "羊形高度", form.HeightM.ToString("R"));
                SelfTest.True(form.RadiusM == expected[k][1], "羊形半径", form.RadiusM.ToString("R"));
                SelfTest.True(form.Scale == expected[k][2], "羊形缩放", form.Scale.ToString("R"));
                SelfTest.Equal((long)expected[k][3], (long)form.WoolClusters);
                SelfTest.True(form.EmblemSizeM == expected[k][4], "额标尺寸", form.EmblemSizeM.ToString("R"));
                SelfTest.True(SheepMesh.FormScale((SheepKind)k) == (float)expected[k][2], "FormScale 与表一致", SheepMesh.FormScale((SheepKind)k).ToString("R"));

                var mesh = SheepMesh.Build((SheepKind)k);
                SelfTest.True(mesh.vertexCount <= SheepMesh.VertexBudgetPerForm, "顶点 ≤ 512", mesh.vertexCount.ToString());
                SelfTest.True(mesh.triangles.Length / 3 <= SheepMesh.TriangleBudgetPerForm, "三角形 ≤ 512", (mesh.triangles.Length / 3).ToString());
                SelfTest.Equal(mesh.vertexCount, mesh.colors32.Length);
                var again = SheepMesh.Build((SheepKind)k);
                var same = mesh.vertexCount == again.vertexCount;
                var a = mesh.vertices;
                var b = again.vertices;
                for (var i = 0; i < a.Length && same; i++) same = a[i] == b[i];
                SelfTest.True(same, "同羊形网格可复现", "有差异");
                SelfTest.True(mesh.bounds.size.y > 0f && mesh.bounds.size.y <= (float)form.HeightM * 2f, "包围盒高度合理", mesh.bounds.size.y.ToString("R"));
            }
            SelfTest.True(SheepMesh.Build(SheepKind.Grunt).vertexCount < SheepMesh.Build(SheepKind.Ram).vertexCount, "团数多的羊形顶点更多", "未体现");
            // §5(b)：抖动种子是 EntityId——同 id 同形，不同 id 有差异
            SelfTest.True(SheepMesh.WoolJitter(SheepKind.Grunt, 7) == SheepMesh.WoolJitter(SheepKind.Grunt, 7), "同 id 同抖动", "有差异");
            SelfTest.True(SheepMesh.WoolJitter(SheepKind.Grunt, 7) != SheepMesh.WoolJitter(SheepKind.Grunt, 8), "不同 id 抖动不同", "相同");
            SelfTest.True(SheepMesh.KindOf(9) == SheepKind.King, "越界 kind 收敛到羊王", SheepMesh.KindOf(9).ToString());
        }

        private static void ChecksSurfaceQuality()
        {
            for (var k = 0; k < SheepMesh.FormCount; k++)
            {
                var kind = (SheepKind)k;
                var mesh = SheepMesh.Build(kind);
                try
                {
                    var vertices = mesh.vertices;
                    var normals = mesh.normals;
                    var triangles = mesh.triangles;
                    SelfTest.Equal(vertices.Length, normals.Length);
                    SelfTest.True(Math.Abs(mesh.bounds.min.y) < 0.0001f, "四蹄接地", mesh.bounds.min.y.ToString("R"));
                    var expectedHeight = SheepMesh.Form(kind).HeightM / SheepMesh.Form(kind).Scale;
                    SelfTest.True(Math.Abs(mesh.bounds.size.y - expectedHeight) < 0.001, "网格只含局部尺寸，实例缩放一次", mesh.bounds.size.y.ToString("R"));
                    for (var i = 0; i < vertices.Length; i++)
                    {
                        SelfTest.True(!float.IsNaN(vertices[i].x) && !float.IsInfinity(vertices[i].x) &&
                            !float.IsNaN(vertices[i].y) && !float.IsInfinity(vertices[i].y) &&
                            !float.IsNaN(vertices[i].z) && !float.IsInfinity(vertices[i].z), "坐标有限", i.ToString());
                        SelfTest.True(Math.Abs(normals[i].magnitude - 1f) < 0.001f, "顶点法线归一", i.ToString());
                    }
                    for (var i = 0; i < triangles.Length; i += 3)
                    {
                        var a = triangles[i]; var b = triangles[i + 1]; var c = triangles[i + 2];
                        var face = Vector3.Cross(vertices[b] - vertices[a], vertices[c] - vertices[a]);
                        SelfTest.True(face.sqrMagnitude > 1e-12f, "没有极点退化三角形", i.ToString());
                        SelfTest.True(Vector3.Dot(face, normals[a] + normals[b] + normals[c]) > 0f,
                            "三角绕序与外向法线一致", i.ToString());
                    }
                }
                finally { UnityEngine.Object.DestroyImmediate(mesh); }
            }
        }

        private static void ChecksScaleAndEmblemAttachment()
        {
            var pool = new SheepInstancePool();
            var visuals = new SheepVisuals(pool);
            for (var k = 0; k < SheepMesh.FormCount; k++)
            {
                var kind = (SheepKind)k;
                var view = new EntityView { Id = (ushort)(k + 1), Kind = (byte)k, Visible = true,
                    X = 3.0, Y = 0.0, Z = -2.0, YawRad = 0.7, HpRatio = 1.0 };
                visuals.BeginFrame();
                var index = visuals.Write(view, 0.0);
                var instance = pool.Instances[index];
                var expectedScale = SheepMesh.FormScale(kind) * SheepMesh.WoolJitter(kind, view.Id);
                SelfTest.True(Math.Abs(instance.Transform.MultiplyVector(Vector3.up).magnitude - expectedScale) < 0.0001f,
                    "体型缩放只在实例施加一次", kind.ToString());
                var forehead = instance.Transform.MultiplyPoint3x4(SheepMesh.EmblemAnchor(kind));
                var emblem = instance.EmblemTransform.MultiplyPoint3x4(Vector3.zero);
                SelfTest.True((forehead - emblem).magnitude < 0.0001f, "额标随身体缩放和旋转贴合", kind.ToString());
                SelfTest.True(Math.Abs(instance.EmblemTransform.MultiplyVector(Vector3.up).magnitude - expectedScale) < 0.0001f,
                    "额标同步实例抖动", kind.ToString());
                view.State = SheepVisuals.StateDead;
                visuals.BeginFrame(); visuals.Write(view, 100.0);
                visuals.BeginFrame(); index = visuals.Write(view, 850.0);
                instance = pool.Instances[index];
                forehead = instance.Transform.MultiplyPoint3x4(SheepMesh.EmblemAnchor(kind));
                emblem = instance.EmblemTransform.MultiplyPoint3x4(Vector3.zero);
                SelfTest.True((forehead - emblem).magnitude < 0.0001f, "尸体收缩时额标不漂移", kind.ToString());
            }
        }

        private static void ChecksPool()
        {
            SelfTest.Equal(1024, (long)SheepInstancePool.EntityCapacity);
            SelfTest.Equal(256, (long)SheepInstancePool.PerFormCapacity);
            SelfTest.Equal(1024, SheepInstancePool.EntityCapacity);
            var pool = new SheepInstancePool();
            int index;
            SelfTest.Equal(0, (long)pool.TryAcquire(0, out index));
            SelfTest.Equal(0, index);
            SelfTest.Equal(256, (long)pool.TryAcquire(1, out index));
            SelfTest.Equal(256, index);
            SelfTest.Equal(2, pool.Count);
            for (var i = 0; i < 255; i++) pool.TryAcquire(0, out index);
            SelfTest.Equal(256, pool.CursorOf(0));
            SelfTest.Equal(-1, pool.TryAcquire(0, out index));
            SelfTest.Equal(-1, index);
            SelfTest.Equal(1, pool.OverflowCount);
            SelfTest.Equal(-1, (long)pool.TryAcquire(-1, out index));
            SelfTest.Equal(2, pool.OverflowCount);

            var instances = pool.Instances;
            pool.Reset();
            SelfTest.Equal(0, pool.Count);
            SelfTest.Equal(0, pool.CursorOf(0));
            SelfTest.True(ReferenceEquals(instances, pool.Instances), "Reset 不换实例数组", "换了");
            SelfTest.Equal(1024, pool.Instances.Length);
        }

        private static void ChecksZeroAlloc()
        {
            var pool = new SheepInstancePool();
            var visuals = new SheepVisuals(pool);
            var views = new EntityView[64];
            for (var i = 0; i < views.Length; i++)
            {
                views[i] = new EntityView();   // EntityView 是类
                views[i].Id = (ushort)(i + 1);
                views[i].Kind = (byte)(i % 4);
                views[i].Visible = true;
                views[i].X = i * 0.5;
                views[i].Z = 6.0;
                views[i].HpRatio = 1.0 - 0.01 * i;
                views[i].State = (byte)(i % 13);
            }

            // 测量窗口里不能有任何分配：缓冲先建好
            var indexes = new int[3][];
            for (var frame = 0; frame < 3; frame++) indexes[frame] = new int[views.Length];
            // 预热一整轮同样的调用：第一次调用会 JIT 编译 pool.Reset/visuals.Write，那次编译自身要分配
            // 托管内存（探针实测首轮 200 B，之后每轮 0 B）。稳态的定义不含首次编译。
            pool.Reset();
            for (var i = 0; i < views.Length; i++) visuals.Write(views[i], 0.0);
            var instancesBefore = pool.Instances;
            var before = AllocMeter.Begin();
            for (var frame = 0; frame < 3; frame++)
            {
                pool.Reset();
                for (var i = 0; i < views.Length; i++) indexes[frame][i] = visuals.Write(views[i], frame * 50.0);
            }
            AllocMeter.AssertZero(before);
            SelfTest.True(ReferenceEquals(instancesBefore, pool.Instances), "三帧之后实例数组还是同一个", "换了");
            for (var i = 0; i < views.Length; i++)
            {
                SelfTest.True(indexes[0][i] >= 0 && indexes[0][i] < SheepInstancePool.EntityCapacity, "实例下标落在池内", indexes[0][i].ToString());
                SelfTest.Equal(indexes[0][i], indexes[1][i]);   // 同一可见集合 → 引用集合恒等
                SelfTest.Equal(indexes[0][i], indexes[2][i]);
            }
            SelfTest.True(visuals.SkippedCount == 0, "没有跳过", visuals.SkippedCount.ToString());
            // 连续帧的实例下标恒等：同一可见集合必然落在同一批记录上
            pool.Reset();
            var first = visuals.Write(views[7], 0.0);
            pool.Reset();
            var second = visuals.Write(views[7], 50.0);
            SelfTest.Equal(first, second);
        }

        private static void ChecksEmblem()
        {
            SelfTest.True(SheepVisuals.EmblemIntensity(1.0) == 0.2, "hp=1 → 0.2", SheepVisuals.EmblemIntensity(1.0).ToString("R"));
            SelfTest.True(SheepVisuals.EmblemIntensity(0.0) == 1.0, "hp=0 → 1.0", SheepVisuals.EmblemIntensity(0.0).ToString("R"));
            SelfTest.True(Math.Abs(SheepVisuals.EmblemIntensity(0.5) - 0.6) < 1e-12, "hp=0.5 → 0.6", SheepVisuals.EmblemIntensity(0.5).ToString("R"));
            SelfTest.True(SheepVisuals.EmblemIntensity(1.5) == 0.2 && SheepVisuals.EmblemIntensity(-1.0) == 1.0, "hp 钳到 [0,1]", SheepVisuals.EmblemIntensity(1.5).ToString("R"));
            SelfTest.True(SheepVisuals.EmblemSmoothingPerTick == 0.153518, "平滑步长常量", SheepVisuals.EmblemSmoothingPerTick.ToString("R"));
            SelfTest.True(Math.Abs(SheepVisuals.SmoothEmblem(0.2, 1.0) - (0.2 + 0.8 * 0.153518)) < 1e-12, "一步平滑", SheepVisuals.SmoothEmblem(0.2, 1.0).ToString("R"));

            SelfTest.Equal(0xC5E2ED, Channel(SheepMesh.ColorOf(SheepKind.Elite)));
            SelfTest.Equal(0x7AD1FF, Channel(SheepMesh.EmblemColorPure(SheepKind.Elite)));
            SelfTest.Equal(0xFF8A4C, Channel(SheepMesh.EmblemColorPure(SheepKind.King)));
            SelfTest.Equal(0xEDD4AA, Channel(SheepMesh.ColorOf(SheepKind.King)));

            var emblem = SheepMesh.BuildEmblem(SheepKind.King);
            SelfTest.True(emblem.vertexCount >= 5 + 8 * 4, "额标含描边与外圈顶点", emblem.vertexCount.ToString());
            SelfTest.True(emblem.triangles.Length / 3 >= 4 + 16, "额标含描边与外圈三角形", (emblem.triangles.Length / 3).ToString());
            SelfTest.Equal(emblem.vertexCount, emblem.colors32.Length);
            SelfTest.Equal(0xFF8A4C, Channel(emblem.colors32[0]));
            foreach (var normal in emblem.normals)
                SelfTest.True(normal.z > 0.99f, "额标面朝羊脸前方", normal.ToString());

            // §5(d)/§7：shader 必须是纯文本、不声明 sampler2D、不引用贴图、ZWrite Off
            var path = Path.Combine(Application.dataPath, "Shaders", "Emblem.shader");
            SelfTest.True(File.Exists(path), "Emblem.shader 存在", path);
            if (!File.Exists(path)) return;
            var text = File.ReadAllText(path);
            SelfTest.True(text.IndexOf("sampler2D", StringComparison.Ordinal) < 0, "不声明 sampler2D", "命中");
            SelfTest.True(text.IndexOf("ZWrite Off", StringComparison.Ordinal) >= 0, "ZWrite Off", "未命中");
            SelfTest.True(text.IndexOf("_EmissiveTint", StringComparison.Ordinal) >= 0 && text.IndexOf("_EmissiveIntensity", StringComparison.Ordinal) >= 0, "逐实例自发光属性", "未命中");
            SelfTest.True(text.IndexOf(".png", StringComparison.Ordinal) < 0 && text.IndexOf("Texture2D", StringComparison.Ordinal) < 0, "不引用贴图", "命中");
        }

        private static void ChecksStateMap()
        {
            SelfTest.Equal(13, (long)SheepVisuals.StateCount);
            SelfTest.Equal(13, Enum.GetValues(typeof(SheepAnim)).Length);   // 枚举本身也必须是 13 个
            var names = new[] { "Idle", "Alert", "Run", "Windup", "Charge", "Attack", "Ranged", "Stagger", "Dead", "KingIdle", "KingWalk", "KingRage", "Summon" };
            for (var i = 0; i < SheepVisuals.StateCount; i++)
            {
                var anim = SheepVisuals.MapState((byte)i);
                SelfTest.Equal(i, (long)anim);
                SelfTest.True(anim.ToString() == names[i], "状态码 " + i + " → " + names[i], anim.ToString());
                SelfTest.Equal(i, (long)SheepVisuals.MapAnim((byte)i, 0));
            }
            SelfTest.Equal(32, (long)SheepVisuals.FlagCharging);   // 线上掩码 1<<5，不是 S03 的相对位 3
            SelfTest.Equal(64, (long)SheepVisuals.FlagFading);
            SelfTest.Equal((long)SheepAnim.Charge, (long)SheepVisuals.MapAnim(0, 32));
            SelfTest.Equal((long)SheepAnim.Idle, (long)SheepVisuals.MapAnim(0, 8));      // rageMode 不再被误判成 Charge
            SelfTest.Equal((long)SheepAnim.Idle, (long)SheepVisuals.MapAnim(0, 16));     // reloading 不再被误判成尸体
            SelfTest.True(SheepVisuals.MapState(200) == SheepAnim.Idle, "越界状态回到 Idle", SheepVisuals.MapState(200).ToString());

            float shrink, sink, darken;
            SheepVisuals.CorpseParams(0.0, out shrink, out sink, out darken);
            SelfTest.True(shrink == 1f && sink == 0f && darken == 1f, "尸体起点完好", shrink.ToString("R"));
            SheepVisuals.CorpseParams(1.0, out shrink, out sink, out darken);
            SelfTest.True(shrink == 0.72f && sink == 0.16f && darken == 0.62f, "尸体终点 0.72/0.16/0.62", shrink.ToString("R"));
            SelfTest.True(SheepVisuals.CorpseProgress(750.0) == 0.5, "1500ms 半程", SheepVisuals.CorpseProgress(750.0).ToString("R"));
            SelfTest.True(SheepVisuals.CorpseFadeMs == 1500.0, "淡出时长 1500ms", SheepVisuals.CorpseFadeMs.ToString("R"));

            // 死亡羊只写进池一次，淡出结束后被剔除
            var pool = new SheepInstancePool();
            var visuals = new SheepVisuals(pool);
            var dead = new EntityView();
            dead.Id = 1;
            dead.Kind = 0;
            dead.Visible = true;
            dead.Z = 5.0;
            dead.State = 8;
            SelfTest.Equal(0, visuals.Write(dead, 0.0));
            pool.Reset();
            SelfTest.Equal(0, visuals.Write(dead, 750.0));
            pool.Reset();
            SelfTest.Equal(-1, visuals.Write(dead, 1600.0));
            SelfTest.True(visuals.CorpseCount == 3, "三次都按尸体处理", visuals.CorpseCount.ToString());
        }

        private static void ChecksCulling()
        {
            var cameraObject = new GameObject("SheepCamera");
            var camera = cameraObject.AddComponent<Camera>();
            camera.fieldOfView = 75f;
            camera.nearClipPlane = 0.1f;
            camera.farClipPlane = 300f;
            camera.transform.position = Vector3.zero;
            camera.transform.rotation = Quaternion.identity;   // 朝 +Z

            var pool = new SheepInstancePool();
            var instances = pool.Instances;
            instances[0] = MakeInstance(new Vector3(0f, 0f, 10f), 0);      // 正前方 10m
            instances[1] = MakeInstance(new Vector3(0f, 0f, 200f), 0);     // 200m 外
            instances[2] = MakeInstance(new Vector3(0f, 0f, -10f), 0);     // 背后
            instances[3] = MakeInstance(new Vector3(0f, 0f, 5f), 3);       // 羊王贴身
            instances[4] = MakeInstance(new Vector3(-60f, 0f, 10f), 0);    // 视锥外（左侧偏 80°）

            var visible = new int[SheepInstancePool.EntityCapacity];
            var count = Culling.Filter(camera, pool, visible);
            SelfTest.Equal(2, count);
            SelfTest.Equal(0, visible[0]);
            SelfTest.Equal(3, visible[1]);

            float ratio;
            SelfTest.True(Culling.IsVisible(camera, new Vector3(0f, 0f, 10f), 0.5f, out ratio), "10m 处可见", ratio.ToString("R"));
            SelfTest.True(!Culling.IsVisible(camera, new Vector3(0f, 0f, 95f), 0.5f, out ratio), "95m 被距离剔除", ratio.ToString("R"));
            // ADR-011：剔除距离改由 C14 §5 的羊档（60m）决定，所以"仍然可见"的样本必须落在 60m 以内，
            // 否则这条断言会在距离检查处提前返回，"占比不足"那一路就再也没被真正走过。
            SelfTest.True(Culling.IsVisible(camera, new Vector3(0f, 0f, 55f), 0.5f, out ratio), "55m 处仍在（占比够）", ratio.ToString("R"));
            SelfTest.True(!Culling.IsVisible(camera, new Vector3(0f, 0f, 55f), 0.05f, out ratio), "占比不足被剔除", ratio.ToString("R"));
            SelfTest.True(ratio < (float)Culling.CullMinScreenRatio, "被剔除时占比确实低于阈值", ratio.ToString("R"));
            SelfTest.True(Culling.CullMinScreenRatio == 0.0015, "占比阈值", Culling.CullMinScreenRatio.ToString("R"));
            // 单一来源（ADR-011）：Culling 不再自带距离，读数必须与 Batching 的羊档逐位相同。
            SelfTest.True(Culling.CullDistanceMeters == Batching.CullDistanceMeters(Batching.CullKindSheep) &&
                Culling.CullDistanceMeters == 60f, "剔除距离取自 Batching 羊档", Culling.CullDistanceMeters.ToString("R"));

            // 平面外扩：把一个点放在视锥侧平面外一点点——半径不够时被剔除，羊王半径（1.6m）够得着
            var edge = new Vector3(0f, 0f, 0f);
            var limit = Mathf.Tan(camera.fieldOfView * 0.5f * Mathf.Deg2Rad) * camera.aspect * 12f;
            edge = new Vector3(limit + 0.2f, 0f, 12f);
            var smallRadius = Culling.IsVisible(camera, edge, 0.05f, out ratio);
            var kingRadius = Culling.IsVisible(camera, edge, (float)SheepMesh.Form(SheepKind.King).RadiusM, out ratio);
            SelfTest.True(!smallRadius, "侧平面外一点点、半径不足 → 剔除", "可见");
            SelfTest.True(kingRadius, "同一位置换成羊王半径 → 可见（平面外扩生效）", "被剔除");

            UnityEngine.Object.DestroyImmediate(cameraObject);
        }

        private static SheepInstance MakeInstance(Vector3 position, byte kind)
        {
            var instance = default(SheepInstance);
            var scale = SheepMesh.FormScale((SheepKind)kind);
            instance.Transform = Matrix4x4.TRS(position, Quaternion.identity, new Vector3(scale, scale, scale));
            instance.Kind = kind;
            instance.Visible = true;
            instance.HpRatio = 1f;
            return instance;
        }

        private static long Channel(Color32 color)
        {
            return ((long)color.r << 16) | ((long)color.g << 8) | color.b;
        }
    }
}
