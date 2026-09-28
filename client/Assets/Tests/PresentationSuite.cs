using System;
using System.Collections.Generic;
using Ac.Boot;
using Ac.Core;
using Ac.Net;
using Ac.Sim;
using Ac.UI;
using Ac.View;
using UnityEngine;

namespace Ac.Tests
{
    // B1 的守门用例：证明"呈现层真的被装配起来了、而且不抛异常"。
    // 断言都打在结构上（对象在不在、网格顶点数、实例池能不能租出/归还、屏幕流跟不跟相位、
    // 哪几段被打点），把装配逻辑抽掉就会变红。每个用例结束时把造出来的 GameObject/Mesh/Material
    // 全部 DestroyImmediate，并且复位静态状态（画质档），不给下一个用例留残留。
    internal static class PresentationSuite
    {
        public static void Register()
        {
            SelfTest.Add("presentation.assembles", ChecksAssembles);
            SelfTest.Add("presentation.screen_flow_phases", ChecksScreenFlowPhases);
            SelfTest.Add("presentation.stage_marks", ChecksStageMarks);
            SelfTest.Add("presentation.fx_from_events", ChecksFxFromEvents);
            SelfTest.Add("presentation.crosshair_palette", ChecksCrosshairPalette);
            SelfTest.Add("presentation.sheep_pool_and_draw", ChecksSheepPoolAndDraw);
            SelfTest.Add("presentation.dispose_cleanup", ChecksDisposeCleanup);
        }

        // 装配 + 帧回路的最小组合：用例统一用它，Dispose 里把两条一起丢掉
        private sealed class Rig : IDisposable
        {
            internal PresentationLayer Layer;
            internal GameLoop Loop;

            // LightingRig 会改 RenderSettings（场景级全局态）：装配前存一份，Dispose 后放回去，
            // 免得后面的用例看到的是"上一局的天光"。
            private readonly bool _fog;
            private readonly UnityEngine.Rendering.AmbientMode _ambientMode;
            private readonly UnityEngine.Color _ambientLight;
            private readonly float _ambientIntensity;
            private readonly UnityEngine.FogMode _fogMode;
            private readonly float _fogStart;
            private readonly float _fogEnd;
            private readonly UnityEngine.Color _fogColor;

            internal Rig(in SettingsSnapshot settings)
            {
                _fog = RenderSettings.fog;
                _ambientMode = RenderSettings.ambientMode;
                _ambientLight = RenderSettings.ambientLight;
                _ambientIntensity = RenderSettings.ambientIntensity;
                _fogMode = RenderSettings.fogMode;
                _fogStart = RenderSettings.fogStartDistance;
                _fogEnd = RenderSettings.fogEndDistance;
                _fogColor = RenderSettings.fogColor;

                Layer = PresentationLayer.Create(settings);
                Loop = new GameLoop(new SnapshotView(), new EntityViews(), new Hud(), new FrameProfiler());
                Loop.LocalPlayerId = 1;
                Layer.Attach(Loop);
                Layer.ApplySettings(settings);
            }

            public void Dispose()
            {
                if (Layer != null) Layer.Dispose();
                Layer = null;
                Loop = null;
                RenderSettings.fog = _fog;
                RenderSettings.ambientMode = _ambientMode;
                RenderSettings.ambientLight = _ambientLight;
                RenderSettings.ambientIntensity = _ambientIntensity;
                RenderSettings.fogMode = _fogMode;
                RenderSettings.fogStartDistance = _fogStart;
                RenderSettings.fogEndDistance = _fogEnd;
                RenderSettings.fogColor = _fogColor;
            }
        }

