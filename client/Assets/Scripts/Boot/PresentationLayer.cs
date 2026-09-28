using System;
using Ac.Core;
using Ac.Net;
using Ac.Sim;
using Ac.UI;
using Ac.View;
using UnityEngine;
using UnityEngine.Rendering;

namespace Ac.Boot
{
    // B1：呈现层的运行期装配。C07/C08/C09/C13 的计划都停在"纯 C# 可断言"上——画面为空、
    // ViewModel/FpsCamera/ArenaMesh/Effects/SheepInstancePool/DebugPanel 从未被任何生产代码构造。
    // 本类就是那些类的第一个生产构造点：相机、光照、场地网格与碰撞、羊群实例与剔除、特效、
    // 屏幕流与调试面板都在这里造出来并互相接线。
    //
    // 全部 Unity 对象挂在 Root 之下，Dispose() 一次性销毁，且不留下任何静态状态（画质档复位）：
    // 无头用例可以整块构造、整块断言、整块丢掉，不给下一个用例留残留。
    public sealed class PresentationLayer
    {
        public const string RootName = "Ac.Presentation";
        public const string CameraName = "Ac.MainCamera";
        public const string SunName = "Ac.Sun";
        public const string ArenaName = "Ac.Arena";
        public const int ArenaPartCount = 7;
        public const int WorldLayer = 0;
        // 线上 kind（Ac.Net.EntityRecord）：0 player / 1 sheep / 2 projectile / 3 pickup
        public const byte WireKindSheep = 1;
        // URP 的相机附加组件：本 asmdef 不引 URP 程序集（引用一断就编译不过），按类型名探一下，
        // 有就补上；没有就留给 URP 自己在渲染时补（CameraExtensions 会 AddComponent）。
        private const string UniversalCameraDataType =
            "UnityEngine.Rendering.Universal.UniversalAdditionalCameraData, Unity.RenderPipelines.Universal.Runtime";

        // 场地部件 → 材质：顺序固定，Dispose 与断言都按它走
        private static readonly string[] ArenaPartNames = { "Ground", "DirtYard", "Fence", "Barn", "BarnRoof", "Hay", "OuterRing" };

        private readonly GameObject _root;
        private readonly GameObject _cameraObject;
        private readonly GameObject _arenaObject;
        private readonly Mesh[] _arenaParts = new Mesh[ArenaPartCount];
        private readonly Material[] _arenaMaterials = new Material[Materials.MaterialCount];
        private readonly Mesh[] _sheepMeshes = new Mesh[SheepMesh.FormCount];
        private readonly Mesh[] _emblemMeshes = new Mesh[SheepMesh.FormCount];
        // 剔除结果缓冲：Culling 把实例下标写进来，绘制按羊形分桶（池是 form * PerFormCapacity + cursor 的跨步布局）
        private readonly int[] _visible = new int[SheepInstancePool.EntityCapacity];
        // 每形上限 = PerFormCapacity(256) ≤ MaxInstancesPerBatch(1023) ⇒ BatchCount 恒为 1；
        // 数组按每形上限开，而不是按 1023 开（4 份 1023 是 256KB 白占）。
        private readonly Matrix4x4[] _bodyBatch = new Matrix4x4[SheepInstancePool.PerFormCapacity];
        private readonly Matrix4x4[] _emblemBatch = new Matrix4x4[SheepInstancePool.PerFormCapacity];
        private readonly EntityView _scratch = new EntityView();

        private Texture2D[] _textures;
        private Material _sheepMaterial;
        private Material _emblemMaterial;
        private GameLoop _loop;
        private double _nowMs;
        private double _elapsedMs;
        private int _seenMatchStates;
        private int _savedTier;
        private bool _disposed;

