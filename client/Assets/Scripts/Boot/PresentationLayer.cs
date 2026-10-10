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
        // 每形三角形数（构造期从真网格数一次，帧内只做乘法：Mesh.triangles 每次取值都会分配一份 int[]）
        private readonly int[] _sheepTriangles = new int[SheepMesh.FormCount];
        private readonly int[] _emblemTriangles = new int[SheepMesh.FormCount];
        // 每形上限 = PerFormCapacity(256) ≤ MaxInstancesPerBatch(1023) ⇒ BatchCount 恒为 1；
        // 数组按每形上限开，而不是按 1023 开（4 份 1023 是 256KB 白占）。
        private readonly Matrix4x4[] _bodyBatch = new Matrix4x4[SheepInstancePool.PerFormCapacity];
        private readonly Matrix4x4[] _emblemBatch = new Matrix4x4[SheepInstancePool.PerFormCapacity];
        private readonly EntityView _scratch = new EntityView();

        private Texture2D[] _textures;
        // 快照率窗口（最近 1 秒）
        private const double SnapshotRateWindowMs = 1000.0;
        private double _rateWindowMs;
        private int _rateWindowSamples;
        private int _rateWindowApplied;
        private float _lastRateHz;
        private Material _sheepMaterial;
        // S16：弹药补给箱的渲染（实体 kind==3 驱动的静态立方体池，见 SyncCrates）。
        public CrateVisuals Crates { get; private set; }
        public int CrateCount { get { return Crates == null ? 0 : Crates.Count; } }
        private const string CrateRootName = "Ac.Crates";
        private const string CratePartName = "Ac.Crate";
        public const byte WireKindPickup = 3;   // EntityKind::kPickup 的线上 kind 码
        // 箱子视觉尺寸（与服务端 kPickupRadiusM=0.35 的交互半径同尺度）：0.7m 立方，底贴地。
        private const float CrateSizeM = 0.7f;
        private readonly GameObject[] _crateObjects = new GameObject[CrateVisuals.MaxCrates];
        private Material _crateMaterial;
        // 视图模型（C09）：此前 ViewModel 只有锚点、没有网格，所以“手里没有枪”。
        // 锚点 ViewModelAnchor.OffsetZ 是 -0.02（几乎贴在眼位），而横向偏移是 0.17m：在 z≈0 的深度上
        // 这个横向偏移会被投影到屏幕右外侧（实测：枪完全看不见）。把整枪沿 +Z 前推，
        // 让枪口落在 ViewModel.MuzzleOffset.z 附近（0.30 + 0.30 ≈ 0.60）。
        private const float WeaponForwardM = 0.42f;
        private const float WeaponModelScale = 1.55f;   // 视图模型标准做法：放大到能看清细节
        private GameObject _weaponObject;
        private MeshRenderer _weaponRenderer;
        private MeshFilter _weaponFilter;
        private GameObject _weaponAccentObject;
        private MeshFilter _weaponAccentFilter;
        private MeshRenderer _weaponAccentRenderer;
        private Light _muzzleLight;
        private GameObject _shellObject;
        private Vector3 _shellVelocity;
        private double _shellLifeMs;
        private GameObject _muzzleFlash;
        private double _weaponSwayMs;
        private float _weaponKick01;          // 1 = 刚开火，随时间衰减到 0
        private int _weaponSlotBuilt = -1;
        private readonly Mesh[] _weaponBodies = new Mesh[WeaponMesh.SlotCount];
        private readonly Mesh[] _weaponAccents = new Mesh[WeaponMesh.SlotCount];
        public int VisibleWeaponSlot { get { return _weaponSlotBuilt; } }
        public TracerRenderer TracerView { get; private set; }
        private float _bobPhase;

        private Material _emblemMaterial;   // 保留：旧路径的单份引用（用于就绪判定）
        private readonly Material[] _emblemMaterials = new Material[SheepMesh.FormCount];
        private GameLoop _loop;
        private double _nowMs;
        private double _elapsedMs;
        private int _seenMatchStates;
        private int _savedTier;
        private bool _disposed;
        // "本地身份没认领到"的警告只打一次（每帧打会把日志刷爆，反而不看见）
        private bool _warnedNoIdentity;
        // 认领到身份的**成功**日志只打一次（与上面那条配对：只有失败有声音，实跑排障时只能靠猜）
        private bool _identityBoundLogged;
        // 装配时刻（Time.realtimeSinceStartup）：见 NoIdentityWarnSeconds 的宽限
        private double _attachedAtSeconds = -1.0;
        // 版本行是计算属性（含非常量 BuildCommit）：构造期拼一次就冻住。帧内再取一次就是一次字符串分配
        //（+ int 装箱），而帧预算的托管分配上限是 0 B。
        private readonly string _versionLine;

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
        // 上屏：布局模型（Ac.UI，可无头单测）+ 薄 IMGUI 适配层（Ac.Boot，只在有显示设备时才画）
        public OverlayModel Overlay { get; private set; }
        public OverlayRenderer OverlayRenderer { get; private set; }
        // 布局模型被产出过多少次（用例用它证明"接上了"，也证明 TickOverlay 没有偷偷每帧做这件事）
        public int OverlayBuilds { get; private set; }

        public bool MaterialsReady { get { return _sheepMaterial != null; } }
        public int FxTicks { get; private set; }
        // overlay 的计数与打点同义：只有该段真的做功（切了相位 / 刷了面板）的帧才 +1
        public int OverlayTicks { get; private set; }
        public int DrawTicks { get; private set; }
        public int HitFxCount { get; private set; }
        // 本地开火当帧射出的曳光条数 / 被权威命中点改写终点的条数。用例靠它们区分"开火真的出图了"
        // 与"只播了个动画"，也钉住"命中回执确实回填到了曳光上"。
        public int LocalTracerSpawns { get; private set; }
        public int HitRetargets { get; private set; }
        public int CameraPoseUpdates { get; private set; }
        public int WrittenSheepCount { get; private set; }
        public int CulledSheepCount { get; private set; }
        public int DrawnSheepCount { get; private set; }
        public int SubmittedDraws { get; private set; }
        // C14 帧基准的 triangles 度量来源：引擎的渲染统计（ProfilerCategory.Render 的 Draw Calls/Triangles
        // Count 与 UnityEditor.UnityStats）在 -batchmode 下恒为 0（探针实测），所以三角形只能"数提交"。
        // 这里累加的是本帧真正提交给渲染的网格三角形数（羊身 + 额标 × 实例数），不写凑数常量。
        public int SubmittedTriangles { get; private set; }
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
            _versionLine = VersionInfo.VersionLine;
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
                _sheepTriangles[form] = _sheepMeshes[form].triangles.Length / 3;
                _emblemTriangles[form] = _emblemMeshes[form].triangles.Length / 3;
            }
            _sheepMaterial = ArenaMaterials.CreateSheep("Ac/Sheep");
            // 额标按羊形一份：问界羊/羊王自发光（《§5(d) 配色），出包里“额标一亮就知道该打谁”。
            for (var form = 0; form < SheepMesh.FormCount; form++)
            {
                var kind = (SheepKind)form;
                var emblem = MakeInstancedMaterial("Ac/Emblem" + form, SheepMesh.EmblemColorPure(kind));
                var glow = kind == SheepKind.Elite || kind == SheepKind.King;
                if (glow) MakeEmissive(emblem, kind == SheepKind.King ? 1.6f : 1.1f);
                _emblemMaterials[form] = emblem;
            }

            // ④b 弹药补给箱（S16）：程序化立方体池，实体（kind==3）进视图就摆、离场就收。
            Crates = new CrateVisuals();
            var crateRoot = new GameObject(CrateRootName);
            crateRoot.transform.SetParent(_root.transform, false);
            _crateMaterial = ArenaMaterials.CreateInstanced("Ac/Crate", ArtPalette.Hex(0x9A7B4F));
            var crateMesh = BuiltinCubeMesh();
            for (var i = 0; i < CrateVisuals.MaxCrates; i++)
            {
                var part = new GameObject(CratePartName + i);
                part.transform.SetParent(crateRoot.transform, false);
                part.AddComponent<MeshFilter>().sharedMesh = crateMesh;
                var renderer = part.AddComponent<MeshRenderer>();
                renderer.sharedMaterial = _crateMaterial;
                renderer.enabled = false;
                _crateObjects[i] = part;
            }

            // ⑤ 特效与视图模型：Effects 的池在构造期分配，帧内不再分配
            Effects = new Effects();
            ViewModel = new ViewModel();
            ViewModel.Attach(MainCamera);
            BuildWeaponView();

            // ⑥ 屏幕流与调试面板
            Flow = new ScreenFlow();
            DebugPanel = new DebugPanel();
            Flow.Lobby.OnPhaseChanged += OnPhaseChanged;
            // 上屏：HUD/准星/大厅/结算/波间/调试面板此前都是纯数据类，全仓没有一个 OnGUI
            //（"装配了但没渲染器"）。布局模型挂在装配根下由适配层的 OnGUI 读取。
            Overlay = new OverlayModel();
            var overlayObject = new GameObject(OverlayRenderer.RendererName);
            overlayObject.transform.SetParent(_root.transform, false);
            OverlayRenderer = overlayObject.AddComponent<OverlayRenderer>();
            OverlayRenderer.Bind(this);

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
            _attachedAtSeconds = Time.realtimeSinceStartup;
            if (loop == null) return;
            loop.EventApplied = OnEventApplied;
            // 大厅昵称经 Lobby 清洗后交给帧回路上报 Join；本地身份独立来自 MatchState.localPid。
            // 先减后加：Attach 可以重复调用（Start 不是一次性的），订阅只许留一条。
            Flow.Lobby.OnNameChanged -= OnLobbyNameChanged;
            Flow.Lobby.OnNameChanged += OnLobbyNameChanged;
            // S16：波间升级面板的乐观购买 → 上行 UpgradeSelect（发送/失败计数在 GameLoop 上）。
            Flow.Upgrades.OnUpgrade -= OnUpgradeSelected;
            Flow.Upgrades.OnUpgrade += OnUpgradeSelected;
            // 空名字不许覆盖：GameBootstrap 在 Attach 之后补默认昵称，而 Attach 会被重复调用（Start 不是一次性的）
            if (!string.IsNullOrEmpty(Flow.Lobby.Name)) loop.LocalName = Flow.Lobby.Name;
            _seenMatchStates = loop.MatchStateCount;
            if (loop.MatchStateCount > 0) Flow.Apply(loop.LastMatchState, loop.LocalPlayerId);
        }

        // S16：升级面板买了一张卡 → 帧回路上行 UpgradeSelect（发送失败由权威 MatchState 拉回）。
        private void OnUpgradeSelected(int upgradeId)
        {
            if (_loop != null) _loop.BuyUpgrade(upgradeId);
        }

        public void ApplySettings(in SettingsSnapshot settings)
        {
            Fps.ApplyFov(settings.Fov);
            if (MainCamera != null) MainCamera.fieldOfView = (float)Fps.Fov;
            Batching.SetQualityTier(settings.QualityTier);
            Effects.SetReducedMotion(settings.ReduceMotion);
            // H5：crosshairColor / colorblindSafe 此前只被序列化、没有任何行为读者，准星的颜色就写在这里
            if (_loop != null && _loop.Hud != null) _loop.Hud.Crosshair.ApplySettings(settings);
        }

        public SettingsPanel SettingsPanel { get; private set; }
        private int _settingsClosedFrame = -1;
        private bool _settingsReleasePending;
        public bool SettingsInputBlocked
        {
            get { return (SettingsPanel != null && SettingsPanel.Visible) || _settingsClosedFrame == Time.frameCount || _settingsReleasePending; }
        }

        public void BindSettings(SettingsStore store)
        {
            if (SettingsPanel == null || !ReferenceEquals(SettingsPanel.Store, store)) SettingsPanel = new SettingsPanel(store);
        }

        public void ToggleSettings()
        {
            if (SettingsPanel == null) return;
            SettingsPanel.Toggle();
            if (SettingsPanel.Visible)
            {
                Flow.Chat.Focus(false);
                if (_loop != null && _loop.Sampler != null) _loop.Sampler.Suspend();
            }
            else
            {
                _settingsClosedFrame = Time.frameCount;
                _settingsReleasePending = true;
            }
        }

        public void UpdateSettingsRelease(bool mouseHeld)
        {
            if (!mouseHeld && Time.frameCount != _settingsClosedFrame) _settingsReleasePending = false;
        }

        public void ToggleDebugPanel() { DebugPanel.Toggle(); }

        // 布局模型的输入：全部从既有对象读，本层不推相位、不算身份（OverlayModel 也不自己算）。
        public OverlaySources Sources()
        {
            var sources = default(OverlaySources);
            var loop = _loop;
            var flow = Flow;
            sources.Hud = loop == null ? null : loop.Hud;
            sources.Lobby = flow == null ? null : flow.Lobby;
            sources.Intermission = flow == null ? null : flow.Intermission;
            sources.Upgrades = flow == null ? null : flow.Upgrades;
            sources.Results = flow == null ? null : flow.Results;
            sources.Debug = DebugPanel;
            sources.Chat = flow == null ? null : flow.Chat;      // 局内聊天（显隐由 GameLoop 按相位驱动）
            sources.Players = loop == null ? null : loop.LastMatchState.Players;
            sources.SelfPid = loop == null ? 0 : loop.LocalPlayerId;
            sources.Connected = loop != null && loop.IsConnected;
            sources.ReconnectAttempts = loop == null ? 0 : loop.ReconnectAttempts;
            return sources;
        }

        // 产出一帧绘制项。OnGUI（真上屏）与无头用例走的是同一条路径，所以用例断言的就是生产路径。
        public int BuildOverlay(int widthPx, int heightPx)
        {
            if (_disposed || Overlay == null) return 0;
            var count = Overlay.Build(Sources(), widthPx, heightPx);
            OverlayBuilds += 1;
            return count;
        }

        // 程序化网格的只读取用口：用例要断言"生成出来的网格真的有顶点"，而它们只挂在本层内部
        public Mesh SheepMeshFor(SheepKind kind) { return _sheepMeshes[(int)kind]; }
        public Mesh EmblemMeshFor(SheepKind kind) { return _emblemMeshes[(int)kind]; }
        // 面板 versionLine 字段的来源：构造期拼好的那一份（用例断言它不是每次现拼的）
        public string VersionLine { get { return _versionLine; } }

        // ---- 三段呈现工作（由 GameLoop 按段调用；打点规则见 IFrameWorkSink） ----

        public void TickFx(double dtMs)
        {
            if (_disposed) return;
            FxTicks += 1;
            SyncCamera();
            ViewModel.Refresh();
            // 特效层的开火闸门。此前 `SetAmmoGate` / `SetFireIntervalMs` **没有任何生产调用者**：
            // AmmoGate 恒 0 ⇒ `Effects.SpawnTracer` 每一发都走 RejectedShots 分支，曳光在出包里
            // 一条都不会出现（枪口火焰来自坐力曲线，所以"能看到闪、看不到弹道"）。
            // 闸门取本地武器镜像**开火前**的弹匣：本帧已经打出去的那一发要加回来，否则"最后一发"的
            // 曳光会被自己的"打完归零"闸掉。射速间隔取 kWeapons 镜像表（与服务端同一条公式）。
            var gateLoop = _loop;
            if (gateLoop != null)
            {
                Effects.SetAmmoGate(gateLoop.Weapon.ActiveMag + (gateLoop.LocalShotFired ? 1 : 0));
                Effects.SetFireIntervalMs(WeaponTable.FireIntervalMs(gateLoop.Weapon.Slot, 1.0f));
            }
            Effects.Tick((float)dtMs);
            UpdateWeaponView(dtMs);
            TracerView.Sync(Effects.Tracers, gateLoop != null && gateLoop.CombatVisible);
            SyncCrates();
        }

        public bool TickOverlay(double dtMs)
        {
            if (_disposed) return false;
            var loop = _loop;
            // 没接线（单机 / 帧基准）：屏幕流与面板都没有输入源，这一段**什么都不做** ⇒ 不打点。
            // 旧写法无条件 return true，等于给一个空段每帧发一张"预算内"的通行证（审查点名的假绿）。
            if (loop == null) return false;

            var worked = false;
            if (loop.MatchStateCount != _seenMatchStates)
            {
                _seenMatchStates = loop.MatchStateCount;
                Flow.Apply(loop.LastMatchState, loop.LocalPlayerId);
                worked = true;
            }
            // 面板不可见就不采样：Sample() 要读版本行、分位与实例计数，而 C13 §5 的语义本来就是
            // "不可见不刷新"（DebugPanel.Tick 立刻 return）。关着面板还每帧算统计是白烧。
            // 可见时也只在**到点的那一帧**造样本（250ms 一次），不是每帧——分位是 O(n log n)。
            if (DebugPanel.Visible)
            {
                _elapsedMs += dtMs;                       // 供快照速率用（面板自己的节拍在 RefreshDue 里）
            // 快照率 = 最近 1 秒里真正应用了多少帧（原来是生命周期计数 ÷ 会话时长，
            // 开局那几秒会算出 100~700/s 的虚高值）。
            var loopForRate = _loop;
            if (loopForRate != null)
            {
                _rateWindowMs += dtMs;
                _rateWindowSamples += loopForRate.SnapshotsApplied - _rateWindowApplied;
                _rateWindowApplied = loopForRate.SnapshotsApplied;
                if (_rateWindowMs >= SnapshotRateWindowMs)
                {
                    _lastRateHz = (float)(_rateWindowSamples / (_rateWindowMs / 1000.0));
                    _rateWindowMs = 0.0;
                    _rateWindowSamples = 0;
                }
            }
                var sample = DebugPanel.RefreshDue((float)dtMs) ? Sample() : default(DebugSample);
                DebugPanel.Tick((float)dtMs, sample);
                worked = true;
            }
            if (worked) OverlayTicks += 1;
            return worked;
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
            var triangles = 0;
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
                            Graphics.DrawMeshInstanced(_emblemMeshes[form], 0, EmblemMaterial(form), _emblemBatch, n);
                            submitted += 2;
                            triangles += n * (_sheepTriangles[form] + _emblemTriangles[form]);
                        }
                    }
                    else
                    {
                        for (var i = 0; i < count; i++)
                        {
                            Graphics.DrawMesh(_sheepMeshes[form], _bodyBatch[i], _sheepMaterial, WorldLayer);
                            Graphics.DrawMesh(_emblemMeshes[form], _emblemBatch[i], EmblemMaterial(form), WorldLayer);
                        }
                        submitted += 2 * count;
                        triangles += count * (_sheepTriangles[form] + _emblemTriangles[form]);
                    }
                    drawn += count;
                }
            }
            DrawnSheepCount = drawn;
            SubmittedDraws = submitted;
            SubmittedTriangles = triangles;
            // 没有提交就不返回 true ⇒ GameLoop 不给 draw 打点（见 IFrameWorkSink）
            return submitted > 0;
        }

        public void Dispose()
        {
            if (_disposed) return;
            _disposed = true;
            // 三条缝全部摘干净：只摘 EventApplied 的话，销毁后 GameLoop 每帧还在给 fx/overlay/draw
            // 打≈0 的点——等于让这三段的预算永远"通过"（审查点名的假绿）。
            if (_loop != null)
            {
                if (_loop.EventApplied != null) _loop.EventApplied -= OnEventApplied;
                if (ReferenceEquals(_loop.Fx, _fxSink)) _loop.Fx = null;
                if (ReferenceEquals(_loop.Draw, _drawSink)) _loop.Draw = null;
                if (ReferenceEquals(_loop.Overlay, _overlaySink)) _loop.Overlay = null;
            }
            // §5.6：销毁时把指针锁复位。锁着指针退出（或退出 Play）会留下一个点不动的鼠标。
            if (Fps != null) Fps.ReleasePointerLock();
            Flow.Lobby.OnPhaseChanged -= OnPhaseChanged;
            Flow.Lobby.OnNameChanged -= OnLobbyNameChanged;
            Flow.Upgrades.OnUpgrade -= OnUpgradeSelected;
            Batching.SetQualityTier(_savedTier);
            Kill(_sheepMaterial);
            Kill(_crateMaterial);
            Kill(_emblemMaterial);
            for (var form = 0; form < _emblemMaterials.Length; form++) Kill(_emblemMaterials[form]);
            if (_arenaMaterials != null) for (var i = 0; i < _arenaMaterials.Length; i++) Kill(_arenaMaterials[i]);
            if (_textures != null) for (var i = 0; i < _textures.Length; i++) Kill(_textures[i]);
            for (var i = 0; i < _arenaParts.Length; i++) Kill(_arenaParts[i]);
            for (var i = 0; i < _sheepMeshes.Length; i++) { Kill(_sheepMeshes[i]); Kill(_emblemMeshes[i]); }
            TracerView.Dispose();
            for (var i = 0; i < WeaponMesh.SlotCount; i++)
            {
                Kill(_weaponBodies[i]);
                Kill(_weaponAccents[i]);
            }
            Kill(_root);
            // 适配层是与根一起被销毁的（OnDestroy 里回收它自己建的纹理）：这里把引用也断掉，
            // 免得用例拿到一个"Unity 假 null"的组件。
            OverlayRenderer = null;
        }

        // ---- 内部 ----

        // 事件 → 特效：此前事件只有 HUD 一个消费者，Effects.SpawnHit 在生产里零调用。
        private void OnEventApplied(HudEvent hudEvent)
        {
            if (hudEvent.Type != Ac.Net.EventType.PlayerHit && hudEvent.Type != Ac.Net.EventType.SheepKilled) return;
            var loop = _loop;
            // 权威命中点（S09 的 i16 厘米）优先：它才是"子弹真正打中的那一点"（头/胸/腿），
            // 拿目标实体的中心当锚点会让爆头反馈落在身体中段。三个分量全 0 = 这条事件没带命中点。
            var hasPoint = hudEvent.Type == Ac.Net.EventType.PlayerHit
                && (hudEvent.HitX != 0 || hudEvent.HitY != 0 || hudEvent.HitZ != 0);
            Vector3 position;
            if (hasPoint)
            {
                position = new Vector3(
                    (float)Quantize.DequantizePosition(hudEvent.HitX),
                    (float)Quantize.DequantizePosition(hudEvent.HitY),
                    (float)Quantize.DequantizePosition(hudEvent.HitZ));
                // 自己打出去的那一枪：把最近一段曳光的终点从"视线外推 30m"改到权威命中点上。
                if (loop != null && hudEvent.SubjectId == loop.LocalPlayerId && Effects.RetargetNewestTracer(position))
                {
                    HitRetargets += 1;
                }
            }
            else
            {
                position = MainCamera == null ? Vector3.zero : MainCamera.transform.position;
                EntityView target;
                if (hudEvent.TargetId > 0 && _loop != null && _loop.Views.TryGet((ushort)hudEvent.TargetId, out target) && target != null)
                {
                    position = new Vector3((float)target.RenderX, (float)target.RenderY, (float)target.RenderZ);
                }
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

        // 大厅昵称 → 帧回路的本地身份。名字一变就重解析；名字没变不会走到这里（Lobby.SetName 去重）。
        private void OnLobbyNameChanged(string name)
        {
            if (_loop != null) _loop.LocalName = name;
        }

        // 「本地身份未绑定」这条诊断的宽限期（秒）：Hello → HelloAck → kJoin → 首条 MatchState(1 Hz)
        // 全在这个窗口内完成。起跑线上 LocalPlayerId 必然为 0，所以第一帧就报等于每次正常启动都喊一次狼来了。
        public const double NoIdentityWarnSeconds = 5.0;

        // 纯函数：什么时候才该报"身份没绑上"。`elapsedSeconds` 负值（还没装配）不报。
        public static bool ShouldWarnNoIdentity(double elapsedSeconds, bool alreadyWarned)
        {
            return !alreadyWarned && elapsedSeconds >= NoIdentityWarnSeconds;
        }

        private void SyncCamera()
        {            var loop = _loop;
            if (loop == null || MainCamera == null) return;
            // 认领不到本地身份时给一条可见诊断：相机不跟人 = 画面诡异 + 键鼠不驱动任何实体，
            // 而屏幕上看不出原因（联调外的实测现象：大厅昵称为空 ⇒ 玩家表里没有这一行）。
            // **但起跑线上必然认领不到**（Hello→HelloAck→kJoin→首条 MatchState(1 Hz) 才带回玩家表），
            // 所以加一条宽限期：实测真机出包里原来第一帧就报，等于每次正常启动都喊一次狼来了。
            if (loop.LocalPlayerId == 0)
            {
                if (ShouldWarnNoIdentity(Time.realtimeSinceStartup - _attachedAtSeconds, _warnedNoIdentity))
                {
                    _warnedNoIdentity = true;
                    Debug.LogWarning("Ac.Boot: 本地身份未绑定（大厅昵称=\"" + (loop.LocalName ?? string.Empty) +
                        "\" 未出现在 MatchState 玩家表里）⇒ 相机停在装配原点、键鼠不驱动任何实体");
                }
                return;
            }
            if (!_identityBoundLogged)
            {
                _identityBoundLogged = true;
                Debug.Log("Ac.Boot: 本地身份已绑定 pid=" + loop.LocalPlayerId +
                    "（大厅昵称=\"" + (loop.LocalName ?? string.Empty) + "\"）");
            }
            // 非对局相位一律用转播机位：服务端在局间保留上一局的实体（自己的“尸体”也在），
            // 跟着它会把镜头放到一个趴在地上的旧位置（出包实测：大厅画面是一堵墙、
            // 还有一次整幅上下翻转）。
            if (!loop.CombatVisible)
            {
                ApplyLobbyCameraPose();
                return;
            }
            EntityView local;
            if (!loop.Views.TryGet(loop.LocalPlayerId, out local) || local == null)
            {
                // 认领到了 pid 但世界里还没有本地实体（大厅等开局、加载相位）：
                // 不能就停在装配原点（那里在谷仓内部，画面是一片壁墙），
                // 给一条固定机位：1 号出生点 + 眼高，朝向场地中心。
                ApplyLobbyCameraPose();
                return;
            }
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

        // S16：把实体视图里的补给箱（kind==3）收进纯模型，再摆 Unity 立方体池（跟随权威快照生灭）。
        private void SyncCrates()
        {
            var crates = Crates;
            if (crates == null) return;
            crates.BeginFrame();
            var views = _loop == null ? null : _loop.Views;
            if (views != null)
            {
                for (var i = 0; i < views.ActiveCount; i++)
                {
                    EntityView view;
                    if (!views.TryGet(views.ActiveIdAt(i), out view) || view == null) continue;
                    crates.Write(view);
                }
            }
            for (var i = 0; i < CrateVisuals.MaxCrates; i++)
            {
                var part = _crateObjects[i];
                if (part == null) continue;
                var renderer = part.GetComponent<MeshRenderer>();
                if (i < crates.Count)
                {
                    var instance = crates.Instances[i];
                    // 服务端拾取物实体在 y=0；0.7m 立方底贴地 ⇒ 中心抬高半高。
                    part.transform.SetPositionAndRotation(
                        new Vector3(instance.X, instance.Y + CrateSizeM / 2f, instance.Z),
                        Quaternion.identity);
                    renderer.enabled = true;
                }
                else
                {
                    renderer.enabled = false;
                }
            }
        }

        // 内置单位立方体网格（CreatePrimitive 的共享内置资产，销毁临时 GameObject 不动网格本体）。
        private static Mesh BuiltinCubeMesh()
        {
            var cube = GameObject.CreatePrimitive(PrimitiveType.Cube);
            var mesh = cube.GetComponent<MeshFilter>().sharedMesh;
            if (Application.isPlaying) UnityEngine.Object.Destroy(cube);
            else UnityEngine.Object.DestroyImmediate(cube);
            return mesh;
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
            sample.VersionLine = _versionLine;
            var loop = _loop;
            if (loop == null) return sample;
            sample.PlayerTick = loop.LocalSteps;
            sample.ServerTick = (int)loop.View.AppliedTick;
            // 传输接上了才有网络四项；离线时老老实实报 n/a（HasNetwork=false）
            var transport = loop.Transport;
            sample.HasNetwork = transport != null;
            if (transport != null)
            {
                var stats = transport.Stats;
                sample.PingMs = (float)stats.RttMs;
                sample.LossPercent = stats.PacketLossPermille / 10f;
                sample.InboundBytesPerSec = (float)stats.BytesInPerSec;
                sample.OutboundBytesPerSec = (float)stats.BytesOutPerSec;
            }
            sample.HasFrameTimes = loop.Profiler.FilledFrames > 0;
            sample.FrameTimeP95Ms = loop.Profiler.FrameP95Ms();
            // 剖析器没有"最大值"口径，用 P99 顶上（面板的告警阈值本来就是 FrameBudget.FrameP99BudgetMs）
            sample.FrameTimeMaxMs = loop.Profiler.FrameP99Ms();
            sample.HasViewCounts = true;
            sample.EntityCount = loop.Views.ActiveCount;
            sample.PooledCount = SheepPool.Count;
            sample.HasSnapshotRate = _elapsedMs > 0.0 && loop.SnapshotsApplied > 0;
            // 快照率由 TickFx 的 1 秒窗口维护（这里只读）。
            sample.SnapshotRateHz = _lastRateHz;
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

        // 实例化材质走 ArenaMaterials（唯一出口）：着色器来自对材质资产的引用，不再用 Shader.Find 按名字查
        // —— 按名字查在出包时会被剥离，player 里一个材质都造不出来（ADR-014 §后果-1 的正式修法）。
        private static Material MakeInstancedMaterial(string name, Color32 color)
        {
            return ArenaMaterials.CreateInstanced(name, color);
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

        // 视图模型：程序化网格（ADR-003 零素材）+挂在相机下的基座，
        // 加上待机摆动、行走摆动、开火后坐力与枪口火花。
        private void BuildWeaponView()
        {
            if (MainCamera == null) return;
            _weaponObject = new GameObject("Ac.WeaponView");
            _weaponObject.transform.SetParent(MainCamera.transform, false);
            _weaponObject.transform.localPosition = WeaponBasePosition();
            _weaponObject.transform.localScale = new Vector3(WeaponModelScale, WeaponModelScale, WeaponModelScale);
            _weaponObject.transform.localRotation = Quaternion.Euler(3f, -13f, 0f);   // 侧转：让枪身侧面轮廓（而不是枪口正对镜头）进画面
            _weaponFilter = _weaponObject.AddComponent<MeshFilter>();
            _weaponRenderer = _weaponObject.AddComponent<MeshRenderer>();
            // 配件体：挂在枪体下的子对象，自动继承摆动/后坐
            _weaponAccentObject = new GameObject("Ac.WeaponAccent");
            _weaponAccentObject.transform.SetParent(_weaponObject.transform, false);
            _weaponAccentFilter = _weaponAccentObject.AddComponent<MeshFilter>();
            _weaponAccentRenderer = _weaponAccentObject.AddComponent<MeshRenderer>();
            var accentMaterial = MakeInstancedMaterial("Ac/WeaponAccent", new Color32(0x9A, 0x9E, 0xA8, 255));
            accentMaterial.color = new Color(0.60f, 0.62f, 0.66f, 1f);
            MaterialCullOff(accentMaterial);
            _weaponAccentRenderer.sharedMaterial = accentMaterial;
            _weaponAccentRenderer.shadowCastingMode = UnityEngine.Rendering.ShadowCastingMode.Off;
            _weaponAccentRenderer.receiveShadows = false;
            // 枪口动态光：开火那几帧亮起来（点光，距离短）
            var lightObject = new GameObject("Ac.MuzzleLight");
            lightObject.transform.SetParent(_weaponObject.transform, false);
            lightObject.transform.localPosition = WeaponMesh.MuzzleLocal;
            _muzzleLight = lightObject.AddComponent<Light>();
            _muzzleLight.type = LightType.Point;
            _muzzleLight.range = 7f;
            _muzzleLight.color = new Color(1f, 0.82f, 0.45f);
            _muzzleLight.intensity = 0f;
            // 抛壳：一个小方块，开火时从抛壳口飞出去，自然落下
            _shellObject = new GameObject("Ac.Shell");
            _shellObject.transform.SetParent(_weaponObject.transform, false);
            _shellObject.transform.localScale = new Vector3(0.018f, 0.018f, 0.028f);
            var shellFilter = _shellObject.AddComponent<MeshFilter>();
            var shellRenderer = _shellObject.AddComponent<MeshRenderer>();
            shellFilter.sharedMesh = BuildFlashMesh();
            var shellMaterial = MakeInstancedMaterial("Ac/Shell", new Color32(0xD8, 0xB0, 0x54, 255));
            shellMaterial.color = new Color(0.85f, 0.69f, 0.33f, 1f);
            MaterialCullOff(shellMaterial);
            shellRenderer.sharedMaterial = shellMaterial;
            shellRenderer.shadowCastingMode = UnityEngine.Rendering.ShadowCastingMode.Off;
            shellRenderer.receiveShadows = false;
            _shellObject.SetActive(false);
            var material = MakeInstancedMaterial("Ac/Weapon", new Color32(0x55, 0x57, 0x5E, 255));
            material.color = new Color(0.42f, 0.43f, 0.47f, 1f);
            // 双面：手写长方体的绕序难免有个别面朝里，单面会看到“空心的枪”
            MaterialCullOff(material);
            _weaponRenderer.sharedMaterial = material;
            _weaponRenderer.shadowCastingMode = UnityEngine.Rendering.ShadowCastingMode.Off;
            _weaponRenderer.receiveShadows = false;
            for (var slot = 0; slot < WeaponMesh.SlotCount; slot++)
            {
                _weaponBodies[slot] = WeaponMesh.BuildBody(slot);
                _weaponAccents[slot] = WeaponMesh.BuildAccent(slot);
            }
            TracerView = new TracerRenderer(_root.transform);
            SetWeaponSlot(0);

            // 枪口火花：一个小方块，平时隐藏，开火那几帧亮起来
            _muzzleFlash = new GameObject("Ac.MuzzleFlash");
            _muzzleFlash.transform.SetParent(_weaponObject.transform, false);
            _muzzleFlash.transform.localPosition = WeaponMesh.MuzzleLocal;
            _muzzleFlash.transform.localScale = new Vector3(0.055f, 0.055f, 0.055f);
            var flashFilter = _muzzleFlash.AddComponent<MeshFilter>();
            var flashRenderer = _muzzleFlash.AddComponent<MeshRenderer>();
            flashFilter.sharedMesh = BuildFlashMesh();
            var flashMaterial = MakeInstancedMaterial("Ac/MuzzleFlash", new Color32(0xFF, 0xC2, 0x4D, 255));
            flashMaterial.color = new Color(1f, 0.78f, 0.30f, 1f);
            MaterialCullOff(flashMaterial);
            flashRenderer.sharedMaterial = flashMaterial;
            flashRenderer.shadowCastingMode = UnityEngine.Rendering.ShadowCastingMode.Off;
            _muzzleFlash.SetActive(false);
        }

        private static Mesh BuildFlashMesh()
        {
            var mesh = new Mesh();
            mesh.name = "MuzzleFlash";
            mesh.vertices = new[]
            {
                new Vector3(-0.5f, -0.5f, 0f), new Vector3(0.5f, -0.5f, 0f),
                new Vector3(0.5f, 0.5f, 0f), new Vector3(-0.5f, 0.5f, 0f),
            };
            mesh.SetTriangles(new[] { 0, 1, 2, 0, 2, 3 }, 0);
            mesh.RecalculateNormals();
            mesh.RecalculateBounds();
            return mesh;
        }

        // 切换预建网格，跟随已接受权威校正的本地预测武器。
        private void SetWeaponSlot(int slot)
        {
            if (slot < 0 || slot >= WeaponMesh.SlotCount) slot = 0;
            if (slot == _weaponSlotBuilt || _weaponFilter == null) return;
            _weaponSlotBuilt = slot;
            _weaponFilter.sharedMesh = _weaponBodies[slot];
            if (_weaponAccentFilter != null) _weaponAccentFilter.sharedMesh = _weaponAccents[slot];
        }

        // 每帧：待机摆动 + 行走摆动 + 本地开火信号驱动坐力。
        private void UpdateWeaponView(double dtMs)
        {
            if (_weaponObject == null) return;
            var inCombat = _loop != null && _loop.CombatVisible;
            if (_weaponObject.activeSelf != inCombat) _weaponObject.SetActive(inCombat);
            if (!inCombat) return;
            _weaponSwayMs += dtMs;
            var loop = _loop;
            // 开火信号 = 本地武器镜像的**当帧**结果（C06 §5(c)）。此前这里是"弹匣变小"这条推断：
            // 权威弹匣是 1Hz 的（1 秒才掉一次数），推理出来的开火要等最多一秒才见枪口火焰与坐力；
            // 本地账接上之后 mag 也不再可靠（打空枪、被拒收都会动它）。现在按下那一帧就响。
            if (loop != null && loop.LocalShotFired)
            {
                _weaponKick01 = 1f;
                SpawnLocalShotTracer();
            }
            if (_weaponKick01 > 0f)
            {
                _weaponKick01 -= (float)(dtMs / 180.0);      // 180ms 衰减完
                if (_weaponKick01 < 0f) _weaponKick01 = 0f;
            }

            // 行走摆动的幅度来自真实移动轴（GameLoop 把采样到的最后一条意图曝出来）
            var moving = loop != null && (Mathf.Abs((float)loop.LastMoveX) > 0.01f || Mathf.Abs((float)loop.LastMoveY) > 0.01f);
            _bobPhase += (float)(dtMs / 1000.0) * (moving ? 9.0f : 1.6f);
            var bobAmp = moving ? 0.012f : 0.0025f;
            var swayX = Mathf.Sin(_bobPhase) * bobAmp;
            var swayY = Mathf.Sin(_bobPhase * 2f) * bobAmp * 0.6f;
            var kickBack = _weaponKick01 * 0.035f;
            var kickUp = _weaponKick01 * 0.012f;
            var baseOffset = WeaponBasePosition();
            _weaponObject.transform.localPosition = new Vector3(
                baseOffset.x + swayX, baseOffset.y + swayY + kickUp, baseOffset.z - kickBack);
            _weaponObject.transform.localRotation = Quaternion.Euler(
                3f - _weaponKick01 * 9f, -13f + swayX * 40f, _weaponKick01 * 4f);
            var flashOn = _weaponKick01 > 0.55f;
            if (_muzzleFlash != null) _muzzleFlash.SetActive(flashOn);
            if (_muzzleLight != null) _muzzleLight.intensity = flashOn ? 3.2f : 0f;
            UpdateShell(dtMs);

            if (loop != null) SetWeaponSlot(loop.Weapon.Slot);
        }

        // 开火当帧的曳光：起点 = 枪口挂点（**不是眼位**，C09 §5(c) 要修的 v1 缺陷），方向 = 当前视线，
        // 终点先按 30m 外推（Tracer.FallbackEnd）。这里**不做命中判定**——服务端的 PlayerHit 一到，
        // OnEventApplied 会把最近这一段重新指向权威命中点：打 3m 外的羊时那条 30m 长条只存在 1–3 帧。
        private void SpawnLocalShotTracer()
        {
            if (MainCamera == null) return;
            ViewModel.OnFire();   // 刷新枪口挂点（相机空间的挂点，帧内相机刚动过）
            if (Effects.SpawnTracerFromMuzzle(ViewModel, MainCamera.transform.forward, false, Vector3.zero))
                LocalTracerSpawns += 1;
        }

        // 视图模型基座：锚点的横向偏移按 0.55 收进去（实测：直接用 0.17m 会把整枪顶到右缘外），
        // 并把整枪沿 +Z 前推 WeaponForwardM（枪口落在 ViewModel.MuzzleOffset.z 附近）。
        private static Vector3 WeaponBasePosition()
        {
            var anchor = ViewModel.BaseOffset;
            return new Vector3(anchor.x * 0.60f, anchor.y * 0.62f, anchor.z + WeaponForwardM);
        }

        // 大厅/加载相位的固定机位（1 号出生点，眼高 1.6m，朝向场地中心）。
        private void ApplyLobbyCameraPose()
        {
            // 大厅没有本地实体时的“转播机位”：站在 -Z 侧、抬高 3m（SetPose 自己加眼高）、
            // 俯视场地中心。这里不用 yaw=π：出包实测有一次整幅画面上下翻转（拉丁与俯仰的极点），
            // 从 -Z 朝 +Z 看是同一个场地，不会撞上这个极点。
            Fps.SetPose(0f, 3.0f, -30.0f, 0f, -0.18f);
        }

        // URP Lit 的 “Cull Off”（只改材质实例，不动共享 shader）
        private static void MaterialCullOff(Material material)
        {
            if (material == null) return;
            material.SetFloat("_Cull", 0f);
        }

        private Material EmblemMaterial(int form)
        {
            var material = _emblemMaterials[form];
            return material != null ? material : _emblemMaterial;
        }

        // URP Lit 的自发光（只动材质实例，不碰共享 shader）
        private static void MakeEmissive(Material material, float intensity)
        {
            if (material == null || !material.HasProperty("_EmissionColor")) return;
            var color = material.color;
            material.EnableKeyword("_EMISSION");
            material.SetColor("_EmissionColor", new Color(color.r * intensity, color.g * intensity, color.b * intensity, 1f));
            material.globalIlluminationFlags = MaterialGlobalIlluminationFlags.RealtimeEmissive;
        }

        // 抛壳：开火时从抛壳口给一个初速度，之后受重力，0.9s 后收回池子。
        private void UpdateShell(double dtMs)
        {
            if (_shellObject == null) return;
            if (_weaponKick01 > 0.85f && !_shellObject.activeSelf)
            {
                _shellObject.SetActive(true);
                _shellObject.transform.localPosition = new Vector3(0.03f, 0.02f, 0.06f);
                _shellVelocity = new Vector3(0.55f, 0.75f, -0.15f);
                _shellLifeMs = 900.0;
            }
            if (!_shellObject.activeSelf) return;
            var dt = (float)(dtMs / 1000.0);
            _shellLifeMs -= dtMs;
            _shellVelocity += new Vector3(0f, -9.0f, 0f) * dt;
            _shellObject.transform.localPosition += _shellVelocity * dt;
            _shellObject.transform.localRotation *= Quaternion.Euler(220f * dt, 160f * dt, 90f * dt);
            if (_shellLifeMs <= 0.0) _shellObject.SetActive(false);
        }
    }
}