        // 全量快照：实体 1 = 本地玩家（线上 kind 0），实体 2.. = 羊（线上 kind 1），羊排在玩家正前方
        private static void Feed(GameLoop loop, int sheepCount, uint tick, double firstX, double stepX, double z)
        {
            var frame = default(SnapshotFrame);
            frame.Entities = new FrameEntity[SnapshotView.MaxRecordsPerFrame];
            frame.RemovedIds = new ushort[SnapshotView.MaxRemovedPerFrame];
            frame.Tick = tick;
            frame.ServerTimeMs = tick * 16u;
            frame.LastAckedSeq = (ushort)tick;
            frame.BaselineTick = 0u;                       // 每帧全量
            frame.EntityCount = sheepCount + 1;
            frame.RemovedCount = 0;
            frame.Entities[0] = Entity(1, 0, 0.0, 0.0, 0.0);
            for (var i = 0; i < sheepCount; i++)
            {
                frame.Entities[i + 1] = Entity((ushort)(i + 2), PresentationLayer.WireKindSheep, firstX + stepX * i, 0.0, z);
            }
            SelfTest.True(loop.ApplySnapshot(frame), "快照应当被镜像接受", "被丢弃");
        }

        private static FrameEntity Entity(ushort id, byte kind, double x, double y, double z)
        {
            var entity = default(FrameEntity);
            entity.Id = id;
            entity.KindFlags = kind;
            entity.XCm = (short)Math.Round(x * 100.0);
            entity.YCm = (short)Math.Round(y * 100.0);
            entity.ZCm = (short)Math.Round(z * 100.0);
            entity.YawUnits = 0;
            // 角度单位是整圈 65536：32768 是 π（会把相机翻过去），水平必须写 0
            entity.PitchUnits = 0;
            entity.HpRatioUnits = 255;
            return entity;
        }

        private static void Step(Rig rig, int sheepCount, uint tick, double firstX, double stepX, double z)
        {
            Feed(rig.Loop, sheepCount, tick, firstX, stepX, z);
            rig.Loop.Frame(1000.0 / 60.0);
        }