        public GameObject Root { get { return _root; } }
        public Camera MainCamera { get; private set; }
        public FpsCamera Fps { get; private set; }
        public Light Sun { get; private set; }
        public ArenaMesh Arena { get; private set; }
        public MeshStats ArenaStats { get; private set; }
        public ArenaColliders Colliders { get; private set; }
        public int ArenaColliderCount { get; private set; }
        public Materials Materials { get; private set; }
        public MaterialTable MaterialTable { get; private set; }
        public SheepInstancePool SheepPool { get; private set; }
        public SheepVisuals Sheep { get; private set; }
        public Effects Effects { get; private set; }
        public ViewModel ViewModel { get; private set; }
        public DebugPanel DebugPanel { get; private set; }
        public ScreenFlow Flow { get; private set; }

        public bool MaterialsReady { get { return _sheepMaterial != null; } }
        public int FxTicks { get; private set; }
        public int OverlayTicks { get; private set; }
        public int DrawTicks { get; private set; }
        public int HitFxCount { get; private set; }
        public int CameraPoseUpdates { get; private set; }
        public int WrittenSheepCount { get; private set; }
        public int CulledSheepCount { get; private set; }
        public int DrawnSheepCount { get; private set; }
        public int SubmittedDraws { get; private set; }
        public DrawMode LastDrawMode { get; private set; }

        public IFrameStageSink FxSink { get { return _fxSink; } }
        public IFrameWorkSink DrawSink { get { return _drawSink; } }
        public IFrameWorkSink OverlaySink { get { return _overlaySink; } }

        private readonly FxStage _fxSink;
        private readonly DrawStage _drawSink;
        private readonly OverlayStage _overlaySink;

        private PresentationLayer(in SettingsSnapshot settings)
        {
            _savedTier = Batching.QualityTier;
            // 三条呈现缝只造一次：它们被 GameLoop 每帧调用，属性里 new 等于每帧一次分配（帧基准判 0 B/帧）
            _fxSink = new FxStage(this);
            _drawSink = new DrawStage(this);
            _overlaySink = new OverlayStage(this);

            _root = new GameObject(RootName);

            // ① 相机：FpsCamera 是规格（FOV 60..100、近远裁 0.1/300、眼高 1.6），UnityEngine.Camera 是它在引擎里的落点
            _cameraObject = new GameObject(CameraName);
            _cameraObject.transform.SetParent(_root.transform, false);
            _cameraObject.tag = "MainCamera";                       // Camera.main 靠这个 tag
            MainCamera = _cameraObject.AddComponent<Camera>();
            MainCamera.nearClipPlane = (float)FpsCamera.NearClipMeters;
            MainCamera.farClipPlane = (float)FpsCamera.FarClipMeters;
            Fps = new FpsCamera();
            Fps.ApplyFov(settings.Fov);
            MainCamera.fieldOfView = (float)Fps.Fov;
            EnsureUniversalCameraData(_cameraObject);

            // ② 光照与渲染环境（只设此处传入的光源与相机，全局 QualitySettings 不动）
            var sunObject = new GameObject(SunName);
            sunObject.transform.SetParent(_root.transform, false);
            Sun = sunObject.AddComponent<Light>();
            LightingRig.Apply(Sun, MainCamera);

            // ③ 场地：程序化网格 + 6 种材质 + 5 个静态碰撞盒
            Arena = new ArenaMesh(ArenaParams.Default());
            ArenaStats = Arena.Build();
            Materials = new Materials();
            MaterialTable = Materials.Create();
            _textures = Materials.CreateTextures();
            _arenaMaterials[0] = MaterialTable.Grass;
            _arenaMaterials[1] = MaterialTable.Dirt;
            _arenaMaterials[2] = MaterialTable.Fence;
            _arenaMaterials[3] = MaterialTable.BarnWall;
            _arenaMaterials[4] = MaterialTable.BarnRoof;
            _arenaMaterials[5] = MaterialTable.Hay;

            _arenaObject = new GameObject(ArenaName);
            _arenaObject.transform.SetParent(_root.transform, false);
            Colliders = ArenaColliders.Build();
            ArenaColliderCount = Colliders.CreateColliders(_arenaObject);

            _arenaParts[0] = Arena.GroundMesh;
            _arenaParts[1] = Arena.DirtYardMesh;
            _arenaParts[2] = Arena.FenceMesh;
            _arenaParts[3] = Arena.BarnMesh;
            _arenaParts[4] = Arena.BarnRoofMesh;
            _arenaParts[5] = Arena.HayBaleMesh;
            _arenaParts[6] = Arena.OuterRingMesh;
            var partMaterials = new[]
            {
                MaterialTable.Grass, MaterialTable.Dirt, MaterialTable.Fence,
                MaterialTable.BarnWall, MaterialTable.BarnRoof, MaterialTable.Hay, MaterialTable.Grass,
            };
            for (var i = 0; i < ArenaPartCount; i++) AttachPart(ArenaPartNames[i], _arenaParts[i], partMaterials[i]);

            // ④ 羊群：四形网格 + 额标网格 + 实例池 + 快照→实例的写入器
            SheepPool = new SheepInstancePool();
            Sheep = new SheepVisuals(SheepPool);
            for (var form = 0; form < SheepMesh.FormCount; form++)
            {
                _sheepMeshes[form] = SheepMesh.Build((SheepKind)form);
                _emblemMeshes[form] = SheepMesh.BuildEmblem((SheepKind)form);
            }
            _sheepMaterial = MakeInstancedMaterial("Ac/Sheep", SheepMesh.ColorOf(SheepKind.Grunt));
            _emblemMaterial = MakeInstancedMaterial("Ac/Emblem", SheepMesh.EmblemColorPure(SheepKind.Grunt));

            // ⑤ 特效与视图模型：Effects 的池在构造期分配，帧内不再分配
            Effects = new Effects();
            ViewModel = new ViewModel();
            ViewModel.Attach(MainCamera);

            // ⑥ 屏幕流与调试面板
            Flow = new ScreenFlow();
            DebugPanel = new DebugPanel();
            Flow.Lobby.OnPhaseChanged += OnPhaseChanged;

            Batching.SetQualityTier(settings.QualityTier);
            Effects.SetReducedMotion(settings.ReduceMotion);
        }