        private static void ChecksAssembles()
        {
            var rig = new Rig(SettingsDefaults.Default());
            try
            {
                var layer = rig.Layer;
                SelfTest.True(layer.Root != null, "装配根必须建出来", "是 null");
                SelfTest.True(GameObject.Find(PresentationLayer.RootName) != null, "装配根要能在场景里找到", "找不到");
                // 相机：真实 UnityEngine.Camera + Camera.main + FpsCamera 的冻结参数
                SelfTest.True(layer.MainCamera != null, "必须有真实的相机组件", "是 null");
                SelfTest.True(Camera.main == layer.MainCamera, "相机必须是 Camera.main", Camera.main == null ? "Camera.main 是 null" : "是别的相机");
                SelfTest.True(layer.MainCamera.transform.parent == layer.Root.transform, "相机挂在装配根下", "挂错了");
                SelfTest.True(Math.Abs(layer.MainCamera.nearClipPlane - (float)FpsCamera.NearClipMeters) < 1e-6, "近裁 0.1", layer.MainCamera.nearClipPlane.ToString("R"));
                SelfTest.True(Math.Abs(layer.MainCamera.farClipPlane - (float)FpsCamera.FarClipMeters) < 1e-6, "远裁 300", layer.MainCamera.farClipPlane.ToString("R"));
                SelfTest.True(Math.Abs(layer.Fps.Fov - SettingsDefaults.Fov) < 1e-6, "FOV 取自设置", layer.Fps.Fov.ToString("R"));
                SelfTest.True(Math.Abs(layer.MainCamera.fieldOfView - (float)layer.Fps.Fov) < 1e-4, "相机 FOV 与 FpsCamera 一致", layer.MainCamera.fieldOfView.ToString("R"));

                // 光照：一盏方向光 + 环境（LightingRig 的冻结值）
                SelfTest.True(layer.Sun != null && layer.Sun.type == LightType.Directional, "必须有方向光", "没有");
                SelfTest.True(Math.Abs(layer.Sun.intensity - LightingRig.SunIntensity) < 1e-6, "太阳强度取冻结值", layer.Sun.intensity.ToString("R"));

                // 场地：7 个程序化网格（顶点 > 0）+ 7 个渲染器 + 5 个碰撞盒
                var meshes = new[] { layer.Arena.GroundMesh, layer.Arena.DirtYardMesh, layer.Arena.FenceMesh, layer.Arena.BarnMesh, layer.Arena.BarnRoofMesh, layer.Arena.HayBaleMesh, layer.Arena.OuterRingMesh };
                for (var i = 0; i < meshes.Length; i++)
                {
                    SelfTest.True(meshes[i] != null, "场地部件 " + i + " 必须有网格", "是 null");
                    SelfTest.True(meshes[i].vertexCount > 0, "场地部件 " + i + " 的顶点数必须 > 0", meshes[i].vertexCount.ToString());
                }
                SelfTest.Equal(ArenaMesh.GroundVertexSide * ArenaMesh.GroundVertexSide, layer.Arena.GroundMesh.vertexCount);
                SelfTest.Equal(ArenaMesh.GroundTriangles, layer.Arena.GroundMesh.triangles.Length / 3);
                SelfTest.True(layer.ArenaStats.Vertices > layer.Arena.GroundMesh.vertexCount, "统计值来自各网格求和", layer.ArenaStats.Vertices.ToString());
                SelfTest.Equal(Materials.MaterialCount, layer.ArenaStats.Materials);
                var arenaRoot = layer.Root.transform.Find(PresentationLayer.ArenaName);
                SelfTest.True(arenaRoot != null, "场地节点必须存在", "找不到");
                SelfTest.Equal(PresentationLayer.ArenaPartCount, arenaRoot.GetComponentsInChildren<MeshRenderer>().Length);
                SelfTest.Equal(ArenaColliders.BoxCount, layer.ArenaColliderCount);
                SelfTest.Equal(ArenaColliders.BoxCount, arenaRoot.GetComponentsInChildren<BoxCollider>().Length);
                SelfTest.Equal(ArenaColliders.BoxCount, (long)layer.Colliders.Boxes.Length);

                // 羊群：四形羊体 + 四形额标的网格都在预算内
                for (var form = 0; form < SheepMesh.FormCount; form++)
                {
                    var body = layer.SheepMeshFor((SheepKind)form);
                    var emblem = layer.EmblemMeshFor((SheepKind)form);
                    SelfTest.True(body != null && body.vertexCount > 0, "羊形 " + form + " 的网格顶点数 > 0", body == null ? "null" : body.vertexCount.ToString());
                    SelfTest.True(body.vertexCount <= SheepMesh.VertexBudgetPerForm, "羊形 " + form + " 顶点数不超预算", body.vertexCount.ToString());
                    SelfTest.True(emblem != null && emblem.vertexCount > 0, "羊形 " + form + " 的额标网格顶点数 > 0", emblem == null ? "null" : emblem.vertexCount.ToString());
                }
                SelfTest.True(layer.SheepMeshFor(SheepKind.Grunt).vertexCount < layer.SheepMeshFor(SheepKind.Ram).vertexCount, "团数不同的羊形顶点数必须不同", "一样");
                SelfTest.True(layer.SheepPool.Instances.Length == SheepInstancePool.EntityCapacity, "实例池容量 1024", layer.SheepPool.Instances.Length.ToString());

                // 视图模型挂在真实相机上（挂点参数来自 ViewModelAnchor）
                SelfTest.True(layer.ViewModel != null, "ViewModel 必须被构造", "是 null");
                SelfTest.True(layer.ViewModel.MuzzleEyeDistanceM > ViewModel.MinMuzzleEyeDistanceM, "枪口与眼位的距离必须真的算出来", layer.ViewModel.MuzzleEyeDistanceM.ToString("R"));
                SelfTest.Equal(0, (long)(layer.FxTicks + layer.OverlayTicks + layer.DrawTicks));   // 还没跑帧：呈现段不该有做功计数
            }
            finally { rig.Dispose(); }
        }

        private static void ChecksScreenFlowPhases()
        {
            var settings = SettingsDefaults.Default();
            var rig = new Rig(settings);
            try
            {
                var flow = rig.Layer.Flow;
                var players = new[]
                {
                    new MatchStatePlayer { Pid = 1, Name = "a", Ready = true },
                    new MatchStatePlayer { Pid = 2, Name = "b", Ready = false },
                };
                var state = default(MatchStatePayload);
                state.Players = players;
                state.Wave = 0;
                flow.Apply(state, 1);
                SelfTest.True(flow.LobbyVisible, "lobby 相位只显示大厅", "没显示");
                SelfTest.True(!flow.PlayingVisible && !flow.IntermissionVisible && !flow.ResultsVisible, "lobby 相位不许显示对局/波间/结算界面", "露出来了");
                SelfTest.Equal(2, (long)flow.PlayerCount);
                SelfTest.Equal(1, (long)flow.ReadyCount);
                SelfTest.Equal(2, (long)flow.RosterRows);
                SelfTest.True(flow.RosterVisible, "大厅要看得见队伍表", "看不见");
                SelfTest.True(!rig.Layer.Fps.Locked, "还没进对局不许锁指针", "锁上了");

                state.Phase = Hud.PhaseLoading;
                flow.Apply(state, 1);
                SelfTest.True(flow.LoadingVisible, "loading 相位显示载入界面", "没显示");

                state.Phase = Hud.PhasePlaying;
                flow.Apply(state, 1);
                SelfTest.True(flow.PlayingVisible && !flow.LobbyVisible, "playing 相位只显示 HUD", "没切过去");
                SelfTest.True(!flow.RosterVisible, "对局中队伍表交给 HUD", "还挂着");
                SelfTest.True(rig.Layer.Fps.Locked, "进对局必须锁指针（相位驱动的副作用）", "没锁");

                state.Phase = Hud.PhaseIntermission;
                state.Wave = 3;
                state.IntermissionMs = 12000;
                flow.Apply(state, 1);
                SelfTest.True(flow.IntermissionVisible, "波间相位显示波间界面", "没显示");
                SelfTest.True(flow.Intermission.SkipEnabled, "剩余 12000ms ≤ 15000 应当可跳过", "不可跳过");
                SelfTest.Equal(12000, (long)flow.Intermission.RemainingMs);
                SelfTest.True(!rig.Layer.Fps.Locked, "离开对局必须释放指针", "还锁着");

                state.Phase = Hud.PhaseEnded;
                flow.Apply(state, 1);
                SelfTest.True(flow.ResultsVisible, "ended 相位显示结算", "没显示");
                SelfTest.True(flow.Results.FetchRequestCount > 0, "进结算要请求一次榜单", "没请求");
                SelfTest.True(!flow.IntermissionVisible, "结算不再显示波间", "还挂着");
                SelfTest.Equal(5, (long)flow.ApplyCount);
                SelfTest.Equal(4, (long)flow.Lobby.PhaseChangeCount);   // lobby→loading→playing→intermission→ended

                // 重复同相位不重入（§8 第一条风险）
                flow.Apply(state, 1);
                SelfTest.Equal(4, (long)flow.Lobby.PhaseChangeCount);
            }
            finally { rig.Dispose(); }
        }