        public static PresentationLayer Create(in SettingsSnapshot settings)
        {
            return new PresentationLayer(settings);
        }

        public static PresentationLayer Create(in SettingsSnapshot settings, GameObject parent)
        {
            var layer = new PresentationLayer(settings);
            if (parent != null) layer._root.transform.SetParent(parent.transform, false);
            return layer;
        }

        // 接线：帧回路的镜像/剖析器/事件是呈现层的输入。接上之后 fx/overlay/draw 三段才会真的做功。
        public void Attach(GameLoop loop)
        {
            _loop = loop;
            if (loop == null) return;
            loop.EventApplied = OnEventApplied;
            _seenMatchStates = loop.MatchStateCount;
            if (loop.MatchStateCount > 0) Flow.Apply(loop.LastMatchState, loop.LocalPlayerId);
        }

        public void ApplySettings(in SettingsSnapshot settings)
        {
            Fps.ApplyFov(settings.Fov);
            if (MainCamera != null) MainCamera.fieldOfView = (float)Fps.Fov;
            Batching.SetQualityTier(settings.QualityTier);
            Effects.SetReducedMotion(settings.ReduceMotion);
            // H5：crosshairColor / colorblindSafe 此前只被序列化、没有任何行为读者，准星的颜色就写在这里
            if (_loop != null && _loop.Hud != null) _loop.Hud.Crosshair.SetPalette(settings.CrosshairColor, settings.ColorblindSafe);
        }

        public void ToggleDebugPanel() { DebugPanel.Toggle(); }

        // 程序化网格的只读取用口：用例要断言"生成出来的网格真的有顶点"，而它们只挂在本层内部
        public Mesh SheepMeshFor(SheepKind kind) { return _sheepMeshes[(int)kind]; }
        public Mesh EmblemMeshFor(SheepKind kind) { return _emblemMeshes[(int)kind]; }