        // 打点规则：段真的做功才打点。抽掉装配（= 不接 sink）就一段都不许出现。
        private static void ChecksStageMarks()
        {
            var rig = new Rig(SettingsDefaults.Default());
            try
            {
                for (uint tick = 1; tick <= 5; tick++) Step(rig, 4, tick, -2, 1, 10);
                var profiler = rig.Loop.Profiler;
                SelfTest.True(profiler.P95Ms(FrameStage.Fx) == 0f, "未接 sink 时 fx 段不许打点", profiler.P95Ms(FrameStage.Fx).ToString("R"));
                SelfTest.True(profiler.P95Ms(FrameStage.Overlay) == 0f, "未接 sink 时 overlay 段不许打点", profiler.P95Ms(FrameStage.Overlay).ToString("R"));
                SelfTest.True(profiler.P95Ms(FrameStage.Draw) == 0f, "未接 sink 时 draw 段不许打点", profiler.P95Ms(FrameStage.Draw).ToString("R"));
                SelfTest.Equal(0, (long)rig.Layer.FxTicks);

                rig.Loop.Fx = rig.Layer.FxSink;
                rig.Loop.Overlay = rig.Layer.OverlaySink;
                rig.Loop.Draw = rig.Layer.DrawSink;
                for (uint tick = 6; tick <= 10; tick++) Step(rig, 30, tick, -5, 0.34, 10);

                SelfTest.Equal(5, (long)rig.Layer.FxTicks);
                SelfTest.Equal(5, (long)rig.Layer.OverlayTicks);
                SelfTest.Equal(5, (long)rig.Layer.DrawTicks);
                SelfTest.True(profiler.P95Ms(FrameStage.Fx) > 0f, "接了 fx sink 就该打点", profiler.P95Ms(FrameStage.Fx).ToString("R"));
                SelfTest.True(profiler.P95Ms(FrameStage.Overlay) > 0f, "overlay 每帧都做功，必须打点", profiler.P95Ms(FrameStage.Overlay).ToString("R"));
                // draw 只在真的提了绘制时才有样本：本机取不到着色器时 SubmittedDraws == 0，那就必须缺段
                if (rig.Layer.SubmittedDraws > 0)
                {
                    SelfTest.True(profiler.P95Ms(FrameStage.Draw) > 0f, "有绘制提交就必须给 draw 打点", profiler.P95Ms(FrameStage.Draw).ToString("R"));
                }
                else
                {
                    SelfTest.True(profiler.P95Ms(FrameStage.Draw) == 0f, "没有绘制提交就不许给 draw 打点", profiler.P95Ms(FrameStage.Draw).ToString("R"));
                }

                // 摘掉 sink：不许再涨（同 BootSuite 的门）
                var fx = rig.Layer.FxTicks;
                rig.Loop.Fx = null;
                rig.Loop.Draw = null;
                rig.Loop.Overlay = null;
                for (uint tick = 11; tick <= 13; tick++) Step(rig, 30, tick, -5, 0.34, 10);
                SelfTest.Equal(fx, (long)rig.Layer.FxTicks);
            }
            finally { rig.Dispose(); }
        }

        // 事件缝：服务端命中事件 → 特效池真的出粒子（此前事件只有 HUD 一个消费者）
        private static void ChecksFxFromEvents()
        {
            var rig = new Rig(SettingsDefaults.Default());
            try
            {
                Step(rig, 1, 1, -2, 1, 10);                 // 目标实体 id=2 在 (x=-2, z=10)
                SelfTest.Equal(0, (long)rig.Layer.HitFxCount);
                rig.Loop.Events = new EventIdTracker();

                var header = default(PacketHeader);
                header.Type = PacketType.Event;
                rig.Loop.OnPacket(header, HitEventPayload());

                SelfTest.Equal(1, (long)rig.Loop.EventsApplied);
                SelfTest.Equal(1, (long)rig.Layer.HitFxCount);
                var particles = rig.Layer.Effects.ParticlePool;
                SelfTest.True(particles.LiveCount > 0, "命中事件必须真的发出粒子", particles.LiveCount.ToString());
                var anchor = false;
                for (var i = 0; i < Particles.Capacity; i++)
                {
                    if (!particles.Buffer[i].Alive) continue;
                    anchor = true;
                    // 弹着点取自事件目标实体的渲染位置：抽掉"TargetId → 视图"这一步就会掉到相机脚下
                    SelfTest.True(Math.Abs(particles.Buffer[i].Position.z - 10f) < 0.01f, "粒子落在目标实体处（z=10）", particles.Buffer[i].Position.z.ToString("R"));
                    break;
                }
                SelfTest.True(anchor, "至少有一粒活着", "一粒都没有");
                SelfTest.True(rig.Layer.Effects.Vignette > Effects.VignetteMin, "命中要让屏幕泛红", rig.Layer.Effects.Vignette.ToString("R"));
            }
            finally { rig.Dispose(); }
        }

        private static void ChecksCrosshairPalette()
        {
            var settings = SettingsDefaults.Default();
            settings.CrosshairColor = 0x66E0FF;
            settings.ColorblindSafe = false;
            settings.ReduceMotion = true;
            var rig = new Rig(settings);
            try
            {
                SelfTest.Equal(0x66E0FF, rig.Loop.Hud.Crosshair.ColorRgb);
                SelfTest.True(!rig.Loop.Hud.Crosshair.ColorblindSafe, "色盲档关着", "开着");
                SelfTest.True(rig.Layer.Effects.ParticlePool.ReducedMotion, "reduceMotion 要传到粒子池", "没传");

                settings.ColorblindSafe = true;
                rig.Layer.ApplySettings(settings);
                var expected = SettingsDefaults.MostContrastingCrosshairColor();
                SelfTest.True(expected != 0x66E0FF, "色盲档必须挑一个与用户选色不同的档（否则这条断言没有判别力）", expected.ToString("X6"));
                SelfTest.Equal(expected, rig.Loop.Hud.Crosshair.ColorRgb);
                SelfTest.True(rig.Loop.Hud.Crosshair.ColorblindSafe, "色盲档开着", "关着");
            }
            finally { rig.Dispose(); }
        }

        private static void ChecksSheepPoolAndDraw()
        {
            var rig = new Rig(SettingsDefaults.Default());
            try
            {
                var pool = rig.Layer.SheepPool;
                // 租出 / 归还（Reset 即"本帧复位"：游标归零 + 整池失活）
                pool.Reset();
                int index;
                SelfTest.Equal(0, (long)pool.TryAcquire((int)SheepKind.Grunt, out index));
                SelfTest.Equal(1, (long)pool.Count);
                pool.Reset();
                SelfTest.Equal(0, (long)pool.Count);
                SelfTest.True(pool.TryAcquire((int)SheepKind.Grunt, out index) >= 0 && index == 0, "归还后从 0 号槽重新发", index.ToString());

                rig.Loop.Fx = rig.Layer.FxSink;
                rig.Loop.Draw = rig.Layer.DrawSink;
                rig.Loop.Overlay = rig.Layer.OverlaySink;
                for (uint tick = 1; tick <= 3; tick++) Step(rig, 30, tick, -5, 0.34, 10);
                // 相机姿态来自本地实体视图：位置 + FpsCamera 的眼高，抽掉同步这一步就会留在原点
                SelfTest.True(rig.Layer.CameraPoseUpdates > 0, "相机姿态必须每帧被同步", "一次都没同步");
                var eye = rig.Layer.MainCamera.transform.position;
                SelfTest.True(Math.Abs(eye.y - (float)FpsCamera.EyeHeightMeters) < 1e-4, "眼高 1.6m", eye.y.ToString("R"));
                SelfTest.True(Math.Abs(eye.x) < 1e-4 && Math.Abs(eye.z) < 1e-4, "相机落在本地玩家位置", eye.ToString("R"));
                // 30 只羊挤在 grunt 一形里：池只租 30 个槽，其余保持空闲
                SelfTest.Equal(30, (long)rig.Layer.WrittenSheepCount);
                SelfTest.Equal(30, (long)rig.Layer.CulledSheepCount);
                SelfTest.Equal(30, (long)rig.Layer.SheepPool.CursorOf((int)SheepKind.Grunt));
                SelfTest.Equal(0, (long)rig.Layer.SheepPool.CursorOf((int)SheepKind.King));
                // 30 ≥ InstanceThreshold(25) ⇒ 走实例化；每形一次 body + 一次额标
                SelfTest.True(rig.Layer.MaterialsReady, "羊材质必须建出来（Shader.Find 取不到 URP/Lit 就没法提交绘制）", "材质是 null");
                SelfTest.True(rig.Layer.LastDrawMode == DrawMode.Instanced, "30 只羊必须走实例化", rig.Layer.LastDrawMode.ToString());
                SelfTest.Equal(30, (long)rig.Layer.DrawnSheepCount);
                SelfTest.Equal(2, (long)rig.Layer.SubmittedDraws);
                // 少量羊走单绘（Decide 的另一条分支）
                Step(rig, 3, 4, -1, 1, 8);
                SelfTest.True(rig.Layer.LastDrawMode == DrawMode.Single, "3 只羊走单绘", rig.Layer.LastDrawMode.ToString());
                SelfTest.Equal(3, (long)rig.Layer.DrawnSheepCount);
                // 画质档上限：最低档 256 ⇒ 1024 只也画不了那么多
                SelfTest.Equal(256, (long)Batching.VisibleInstances(1024, 0));
            }
            finally { rig.Dispose(); }
        }