        // ---- 三段呈现工作（由 GameLoop 按段调用；打点规则见 IFrameWorkSink） ----

        public void TickFx(double dtMs)
        {
            if (_disposed) return;
            FxTicks += 1;
            SyncCamera();
            ViewModel.Refresh();
            Effects.Tick((float)dtMs);
        }

        public bool TickOverlay(double dtMs)
        {
            if (_disposed) return false;
            OverlayTicks += 1;
            _elapsedMs += dtMs;
            var loop = _loop;
            if (loop != null)
            {
                if (loop.MatchStateCount != _seenMatchStates)
                {
                    _seenMatchStates = loop.MatchStateCount;
                    Flow.Apply(loop.LastMatchState, loop.LocalPlayerId);
                }
                DebugPanel.Tick((float)dtMs, Sample());
            }
            return true;
        }

        public bool TickDraw(double dtMs)
        {
            if (_disposed) return false;
            DrawTicks += 1;
            _nowMs += dtMs;

            Sheep.BeginFrame();            // 池的"每帧复位"契约：漏掉它，同形累计满 256 之后羊会静默消失
            WriteSheepInstances();
            WrittenSheepCount = SheepPool.Count;

            var culled = Culling.Filter(MainCamera, SheepPool, _visible);
            CulledSheepCount = culled;

            var drawn = 0;
            var submitted = 0;
            if (culled > 0 && _sheepMaterial != null)
            {
                // 画质档的实例上限（256/512/1024）在这里生效：低档先保帧率
                var cap = Batching.VisibleInstances(culled, Batching.QualityTier);
                LastDrawMode = Batching.Decide(cap);
                var instanced = LastDrawMode == DrawMode.Instanced;
                for (var form = 0; form < SheepMesh.FormCount && drawn < cap; form++)
                {
                    var start = form * SheepInstancePool.PerFormCapacity;
                    var end = start + SheepInstancePool.PerFormCapacity;
                    var count = 0;
                    for (var v = 0; v < culled && count < _bodyBatch.Length && drawn + count < cap; v++)
                    {
                        var index = _visible[v];
                        if (index < start || index >= end) continue;
                        var instance = SheepPool.Instances[index];
                        _bodyBatch[count] = instance.Transform;
                        _emblemBatch[count] = instance.EmblemTransform;
                        count += 1;
                    }
                    if (count == 0) continue;
                    if (instanced)
                    {
                        var batches = Batching.BatchCount(count, Batching.QualityTier);
                        for (var b = 0; b < batches; b++)
                        {
                            var offset = b * Batching.MaxInstancesPerBatch;
                            var n = count - offset;
                            if (n > Batching.MaxInstancesPerBatch) n = Batching.MaxInstancesPerBatch;
                            if (n <= 0) break;
                            Graphics.DrawMeshInstanced(_sheepMeshes[form], 0, _sheepMaterial, _bodyBatch, n);
                            Graphics.DrawMeshInstanced(_emblemMeshes[form], 0, _emblemMaterial, _emblemBatch, n);
                            submitted += 2;
                        }
                    }
                    else
                    {
                        for (var i = 0; i < count; i++)
                        {
                            Graphics.DrawMesh(_sheepMeshes[form], _bodyBatch[i], _sheepMaterial, WorldLayer);
                            Graphics.DrawMesh(_emblemMeshes[form], _emblemBatch[i], _emblemMaterial, WorldLayer);
                        }
                        submitted += 2 * count;
                    }
                    drawn += count;
                }
            }
            DrawnSheepCount = drawn;
            SubmittedDraws = submitted;
            // 没有提交就不返回 true ⇒ GameLoop 不给 draw 打点（见 IFrameWorkSink）
            return submitted > 0;
        }

        public void Dispose()
        {
            if (_disposed) return;
            _disposed = true;
            if (_loop != null) _loop.EventApplied = null;
            Flow.Lobby.OnPhaseChanged -= OnPhaseChanged;
            Batching.SetQualityTier(_savedTier);
            Kill(_sheepMaterial);
            Kill(_emblemMaterial);
            if (_arenaMaterials != null) for (var i = 0; i < _arenaMaterials.Length; i++) Kill(_arenaMaterials[i]);
            if (_textures != null) for (var i = 0; i < _textures.Length; i++) Kill(_textures[i]);
            for (var i = 0; i < _arenaParts.Length; i++) Kill(_arenaParts[i]);
            for (var i = 0; i < _sheepMeshes.Length; i++) { Kill(_sheepMeshes[i]); Kill(_emblemMeshes[i]); }
            Kill(_root);
        }

        // ---- 内部 ----

        // 事件 → 特效：此前事件只有 HUD 一个消费者，Effects.SpawnHit 在生产里零调用。
        private void OnEventApplied(HudEvent hudEvent)
        {
            if (hudEvent.Type != Ac.Net.EventType.PlayerHit && hudEvent.Type != Ac.Net.EventType.SheepKilled) return;
            var position = MainCamera == null ? Vector3.zero : MainCamera.transform.position;
            EntityView target;
            if (hudEvent.TargetId > 0 && _loop != null && _loop.Views.TryGet((ushort)hudEvent.TargetId, out target) && target != null)
            {
                position = new Vector3((float)target.RenderX, (float)target.RenderY, (float)target.RenderZ);
            }
            Effects.SetHitAnchor(position);
            Effects.SpawnHit(hudEvent.HitFlags);
            HitFxCount += 1;
        }

        private void OnPhaseChanged(byte phase)
        {
            // §5.6：进入对局才锁指针，离开对局必须释放（否则结算界面上鼠标是死的）
            if (phase == Hud.PhasePlaying) Fps.RequestPointerLock();
            else Fps.ReleasePointerLock();
        }

        private void SyncCamera()
        {
            var loop = _loop;
            if (loop == null || MainCamera == null) return;
            EntityView local;
            if (!loop.Views.TryGet(loop.LocalPlayerId, out local) || local == null) return;
            Fps.SetPose(local.RenderX, local.RenderY, local.RenderZ, local.YawRad, local.PitchRad);
            // 眼高由 FpsCamera 加（SetPose 内部），俯仰取负号是 Unity 的朝向约定
            _cameraObject.transform.SetPositionAndRotation(
                new Vector3((float)Fps.X, (float)Fps.Y, (float)Fps.Z),
                Quaternion.Euler((float)(-Fps.PitchRad * Mathf.Rad2Deg), (float)(Fps.YawRad * Mathf.Rad2Deg), 0f));
            CameraPoseUpdates += 1;
        }

        private void WriteSheepInstances()
        {
            var views = _loop == null ? null : _loop.Views;
            if (views == null) return;
            for (var i = 0; i < views.ActiveCount; i++)
            {
                EntityView view;
                if (!views.TryGet(views.ActiveIdAt(i), out view) || view == null) continue;
                if (!view.Visible) continue;
                if ((view.Flags & EntityRecord.KindMask) != WireKindSheep) continue;   // 玩家/弹丸/拾取物不进羊群池
                _scratch.Id = view.Id;
                _scratch.Kind = (byte)SheepFormOf(view.State);
                _scratch.Visible = true;
                _scratch.Flags = view.Flags;
                _scratch.State = view.State;
                _scratch.HpRatio = view.HpRatio;
                _scratch.X = view.RenderX;
                _scratch.Y = view.RenderY;
                _scratch.Z = view.RenderZ;
                _scratch.YawRad = view.YawRad;
                Sheep.Write(_scratch, _nowMs);
            }
        }