        private static void ChecksDisposeCleanup()
        {
            var savedTier = Batching.QualityTier;
            Batching.SetQualityTier(0);                     // 故意与设置里的档位不同，看 Dispose 复原不复原
            try
            {
                var settings = SettingsDefaults.Default();   // QualityTier = 2
                var rig = new Rig(settings);
                SelfTest.Equal(2, (long)Batching.QualityTier);
                var mesh = rig.Layer.Arena.GroundMesh;
                SelfTest.True(GameObject.Find(PresentationLayer.RootName) != null, "装配根在场景里", "找不到");
                rig.Dispose();
                SelfTest.True(rig.Layer == null, "Rig 自己把引用清掉", "还留着");
                SelfTest.True(GameObject.Find(PresentationLayer.RootName) == null, "销毁后装配根必须从场景里消失", "还在");
                SelfTest.True(mesh == null, "程序化网格必须被销毁", "还在");
                SelfTest.True(Batching.QualityTier == 0, "画质档要复原（静态状态不许外泄）", Batching.QualityTier.ToString());

                // 再装配一次：没有任何残留会挡住第二次（相机/场景对象都不冲突）
                var again = new Rig(settings);
                try
                {
                    SelfTest.True(again.Layer.MainCamera != null, "第二次装配照样要有相机", "是 null");
                    SelfTest.True(Camera.main == again.Layer.MainCamera, "第二次装配的相机就是 Camera.main", "不是");
                }
                finally { again.Dispose(); }
            }
            finally { Batching.SetQualityTier(savedTier); }
        }

        // type=6 事件帧载荷：tick u32 | count u8 | 条目块（见 EventCodec.DecodeFrame）。
        // 生产侧没有事件编码器（事件只有服务端→客户端一个方向），所以用例自带一个小写入器。
        private static byte[] HitEventPayload()
        {
            var bytes = new List<byte>(32);
            PutU32(bytes, 7);                                     // tick
            bytes.Add(1);                                         // count
            PutU32(bytes, 1);                                     // eventId（幂等键）
            bytes.Add((byte)Ac.Net.EventType.PlayerHit);
            PutU16(bytes, 1);                                     // subjectId
            PutU16(bytes, 2);                                     // targetId = 羊实体 id
            PutU16(bytes, 30);                                    // value（伤害）
            bytes.Add((byte)CombatFlags.HitFlagKilled);
            PutU16(bytes, 0);                                     // hitX/Y/Z（i16，本用例不读）
            PutU16(bytes, 0);
            PutU16(bytes, 0);
            return bytes.ToArray();
        }

        private static void PutU16(List<byte> bytes, ushort value)
        {
            bytes.Add((byte)(value & 0xFF));
            bytes.Add((byte)((value >> 8) & 0xFF));
        }

        private static void PutU32(List<byte> bytes, uint value)
        {
            bytes.Add((byte)(value & 0xFF));
            bytes.Add((byte)((value >> 8) & 0xFF));
            bytes.Add((byte)((value >> 16) & 0xFF));
            bytes.Add((byte)((value >> 24) & 0xFF));
        }
    }
}