        // 线上 kind 只有「1 = 羊」这一档，羊形（grunt/ram/elite/king）根本不在快照里，而 C08 的
        // SheepVisuals.Write 把 view.Kind 直接当羊形用（两份计划的冲突，不自行裁决）。这里只用
        // 快照里能确证的一档：state ≥ KingIdle(9) ⇒ 羊王（C08 §5(e) 的 9..12 全是羊王状态），其余按 grunt 画。
        public static SheepKind SheepFormOf(byte state)
        {
            return state >= (byte)SheepAnim.KingIdle ? SheepKind.King : SheepKind.Grunt;
        }

        private DebugSample Sample()
        {
            var sample = default(DebugSample);
            sample.VersionLine = VersionInfo.VersionLine;
            var loop = _loop;
            if (loop == null) return sample;
            sample.PlayerTick = loop.LocalSteps;
            sample.ServerTick = (int)loop.View.AppliedTick;
            // 会话层（C03）还没接进自举：离线时 Transport 为空，网络四项老老实实报 n/a
            sample.HasNetwork = loop.Transport != null;
            sample.HasFrameTimes = loop.Profiler.FilledFrames > 0;
            sample.FrameTimeP95Ms = loop.Profiler.FrameP95Ms();
            // 剖析器没有"最大值"口径，用 P99 顶上（面板的告警阈值本来就是 FrameBudget.FrameP99BudgetMs）
            sample.FrameTimeMaxMs = loop.Profiler.FrameP99Ms();
            sample.HasViewCounts = true;
            sample.EntityCount = loop.Views.ActiveCount;
            sample.PooledCount = SheepPool.Count;
            sample.HasSnapshotRate = _elapsedMs > 0.0 && loop.SnapshotsApplied > 0;
            sample.SnapshotRateHz = _elapsedMs > 0.0 ? (float)(loop.SnapshotsApplied / (_elapsedMs / 1000.0)) : 0f;
            return sample;
        }

        private void AttachPart(string name, Mesh mesh, Material material)
        {
            if (mesh == null) return;
            var part = new GameObject(name);
            part.transform.SetParent(_arenaObject.transform, false);
            var filter = part.AddComponent<MeshFilter>();
            filter.sharedMesh = mesh;
            var renderer = part.AddComponent<MeshRenderer>();
            renderer.sharedMaterial = material;
        }

        private static Material MakeInstancedMaterial(string name, Color32 color)
        {
            var shader = Shader.Find("Universal Render Pipeline/Lit");
            if (shader == null) shader = Shader.Find("Standard");
            if (shader == null) return null;      // 无图形设备/着色器被剥离：不伪造材质，也不会因此打点
            var material = new Material(shader);
            material.name = name;
            material.color = color;
            if (material.HasProperty("_BaseColor")) material.SetColor("_BaseColor", color);
            material.enableInstancing = true;     // DrawMeshInstanced 要求着色器支持实例化
            return material;
        }

        private static void EnsureUniversalCameraData(GameObject cameraObject)
        {
            var type = Type.GetType(UniversalCameraDataType);
            if (type == null) return;
            if (cameraObject.GetComponent(type) != null) return;
            cameraObject.AddComponent(type);
        }

        private static void Kill(UnityEngine.Object target)
        {
            if (target == null) return;
            if (Application.isPlaying) UnityEngine.Object.Destroy(target);
            else UnityEngine.Object.DestroyImmediate(target);
        }

        private sealed class FxStage : IFrameStageSink
        {
            private readonly PresentationLayer _layer;
            internal FxStage(PresentationLayer layer) { _layer = layer; }
            public void Tick(double dtMs) { _layer.TickFx(dtMs); }
        }

        private sealed class DrawStage : IFrameWorkSink
        {
            private readonly PresentationLayer _layer;
            internal DrawStage(PresentationLayer layer) { _layer = layer; }
            public bool Tick(double dtMs) { return _layer.TickDraw(dtMs); }
        }

        private sealed class OverlayStage : IFrameWorkSink
        {
            private readonly PresentationLayer _layer;
            internal OverlayStage(PresentationLayer layer) { _layer = layer; }
            public bool Tick(double dtMs) { return _layer.TickOverlay(dtMs); }
        }
    }
}
