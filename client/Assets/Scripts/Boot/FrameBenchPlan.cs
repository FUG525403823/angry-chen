using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Text;
using Ac.Core;
using Ac.Net;
using Ac.Sim;
using Ac.UI;
using Ac.View;
using Unity.Profiling;
using UnityEngine;

namespace Ac.Boot
{
    // C14 §5 计划场景基准的**公共核心**：装配（GameBootstrap/PresentationLayer）、合成来件、逐帧采样、统计、JSON。
    //
    // 两条机制共用这一份，唯一的差别是"谁来渲染"：
    //   editor（Ac.Tests.FrameBench 调用，ManualRender = true）：-batchmode 下引擎不做任何渲染，Tick 里必须手动
    //     Camera.Render()；这是诊断口径（实测强制渲染有 16~56ms/次、与分辨率无关的地板，见
    //     docs/evidence/client-v2-frame.md 的对照表）。
    //   player（FrameBenchPlayer 调用，ManualRender = false）：出包后的窗口化 player，引擎自己的帧循环渲染，
    //     绝不手动 Camera.Render()；帧时间 = 相邻两次 Update 起点之间的墙钟。
    //
    // 放在 Ac.Boot（运行期程序集）而不是 Ac.Tests：Ac.Tests 是 Editor-only 程序集，player 引用不到，
    // 计划场景的装配逻辑如果待在 Ac.Tests 里，player 就只能复制一份 —— 这正是要避免的。
    public sealed class FrameBenchPlanOptions
    {
        public string Scene = PlanBench.PlanSceneFragment;
        public string OutPath = "";
        public int Warmup = FrameBudget.WarmupFrames;
        public int Sample = FrameBudget.SampleFrames;
        public int Runs = FrameBudget.Runs;
        public int Quality = -1;
        // 空场对照：只让引擎渲染一个空场景（不喂来件、不驱动帧回路），用来量机制地板。
        public bool Empty;
        public string Mechanism = PlanBench.MechanismEditor;

        public static FrameBenchPlanOptions FromArgs(string[] args, string mechanism)
        {
            var options = new FrameBenchPlanOptions { Mechanism = mechanism };
            options.Scene = BenchJson.Arg(args, "-frameBenchScene", PlanBench.PlanSceneFragment);
            options.OutPath = BenchJson.Arg(args, "-frameBenchOut", "");
            options.Warmup = BenchJson.ArgInt(args, "-frameBenchWarmup", FrameBudget.WarmupFrames);
            options.Sample = BenchJson.ArgInt(args, "-frameBenchSample", FrameBudget.SampleFrames);
            options.Runs = BenchJson.ArgInt(args, "-frameBenchRuns", FrameBudget.Runs);
            options.Quality = BenchJson.ArgInt(args, "-frameBenchQuality", -1);
            options.Empty = BenchJson.ArgInt(args, "-frameBenchEmpty", 0) != 0;
            return options;
        }
    }

    // 两条机制共用的 JSON/统计小工具（与之前 Ac.Tests.FrameBench 里的实现逐字一致，避免出现第二套口径）。
    public static class BenchJson
    {
        public static double NowMs()
        {
            return (double)System.Diagnostics.Stopwatch.GetTimestamp() * 1000.0 / System.Diagnostics.Stopwatch.Frequency;
        }

        // 分位数口径与 §5 法则 3 一致：ceil(q*n)-1（FrameProfiler 的备份实现 bench-sim.mjs 同款）。
        public static double Quantile(double[] sorted, double q)
        {
            if (sorted.Length == 0) return -1.0;
            var index = (int)Math.Ceiling(q * sorted.Length) - 1;
            if (index < 0) index = 0;
            if (index >= sorted.Length) index = sorted.Length - 1;
            return sorted[index];
        }

        // 采样序列（未排序）里 [from, from+count) 的中位数：用来区分"稳态就这么慢"与"越过越慢"。
        public static double MedianOf(double[] values, int from, int count)
        {
            if (values.Length == 0 || count <= 0) return -1.0;
            if (from < 0) from = 0;
            if (from >= values.Length) return -1.0;
            if (from + count > values.Length) count = values.Length - from;
            var slice = new double[count];
            Array.Copy(values, from, slice, 0, count);
            Array.Sort(slice);
            return slice[count / 2];
        }

        public static void WriteOut(string outPath, string json)
        {
            if (string.IsNullOrEmpty(outPath)) return;
            var dir = Path.GetDirectoryName(outPath);
            if (!string.IsNullOrEmpty(dir)) Directory.CreateDirectory(dir);
            File.WriteAllText(outPath, json);
        }

        public static void Meta(StringBuilder sb, string key, string value) { Meta(sb, 2, key, value); }
        public static void Meta(StringBuilder sb, int indent, string key, string value)
        {
            sb.Append(' ', indent).Append('"').Append(key).Append("\": \"").Append((value ?? "unknown").Replace("\\", "/").Replace("\"", "'")).Append("\",\n");
        }
        public static void Num(StringBuilder sb, string key, double value) { Num(sb, 2, key, value); }
        public static void Num(StringBuilder sb, int indent, string key, double value)
        {
            var safe = double.IsNaN(value) || double.IsInfinity(value) ? -1.0 : value;
            sb.Append(' ', indent).Append('"').Append(key).Append("\": ").Append(safe.ToString("R", CultureInfo.InvariantCulture)).Append(",\n");
        }
        // 每条 Num 都以 ",\n" 收尾；拼最后一项时要把它连同后面的缩进一起收掉（空对象时不动）。
        public static void TrimLastComma(StringBuilder sb)
        {
            var i = sb.Length - 1;
            while (i >= 0 && char.IsWhiteSpace(sb[i])) i--;
            if (i >= 0 && sb[i] == ',') { sb.Remove(i, sb.Length - i); sb.Append('\n'); }
        }

        public static string Arg(string[] args, string name, string fallback)
        {
            for (var i = 0; i < args.Length - 1; i++) if (args[i] == name) return args[i + 1];
            return fallback;
        }
        public static int ArgInt(string[] args, string name, int fallback)
        {
            int parsed;
            return int.TryParse(Arg(args, name, null), NumberStyles.Integer, CultureInfo.InvariantCulture, out parsed) ? parsed : fallback;
        }
        public static string Safe(Func<string> f, string fallback) { try { var s = f(); return string.IsNullOrEmpty(s) ? fallback : s; } catch { return fallback; } }

        // 渲染证据的计数口径（两条机制共用）：与左上角像素差 >24 的比例。editor 用小 RT 读回喂它，
        // player 用窗口截图喂它 —— 只有"从哪张图上取像素"不同。
        public static double PixelCoverage(Color32[] pixels)
        {
            if (pixels == null || pixels.Length == 0) return -1.0;
            var reference = pixels[0];
            var different = 0;
            for (var i = 0; i < pixels.Length; i++)
            {
                var p = pixels[i];
                var delta = Math.Abs(p.r - reference.r) + Math.Abs(p.g - reference.g) + Math.Abs(p.b - reference.b);
                if (delta > 24) different += 1;
            }
            return (double)different / pixels.Length;
        }

        // 驱动侧显存与活跃 RenderTexture 数：用来排除"越跑越漏"。两条机制都报。
        public static double DriverMemoryMb()
        {
            try { return UnityEngine.Profiling.Profiler.GetAllocatedMemoryForGraphicsDriver() / (1024.0 * 1024.0); }
            catch (Exception) { return -1.0; }
        }

        public static int LiveRenderTextureCount()
        {
            try
            {
                var all = Resources.FindObjectsOfTypeAll<RenderTexture>();
                return all == null ? -1 : all.Length;
            }
            catch (Exception) { return -1; }
        }

        // 从 .git 目录直接读 HEAD，避免在编辑器/player 里起进程。先试 cwd（ps1 从仓库里起进程），
        // 再退回可执行文件所在目录（直接双击 player 时）。
        public static string GitCommit()
        {
            try
            {
                var gitDir = FindGitDir(Directory.GetCurrentDirectory());
                if (gitDir == null) gitDir = FindGitDir(Application.dataPath);
                if (gitDir == null) return "unknown";
                var head = Path.Combine(gitDir, "HEAD");
                if (!File.Exists(head)) return "unknown";
                var text = File.ReadAllText(head).Trim();
                const string prefix = "ref: ";
                if (text.StartsWith(prefix, StringComparison.Ordinal))
                {
                    var refFile = Path.Combine(gitDir, text.Substring(prefix.Length).Replace('/', Path.DirectorySeparatorChar));
                    if (File.Exists(refFile)) text = File.ReadAllText(refFile).Trim();
                    else return "unknown";
                }
                return text.Length >= 7 ? text.Substring(0, 7) : text;
            }
            catch (Exception) { return "unknown"; }
        }

        private static string FindGitDir(string start)
        {
            if (string.IsNullOrEmpty(start)) return null;
            var dir = new DirectoryInfo(start);
            while (dir != null)
            {
                var candidate = Path.Combine(dir.FullName, ".git");
                if (Directory.Exists(candidate)) return candidate;
                dir = dir.Parent;
            }
            return null;
        }
    }

    // 计划场景基准本体。生命周期：Prepare() → Tick() × N（引擎帧循环或同步循环）→ BuildJson()。
    public sealed class PlanBench
    {
        public const string PlanSceneFragment = "4p60sheep";
        public const string MechanismEditor = "editor";
        public const string MechanismPlayer = "player";

        // 60fps 固定步长：预算表本身就是按 60fps 冻结的（P95 ≤ 20ms）。两条机制都用这个 dt 驱动 GameLoop，
        // 这样 player 与 editor 量到的是同一份客户端帧做功。
        public const double DtMs = 1000.0 / 60.0;
        // 服务端 tick = 20Hz（S12 冻结）：MatchState 按这个节奏；战斗事件每帧都来。
        public const int MatchStateEveryFrames = 3;
        public const int DiagnosticsFrames = 120;
        public const int PlayerCount = 4;
        public const int SheepCount = 60;
        public const int EventPayloadBytes = 33;
        // 本地身份：靠 MatchState 里的昵称表认领 pid（Ac.Net.LocalIdentity 的运行期路径）。
        public const string BenchLocalName = "bench";

        private readonly FrameBenchPlanOptions _options;
        private GameLoop _loop;
        private PresentationLayer _pres;
        private Camera _camera;
        private RenderTexture _renderTarget;

        private SnapshotFrame _frame;
        private PacketEncoder _encoder;
        private byte[] _eventPayload;
        private byte[] _matchPayload;
        private PacketHeader _header;
        private uint _snapshotTick = 1u;
        private uint _eventId = 1u;
        private uint _eventTick = 1u;

        private int _arenaRenderers;
        private int _arenaTriangles;
        private int _materialsLive;

        private double[] _frameMs;
        private double[] _feedMs;
        private double[] _loopMs;
        private double[] _renderMs;
        private double[] _workMs;
        private double[] _gapMs;
        private int _measured;

        private long _allocBytes;
        private int _allocUnmeasurable;
        private string _allocReason;
        private int _gc0Delta;
        private int _drawCallsMax;
        private int _trianglesMax;
        private int _particlesMax;
        private int _drawnSheepMax;
        private int _visibleSheepMax;
        private int _skippedSheepMax;
        private int _engineDrawsMax;
        private int _engineTrianglesMax;

        private double _planGcRawBefore;
        private double _planGcStableBefore;
        private double _planGcRawAfter = -1.0;
        private double _planGcStableAfter = -1.0;

        private int _warmupFrames;
        private bool _measuring;
        private bool _finishedWarmup;
        private bool _done;
        private double _prevTickStart = -1.0;
        private bool _staleTickDropped;
        private int _engineFramesStart;
        private int _engineFramesEnd;
        private double[] _engineDeltaMs;
        private int _vSyncBefore;
        private int _targetFpsBefore;
        private string _screenBefore = "?";
        private string _screenAfter = "?";
        private double _pixelCoverage = -1.0;
        private string _pixelNote = "readback unavailable";

        private ProfilerRecorder _drawRecorder;
        private ProfilerRecorder _triRecorder;

        public PlanBench(FrameBenchPlanOptions options)
        {
            _options = options ?? new FrameBenchPlanOptions();
        }

        public FrameBenchPlanOptions Options { get { return _options; } }
        public string Mechanism { get { return _options.Mechanism; } }
        // editor 机制在 Tick 里手动 Camera.Render()；player 机制交给引擎帧循环（绝不手动渲染）。
        public bool ManualRender;
        public GameLoop Loop { get { return _loop; } }
        public PresentationLayer Presentation { get { return _pres; } }
        public Camera Camera { get { return _camera; } }
        public RenderTexture RenderTarget { get { return _renderTarget; } }
        public int ArenaRenderers { get { return _arenaRenderers; } }
        public int ArenaTriangles { get { return _arenaTriangles; } }
        public int MaterialsLive { get { return _materialsLive; } }
        public int DrawCalls { get; private set; }
        public int Triangles { get; private set; }
        public int Particles { get; private set; }
        public int Materials { get; private set; }
        public double FrameP95 { get; private set; }
        public double FrameP99 { get; private set; }
        public double WorkP95 { get; private set; }
        public double FeedP95 { get; private set; }
        public double LoopP95 { get; private set; }
        public double GapP95 { get; private set; }
        public double RenderP95 { get; private set; }
        public double AllocPerFrame { get; private set; }
        public bool AllocMeasured { get; private set; }
        public int Gc0Delta { get { return _gc0Delta; } }
        public int VSyncBefore { get { return _vSyncBefore; } }
        public int TargetFrameRateBefore { get { return _targetFpsBefore; } }
        public string Verdict { get; private set; }
        public int ExitCode { get; private set; }
        public bool Done { get { return _done; } }
        // 采样帧跑满（但还没结算）：机制可以在这之后补渲染证据 / 对照实验，再调 Finish()。
        public bool SampleComplete { get; private set; }
        public bool GraphicsMeasured { get; private set; }
        public double PixelCoverage { get { return _pixelCoverage; } set { _pixelCoverage = value; } }
        public string PixelNote { get { return _pixelNote; } set { _pixelNote = value; } }
        public bool StaleTickDropped { get { return _staleTickDropped; } }
        public string EnvError { get; private set; }
        // 机制专有的 JSON 附加块（editor 的对照实验等）：在标准字段之后追加，末尾逗号由 BuildJson 收尾。
        public List<Action<StringBuilder>> ExtraJson { get { return _extraJson; } }
        private readonly List<Action<StringBuilder>> _extraJson = new List<Action<StringBuilder>>();

        // 装配 + 分辨率 + 渲染目标 + 几何/材质基数。失败时给 ENV 文案（调用方按 §9 判"环境不可用"，不判 PASS）。
        public bool Prepare()
        {
            _screenBefore = Screen.width + "x" + Screen.height;
            // §5 冻结 1920x1080：player 里 SetResolution 真的会开一个 1920x1080 窗口；-batchmode 里它是空操作
            // （Screen 恒 640x480），那时由 ManualRender 把相机渲到 1920x1080 的 RenderTexture 上。
            Screen.SetResolution(1920, 1080, false);
            _screenAfter = Screen.width + "x" + Screen.height;
            if (_options.Quality >= 0) Batching.SetQualityTier(_options.Quality);

            // 运行期装配：GameBootstrap.Start() 造出 Loop + PresentationLayer 并接线 Fx/Draw/Overlay/Audio 与 Sampler。
            // player 里 BeforeSceneLoad 的自身钩子已经跑过一次（这里就什么都不做）；editor -executeMethod 路径下
            // 该钩子不会跑，于是先放一个同名空物体，让 Start() 最后那句 GameObject.Find(RootName) != null 提前返回
            // —— 既避免编辑模式下的 DontDestroyOnLoad（会抛），也保证帧循环由本基准唯一驱动。
            if (GameBootstrap.Loop == null || GameBootstrap.Presentation == null)
            {
                if (GameObject.Find(GameBootstrap.RootName) == null) new GameObject(GameBootstrap.RootName);
                GameBootstrap.Start();
            }
            // 引擎自己的帧循环驱动（GameLoopDriver.Update → loop.Frame(Time.deltaTime)）必须关掉：
            // 本基准按冻结的 60fps 步长驱动同一帧回路，两边同时驱动会让每帧做两次功。
            var drivers = UnityEngine.Object.FindObjectsOfType<GameLoopDriver>();
            for (var i = 0; i < drivers.Length; i++) drivers[i].enabled = false;
            _loop = GameBootstrap.Loop;
            _pres = GameBootstrap.Presentation;
            if (_loop == null || _pres == null || _pres.MainCamera == null || !_pres.MaterialsReady)
            {
                EnvError = "plan scene could not be assembled (loop=" + (_loop != null) + " presentation=" + (_pres != null)
                    + " materialsReady=" + (_pres != null && _pres.MaterialsReady) + ")";
                return false;
            }
            _camera = _pres.MainCamera;
            _loop.LocalName = BenchLocalName;

            if (ManualRender)
            {
                _renderTarget = new RenderTexture(1920, 1080, 24, RenderTextureFormat.ARGB32);
                if (!_renderTarget.Create())
                {
                    EnvError = "no graphics device (1920x1080 RenderTexture could not be created)";
                    return false;
                }
                _camera.targetTexture = _renderTarget;
            }
            else if (_camera.targetTexture != null)
            {
                // player 机制：渲染目标必须是屏幕（引擎帧循环自己渲），不能被上一次运行留下的 RT 粘住。
                _camera.targetTexture = null;
            }

            // §5 冻结契约：VSync off / targetFrameRate 不设限。跑完由调用方（或 Finish）写回原值。
            _vSyncBefore = QualitySettings.vSyncCount;
            _targetFpsBefore = Application.targetFrameRate;
            QualitySettings.vSyncCount = 0;
            Application.targetFrameRate = -1;

            var renderers = _pres.Root == null ? new MeshRenderer[0] : _pres.Root.GetComponentsInChildren<MeshRenderer>(true);
            var liveRenderers = 0;
            for (var i = 0; i < renderers.Length; i++) if (renderers[i].enabled) liveRenderers += 1;
            _arenaTriangles = _pres.ArenaStats.Triangles;
            _materialsLive = CountLiveMaterials();
            if (_options.Empty)
            {
                // 空场对照：关掉场地网格、不喂来件、不驱动帧回路，只让引擎渲一个空场景 —— 量的是机制地板。
                // 相机保持活着（它是 Root 的子物体，整根 SetActive(false) 会把相机也关掉，那就什么都没渲了）。
                for (var i = 0; i < renderers.Length; i++) renderers[i].enabled = false;
                _arenaRenderers = 0;
            }
            else
            {
                _arenaRenderers = liveRenderers;
            }

            StartRecorders();

            _frame = default(SnapshotFrame);
            _encoder = new PacketEncoder();
            _eventPayload = new byte[EventPayloadBytes];
            _matchPayload = BuildMatchStatePayload();
            _header = default(PacketHeader);
            _frameMs = new double[Math.Max(1, _options.Sample)];
            _feedMs = new double[Math.Max(1, _options.Sample)];
            _loopMs = new double[Math.Max(1, _options.Sample)];
            _renderMs = new double[Math.Max(1, _options.Sample)];
            _workMs = new double[Math.Max(1, _options.Sample)];
            _gapMs = new double[Math.Max(1, _options.Sample)];
            _engineDeltaMs = new double[Math.Max(1, _options.Sample)];
            _engineFramesStart = Time.frameCount;
            _engineFramesEnd = _engineFramesStart;
            // 第一帧的帧间隔从"装配完成"算起（否则第一帧记 -1）；预热期不计入样本，装配开销落不进数字。
            _prevTickStart = BenchJson.NowMs();
            return true;
        }

        private void StartRecorders()
        {
            try { _drawRecorder = ProfilerRecorder.StartNew(ProfilerCategory.Render, "Draw Calls Count", 256, ProfilerRecorderOptions.SumAllSamplesInFrame); }
            catch (Exception) { _drawRecorder = default(ProfilerRecorder); }
            try { _triRecorder = ProfilerRecorder.StartNew(ProfilerCategory.Render, "Triangles Count", 256, ProfilerRecorderOptions.SumAllSamplesInFrame); }
            catch (Exception) { _triRecorder = default(ProfilerRecorder); }
        }

        // 场地 6 种 + 羊身 + 额标：非空的才算（材质造不出来时是 null，不能拿常量凑）。
        private int CountLiveMaterials()
        {
            var live = 0;
            var table = _pres.MaterialTable;
            if (table.Grass != null) live += 1;
            if (table.Dirt != null) live += 1;
            if (table.Fence != null) live += 1;
            if (table.BarnWall != null) live += 1;
            if (table.BarnRoof != null) live += 1;
            if (table.Hay != null) live += 1;
            if (_pres.MaterialsReady) live += 2;
            return live;
        }

        // 一帧。editor 机制由基准自己的同步循环调用；player 机制由 MonoBehaviour 的 Update 每帧调用一次。
        public void Tick()
        {
            if (_done || SampleComplete) return;
            var tickStart = BenchJson.NowMs();            // 相邻两次 Update 起点之间的墙钟：player 机制下这就是引擎帧时间（含本帧渲染与 present）。
            var interval = _prevTickStart < 0.0 ? -1.0 : tickStart - _prevTickStart;
            _prevTickStart = tickStart;

            if (!_finishedWarmup && _warmupFrames >= _options.Warmup)
            {
                _finishedWarmup = true;
                AfterWarmup();
            }

            if (_options.Empty)
            {
                // 空场对照：不喂来件、不驱动帧回路，只让引擎渲空场景 —— 量的是机制地板本身。
                if (_finishedWarmup && _measuring && _measured < _options.Sample)
                {
                    _frameMs[_measured] = interval;
                    _feedMs[_measured] = 0.0;
                    _loopMs[_measured] = 0.0;
                    _renderMs[_measured] = -1.0;
                    _workMs[_measured] = 0.0;
                    _gapMs[_measured] = interval;
                    _measured += 1;
                }
                _warmupFrames += 1;
                if (_finishedWarmup && _measured >= _options.Sample) SampleComplete = true;
                return;
            }

            var tFeed0 = BenchJson.NowMs();
            _snapshotTick += 1;
            FeedSnapshot(ref _frame, _snapshotTick);
            if (_warmupFrames % MatchStateEveryFrames == 0) FeedMatchState(_encoder, _matchPayload, ref _header, (uint)(_warmupFrames + 1));
            FeedEvents(_encoder, _eventPayload, ref _header, ref _eventId, _eventTick++);
            var feedCost = BenchJson.NowMs() - tFeed0;

            long allocStart = 0;
            var gc0Start = 0;
            if (_measuring)
            {
                allocStart = AllocWindowStart();
                gc0Start = GC.CollectionCount(0);
            }
            var tLoop0 = BenchJson.NowMs();
            _loop.Frame(DtMs);
            var tLoop1 = BenchJson.NowMs();
            var tRender = tLoop1;
            if (ManualRender) { _camera.Render(); tRender = BenchJson.NowMs(); }
            var tickEnd = tRender;

            _warmupFrames += 1;
            if (!_measuring) return;
            if (_measured >= _options.Sample) { SampleComplete = true; return; }

            _frameMs[_measured] = ManualRender ? (tickEnd - tLoop0 + feedCost) : interval;
            _feedMs[_measured] = feedCost;
            _loopMs[_measured] = tLoop1 - tLoop0;
            _renderMs[_measured] = ManualRender ? (tRender - tLoop1) : -1.0;
            _workMs[_measured] = feedCost + (tLoop1 - tLoop0);
            _gapMs[_measured] = _frameMs[_measured] - _workMs[_measured];
            _engineDeltaMs[_measured] = Time.unscaledDeltaTime * 1000.0;
            _engineFramesEnd = Time.frameCount;

            string reason;
            var bytes = AllocWindowBytes(allocStart, gc0Start, out reason);
            if (bytes < 0) { _allocUnmeasurable += 1; if (_allocReason == null) _allocReason = reason; }
            else _allocBytes += bytes;
            _gc0Delta += GC.CollectionCount(0) - gc0Start;

            var draws = _pres.SubmittedDraws + _arenaRenderers;
            var tris = _pres.SubmittedTriangles + _arenaTriangles;
            var live = _pres.Effects.ParticlePool.LiveCount;
            if (draws > _drawCallsMax) _drawCallsMax = draws;
            if (tris > _trianglesMax) _trianglesMax = tris;
            if (live > _particlesMax) _particlesMax = live;
            if (_pres.DrawnSheepCount > _drawnSheepMax) _drawnSheepMax = _pres.DrawnSheepCount;
            if (_pres.CulledSheepCount > _visibleSheepMax) _visibleSheepMax = _pres.CulledSheepCount;
            if (_pres.Sheep.SkippedCount > _skippedSheepMax) _skippedSheepMax = _pres.Sheep.SkippedCount;
            if (_drawRecorder.Valid && _drawRecorder.CurrentValue > _engineDrawsMax) _engineDrawsMax = (int)_drawRecorder.CurrentValue;
            if (_triRecorder.Valid && _triRecorder.CurrentValue > _engineTrianglesMax) _engineTrianglesMax = (int)_triRecorder.CurrentValue;
            _measured += 1;
            if (_measured >= _options.Sample) SampleComplete = true;
        }

        // 逐帧分配窗口的起点。首选引擎计数器（editor 机制与装了 profiler 的进程）；
        // release player 里没有 profiler 计数器（BenchAlloc.CounterAvailable=false），退回 GC.GetTotalMemory(false)：
        // 同一个窗口、同一个单位（窗口内新分配的托管字节），只是拿不到"本帧分了几次"的分解。
        private long AllocWindowStart()
        {
            return BenchAlloc.CounterAvailable ? BenchAlloc.Begin() : BenchAlloc.ReadHeapBytes();
        }

        // 逐帧分配窗口的读数（窗口 = Begin 到这里的这段代码：合成来件 + GameLoop.Frame，与 editor 机制同窗口）。
        // 退回 GC 堆大小时，窗口里只要跑过一次 GC 就分不出"分配"和"回收"，那一帧记为不可测而不是猜一个数。
        private long AllocWindowBytes(long start, int gc0Start, out string reason)
        {
            if (BenchAlloc.CounterAvailable) return BenchAlloc.BytesSince(start, out reason);
            if (start < 0) { reason = "GC heap size unavailable"; return -1L; }
            if (GC.CollectionCount(0) != gc0Start)
            {
                reason = "GC ran inside the frame window (GC.GetTotalMemory fallback): allocation and collection cannot be separated";
                return -1L;
            }
            var now = BenchAlloc.ReadHeapBytes();
            if (now < start) { reason = "GC heap shrank inside the frame window (" + start + " -> " + now + ")"; return -1L; }
            reason = null;
            return now - start;
        }

        // 预热结束：开测量窗（§5 法则 2 的 GC.GetTotalMemory 基线）、陈旧 tick 探针、渲染证据。
        private void AfterWarmup()
        {
            // 陈旧 tick 必须被镜像整帧丢弃（§5.2）：喂同一 tick 第二次，SnapshotsApplied 不许动。
            if (!_options.Empty)
            {
                var appliedBeforeDuplicate = _loop.SnapshotsApplied;
                var duplicateAccepted = _loop.ApplySnapshot(_frame);
                _staleTickDropped = !duplicateAccepted && _loop.SnapshotsApplied == appliedBeforeDuplicate;
            }
            _planGcRawBefore = GC.GetTotalMemory(false);
            GC.Collect();
            GC.WaitForPendingFinalizers();
            GC.Collect();
            _planGcStableBefore = GC.GetTotalMemory(false);
            _measuring = true;
        }

        // 统计 + JSON。完成后 Verdict/ExitCode 可用（调用方据此退出）。
        public void Finish()
        {
            if (_done) return;
            _done = true;
            _planGcRawAfter = GC.GetTotalMemory(false);
            GC.Collect();
            GC.WaitForPendingFinalizers();
            GC.Collect();
            _planGcStableAfter = GC.GetTotalMemory(false);
            QualitySettings.vSyncCount = _vSyncBefore;
            Application.targetFrameRate = _targetFpsBefore;
            if (_drawRecorder.Valid) _drawRecorder.Dispose();
            if (_triRecorder.Valid) _triRecorder.Dispose();

            var sample = Math.Max(1, _options.Sample);
            AllocMeasured = _allocUnmeasurable == 0 && _measured > 0;
            AllocPerFrame = AllocMeasured ? _allocBytes / (double)Math.Max(1, _measured) : -1.0;

            var feedFirst = BenchJson.MedianOf(_feedMs, 0, 100);
            var feedLast = BenchJson.MedianOf(_feedMs, _measured - 100, 100);
            var gapFirst = BenchJson.MedianOf(_gapMs, 0, 100);
            var gapLast = BenchJson.MedianOf(_gapMs, _measured - 100, 100);
            Array.Sort(_frameMs);
            Array.Sort(_feedMs);
            Array.Sort(_loopMs);
            Array.Sort(_renderMs);
            Array.Sort(_workMs);
            Array.Sort(_gapMs);
            Array.Sort(_engineDeltaMs);
            FrameP95 = BenchJson.Quantile(_frameMs, 0.95);
            FrameP99 = BenchJson.Quantile(_frameMs, 0.99);
            WorkP95 = BenchJson.Quantile(_workMs, 0.95);
            FeedP95 = BenchJson.Quantile(_feedMs, 0.95);
            LoopP95 = BenchJson.Quantile(_loopMs, 0.95);
            GapP95 = BenchJson.Quantile(_gapMs, 0.95);
            RenderP95 = ManualRender ? BenchJson.Quantile(_renderMs, 0.95) : -1.0;

            GraphicsMeasured = _pixelCoverage != 0.0 && _drawCallsMax > 0;
            // 图形主口径：player 机制下引擎的渲染统计真的可用（引擎帧循环在渲染），优先用它；
            // editor 的 -batchmode 里这两个计数器恒 0（实测），于是回落到"提交了什么"的计数。
            var useEngine = _engineDrawsMax > 0 && _engineTrianglesMax > 0;
            DrawCalls = GraphicsMeasured ? (useEngine ? _engineDrawsMax : _drawCallsMax) : -1;
            Triangles = GraphicsMeasured ? (useEngine ? _engineTrianglesMax : _trianglesMax) : -1;
            Particles = GraphicsMeasured ? _particlesMax : -1;
            Materials = GraphicsMeasured ? _materialsLive : -1;

            var stageMissing = 0;
            for (var i = 0; i < FrameProfiler.StageCount; i++) if (!(_loop.Profiler.P95Ms(i) > 0f)) stageMissing += 1;

            var overBudget = FrameP95 > FrameBudget.FrameP95BudgetMs || FrameP99 > FrameBudget.FrameP99BudgetMs
                || (AllocMeasured && AllocPerFrame > FrameBudget.ManagedAllocBudgetBytes) || _gc0Delta > FrameBudget.Gc0DeltaBudget;
            for (var i = 0; i < FrameProfiler.StageCount; i++) if (_loop.Profiler.P95Ms(i) > FrameBudget.StageBudgetMs[i]) overBudget = true;
            if (DrawCalls > FrameBudget.DrawCallBudget || Triangles > FrameBudget.TriangleBudget
                || Particles > FrameBudget.ParticleBudget || Materials > FrameBudget.MaterialBudget) overBudget = true;

            Verdict = overBudget ? "FAIL"
                : (GraphicsMeasured && stageMissing == 0 && AllocMeasured) ? "PASS" : "UNVERIFIED";
            ExitCode = Verdict == "PASS" ? 0 : 1;

            var json = BuildJson(useEngine, stageMissing, overBudget, feedFirst, feedLast, gapFirst, gapLast, sample);
            BenchJson.WriteOut(_options.OutPath, json);
            var line = StdoutLine(useEngine);
            Console.Out.WriteLine(line);
            Console.Out.Flush();
            // 同一行也进 -logFile：窗口化 player 的 stdout 不一定被父进程收得到，日志是唯一保底。
            Debug.Log(line);
        }

        public string StdoutLine(bool useEngine)
        {
            return "FRAMEBENCH " + Verdict + " mechanism=" + Mechanism
                + " p95=" + FrameP95.ToString("R") + " p99=" + FrameP99.ToString("R")
                + " alloc=" + (AllocMeasured ? AllocPerFrame.ToString("R") : "UNMEASURED") + " gc0=" + _gc0Delta
                + " drawCalls=" + DrawCalls + (useEngine ? "(engine)" : "(submitted)")
                + " triangles=" + Triangles + " particles=" + Particles + " materials=" + Materials
                + " workP95=" + WorkP95.ToString("R") + " feedP95=" + FeedP95.ToString("R") + " gapP95=" + GapP95.ToString("R")
                + " frames=" + _loop.Frames + " out=" + (_options.OutPath ?? "(none)");
        }

        private string BuildJson(bool useEngine, int stageMissing, bool overBudget,
            double feedFirst, double feedLast, double gapFirst, double gapLast, int sample)
        {
            var sb = new StringBuilder();
            var scene = _options.Scene;
            sb.Append("{\n");
            BenchJson.Meta(sb, "machine", Environment.MachineName);
            BenchJson.Meta(sb, "cpu", BenchJson.Safe(delegate { return SystemInfo.processorType; }, "unknown"));
            BenchJson.Meta(sb, "gpu", BenchJson.Safe(delegate { return SystemInfo.graphicsDeviceName; }, "none"));
            BenchJson.Meta(sb, "driver", BenchJson.Safe(delegate { return SystemInfo.graphicsDeviceVersion; }, "none"));
            BenchJson.Meta(sb, "unityVersion", Application.unityVersion);
            BenchJson.Meta(sb, "mechanism", Mechanism);
            // 照实报 Screen：player 机制里它真的会是 1920x1080（窗口），-batchmode 里恒 640x480。
            BenchJson.Meta(sb, "resolution", Screen.width > 0 ? Screen.width + "x" + Screen.height : "headless");
            BenchJson.Meta(sb, "qualityTier", Batching.QualityTier.ToString(CultureInfo.InvariantCulture));
            BenchJson.Meta(sb, "scene", scene);
            BenchJson.Meta(sb, "sceneKind", "plan-scene");
            BenchJson.Meta(sb, "emptyScene", _options.Empty ? "1" : "0");
            BenchJson.Meta(sb, "warmupFrames", _options.Warmup.ToString(CultureInfo.InvariantCulture));
            BenchJson.Meta(sb, "sampleFrames", _measured.ToString(CultureInfo.InvariantCulture));
            BenchJson.Meta(sb, "runs", _options.Runs.ToString(CultureInfo.InvariantCulture));
            BenchJson.Meta(sb, "commit", BenchJson.Safe(BenchJson.GitCommit, "unknown"));
            BenchJson.Meta(sb, "verdict", Verdict);
            BenchJson.Meta(sb, "renderMechanism", ManualRender
                ? "GameBootstrap 装配同一条运行期回路；每帧 GameLoop.Frame(16.67ms) + 手动 Camera.Render()（-batchmode 引擎不渲染）"
                : "GameBootstrap 装配同一条运行期回路；每帧只做 合成来件 + GameLoop.Frame(16.67ms)，渲染由引擎自己的帧循环完成（无手动 Camera.Render）");
            BenchJson.Meta(sb, "renderTarget", ManualRender ? "1920x1080 RenderTexture ARGB32 depth24" : "1920x1080 窗口（引擎帧循环渲染）");
            BenchJson.Meta(sb, "resolutionNote", ManualRender
                ? "-batchmode 无窗口：Screen.SetResolution(1920,1080,false) 是空操作（screenBefore=" + _screenBefore
                    + " screenAfter=" + _screenAfter + "）；测量帧栅格化在 1920x1080 的 RenderTexture 上，Screen.* 按实测照报"
                : "player 窗口：Screen.SetResolution(1920,1080,false) 生效（screenBefore=" + _screenBefore + " screenAfter=" + _screenAfter
                    + "），引擎把相机渲到窗口（无 RenderTexture、无手动 Camera.Render）");
            BenchJson.Meta(sb, "renderPixelEvidence", _pixelNote);
            BenchJson.Meta(sb, "entitySource", "本进程内的确定性生成器（4 玩家 + 60 羊），经 GameLoop.ApplySnapshot 进镜像；未设 AC_SERVER、未起 ac_server 进程");
            BenchJson.Meta(sb, "metricScopeNote", ManualRender
                ? "frameP95Ms/frameP99Ms = 一次测量迭代 = 合成来件 + GameLoop.Frame + 手动 Camera.Render（同步，天然含渲染）。"
                    + "batchmode 里 Camera.Render() 是编辑器独立渲染，实测有与分辨率无关的地板（空场 23.9ms、640x480 与 1920x1080 同价），"
                    + "所以这一项量到的主要是机制地板；归因见 phaseMs.* 与 render.*"
                : "frameP95Ms/frameP99Ms = 相邻两次 Update 起点之间的墙钟（含引擎本帧渲染与 present，VSync off、targetFrameRate -1）。"
                    + "phaseMs.workP95 = 合成来件 + GameLoop.Frame（客户端每帧做功），phaseMs.gapP95 = 帧时间减客户端做功（引擎渲染 + present + 余量）");
            BenchJson.Meta(sb, "verdictNote", Verdict == "PASS"
                ? "plan-scene（mechanism=" + Mechanism + "）：图形/分配/分段全部实测且在预算内"
                : "plan-scene（mechanism=" + Mechanism + "）：非 PASS。overBudget=" + overBudget + " stageMissing=" + stageMissing
                    + " graphicsMeasured=" + GraphicsMeasured + " allocMeasured=" + AllocMeasured
                    + " pixelCoverage=" + _pixelCoverage.ToString("R", CultureInfo.InvariantCulture)
                    + "；超的指标见上方各值");
            sb.Append("  \"budget\": {\n");
            BenchJson.Num(sb, 4, "frameP95Ms", FrameBudget.FrameP95BudgetMs);
            BenchJson.Num(sb, 4, "frameP99Ms", FrameBudget.FrameP99BudgetMs);
            BenchJson.Num(sb, 4, "managedAllocBytesPerFrame", FrameBudget.ManagedAllocBudgetBytes);
            BenchJson.Num(sb, 4, "gc0Delta", FrameBudget.Gc0DeltaBudget);
            BenchJson.Num(sb, 4, "drawCalls", FrameBudget.DrawCallBudget);
            BenchJson.Num(sb, 4, "triangles", FrameBudget.TriangleBudget);
            BenchJson.Num(sb, 4, "particles", FrameBudget.ParticleBudget);
            BenchJson.Num(sb, 4, "materials", FrameBudget.MaterialBudget);
            sb.Append("    \"stageP95Ms\": {\n");
            for (var i = 0; i < FrameProfiler.StageCount; i++) BenchJson.Num(sb, 6, FrameBudget.StageNames[i], FrameBudget.StageBudgetMs[i]);
            BenchJson.TrimLastComma(sb);
            sb.Append("    }\n");
            sb.Append("  },\n");
            BenchJson.Num(sb, "frameP95Ms", FrameP95);
            BenchJson.Num(sb, "frameP99Ms", FrameP99);
            BenchJson.Num(sb, "managedAllocBytesPerFrame", AllocPerFrame);
            BenchJson.Num(sb, "gc0Delta", _gc0Delta);
            BenchJson.Num(sb, "drawCalls", DrawCalls);
            BenchJson.Num(sb, "triangles", Triangles);
            BenchJson.Num(sb, "particles", Particles);
            BenchJson.Num(sb, "materials", Materials);
            sb.Append("  \"stageP95\": {\n");
            for (var i = 0; i < FrameProfiler.StageCount; i++) BenchJson.Num(sb, 4, FrameBudget.StageNames[i], _loop.Profiler.P95Ms(i));
            BenchJson.TrimLastComma(sb);
            sb.Append("  },\n");
            sb.Append("  \"metricSource\": {\n");
            BenchJson.Meta(sb, 4, "frameP95Ms", ManualRender
                ? "Stopwatch 墙钟：一次测量迭代 = 合成来件 + GameLoop.Frame + 手动 Camera.Render，样本 ceil(0.95*n)-1 口径"
                : "Stopwatch 墙钟：相邻两次 Update 起点之间（= 引擎一帧的时间，含本帧渲染与 present），样本 ceil(0.95*n)-1 口径");
            BenchJson.Meta(sb, 4, "frameP99Ms", "同上，ceil(0.99*n)-1");
            BenchJson.Meta(sb, 4, "managedAllocBytesPerFrame", (BenchAlloc.CounterAvailable
                    ? "BenchAlloc（引擎计数器 Memory/'GC Allocated In Frame'，语义与 Ac.Tests.AllocMeter 一致）"
                    : "GC.GetTotalMemory(false) 逐帧窗口（本进程没有 profiler 计数器：release player 里 Memory/'GC Allocated In Frame' 不存在）")
                + (ManualRender ? "，逐帧窗口 = 合成来件 + GameLoop.Frame + 手动 Camera.Render"
                    : "，逐帧窗口 = 合成来件 + GameLoop.Frame（窗口必须落在同一个引擎帧内；引擎渲染的分配由 alloc.planWindow* 覆盖）"));
            BenchJson.Meta(sb, 4, "gc0Delta", "GC.CollectionCount(0) 在同一批逐帧窗口里的差值之和");
            BenchJson.Meta(sb, 4, "drawCalls", useEngine
                ? "引擎渲染统计（ProfilerCategory.Render/'Draw Calls Count'，逐帧取窗口内最大值）"
                : "提交计数：PresentationLayer.SubmittedDraws（Graphics.DrawMeshInstanced 次数）+ 场地 MeshRenderer 数（" + _arenaRenderers
                    + "）；取样本窗口内最大值。引擎侧渲染统计在本进程恒为 0，故不采用");
            BenchJson.Meta(sb, 4, "triangles", useEngine
                ? "引擎渲染统计（ProfilerCategory.Render/'Triangles Count'，逐帧取窗口内最大值）"
                : "提交计数：PresentationLayer.SubmittedTriangles（羊身+额标网格三角形 × 实例数）+ ArenaStats.Triangles（" + _arenaTriangles
                    + "）；取样本窗口内最大值");
            BenchJson.Meta(sb, 4, "particles", "Ac.View.Particles.LiveCount（特效层活粒子数，每帧 Update 后的实时值）；取样本窗口内最大值");
            BenchJson.Meta(sb, 4, "materials", "装配层实际持有的非空材质数（场地 6 + 羊身 + 额标）");
            BenchJson.TrimLastComma(sb);
            sb.Append("  },\n");
            sb.Append("  \"phaseMs\": {\n");
            BenchJson.Num(sb, 4, "feedP95", FeedP95);
            BenchJson.Num(sb, 4, "loopP95", LoopP95);
            BenchJson.Num(sb, 4, "renderP95", RenderP95);
            BenchJson.Num(sb, 4, "gapP95", GapP95);
            BenchJson.Num(sb, 4, "workP95", WorkP95);
            BenchJson.Num(sb, 4, "feedMedianFirst100", feedFirst);
            BenchJson.Num(sb, 4, "feedMedianLast100", feedLast);
            BenchJson.Num(sb, 4, "gapMedianFirst100", gapFirst);
            BenchJson.Num(sb, 4, "gapMedianLast100", gapLast);
            BenchJson.Num(sb, 4, "stageSumP95", StageSumP95(_loop.Profiler));
            BenchJson.Num(sb, 4, "engineDeltaP95", BenchJson.Quantile(_engineDeltaMs, 0.95));
            BenchJson.Num(sb, 4, "engineFrames", _engineFramesEnd - _engineFramesStart);
            BenchJson.TrimLastComma(sb);
            sb.Append("  },\n");
            sb.Append("  \"alloc\": {\n");
            BenchJson.Num(sb, 4, "frameWorkBytesPerFrame", AllocMeasured ? _allocBytes / (double)Math.Max(1, _measured) : -1.0);
            BenchJson.Num(sb, 4, "unmeasurableWindows", _allocUnmeasurable);
            BenchJson.Meta(sb, 4, "source", BenchAlloc.CounterAvailable
                ? "engine-counter:Memory/" + BenchAlloc.MarkerName
                : "gc-heap:GC.GetTotalMemory(false) per-frame window");
            if (_allocReason != null) BenchJson.Meta(sb, 4, "unmeasurableReason", _allocReason);
            BenchJson.Num(sb, 4, "planWindowRawBytesPerFrame", PlanWindow(raw: true));
            BenchJson.Num(sb, 4, "planWindowRetainedBytesPerFrame", PlanWindow(raw: false));
            BenchJson.Num(sb, 4, "planWindowFrames", _measured);
            BenchJson.TrimLastComma(sb);
            sb.Append("  },\n");
            sb.Append("  \"graphics\": {\n");
            BenchJson.Num(sb, 4, "pixelCoverage", _pixelCoverage);
            BenchJson.Num(sb, 4, "arenaRenderers", _arenaRenderers);
            BenchJson.Num(sb, 4, "arenaTriangles", _arenaTriangles);
            BenchJson.Num(sb, 4, "materialsLive", _materialsLive);
            BenchJson.Num(sb, 4, "submittedDrawsMax", _drawCallsMax - _arenaRenderers);
            BenchJson.Num(sb, 4, "sheepTrianglesMax", _trianglesMax - _arenaTriangles);
            BenchJson.Num(sb, 4, "engineDrawCallsMax", _engineDrawsMax);
            BenchJson.Num(sb, 4, "engineTrianglesMax", _engineTrianglesMax);
            BenchJson.Num(sb, 4, "drawnSheepMax", _drawnSheepMax);
            BenchJson.Num(sb, 4, "visibleSheepMax", _visibleSheepMax);
            BenchJson.Num(sb, 4, "writtenSheepMax", _pres.WrittenSheepCount);
            BenchJson.Num(sb, 4, "skippedSheepMax", _skippedSheepMax);
            BenchJson.Num(sb, 4, "particleOverflow", _pres.Effects.ParticlePool.OverflowCount);
            BenchJson.Num(sb, 4, "drawTicks", _pres.DrawTicks);
            BenchJson.Num(sb, 4, "fxTicks", _pres.FxTicks);
            BenchJson.Num(sb, 4, "overlayTicks", _pres.OverlayTicks);
            BenchJson.Num(sb, 4, "hitFxCount", _pres.HitFxCount);
            BenchJson.Num(sb, 4, "overlayBuilds", _pres.OverlayBuilds);
            BenchJson.TrimLastComma(sb);
            sb.Append("  },\n");
            sb.Append("  \"feed\": {\n");
            BenchJson.Num(sb, 4, "snapshotEveryFrames", 1);
            BenchJson.Num(sb, 4, "matchStateEveryFrames", MatchStateEveryFrames);
            BenchJson.Num(sb, 4, "eventPayloadBytes", _eventPayload.Length);
            BenchJson.Num(sb, 4, "matchStatePayloadBytes", _matchPayload.Length);
            BenchJson.Num(sb, 4, "players", PlayerCount);
            BenchJson.Num(sb, 4, "sheep", SheepCount);
            BenchJson.Meta(sb, 4, "sheepKindMix", "36 grunt / 12 ram / 8 elite / 4 king（羊形由快照 state 字节推：state>=9 才是羊王，其余一律 grunt —— PresentationLayer.SheepFormOf）");
            BenchJson.Num(sb, 4, "staleTickDropped", _staleTickDropped ? 1 : 0);
            BenchJson.Num(sb, 4, "snapshotsApplied", _loop.SnapshotsApplied);
            BenchJson.Num(sb, 4, "eventsApplied", _loop.EventsApplied);
            BenchJson.Num(sb, 4, "matchStates", _loop.MatchStateCount);
            BenchJson.Num(sb, 4, "localPlayerId", _loop.LocalPlayerId);
            BenchJson.Num(sb, 4, "decodeFailures", _loop.DecodeFailures);
            BenchJson.Num(sb, 4, "frames", _loop.Frames);
            BenchJson.Num(sb, 4, "sampleFrames", _measured);
            BenchJson.TrimLastComma(sb);
            sb.Append("  },\n");
            for (var i = 0; i < _extraJson.Count; i++) _extraJson[i](sb);
            BenchJson.TrimLastComma(sb);
            sb.Append("\n}\n");
            return sb.ToString();
        }

        private double PlanWindow(bool raw)
        {
            var before = raw ? _planGcRawBefore : _planGcStableBefore;
            var after = raw ? _planGcRawAfter : _planGcStableAfter;
            if (after < 0.0 || _measured <= 0) return -1.0;
            return (after - before) / _measured;
        }

        public static double StageSumP95(FrameProfiler profiler)
        {
            var sum = 0.0;
            for (var i = 0; i < FrameProfiler.StageCount; i++) sum += profiler.P95Ms(i);
            return sum;
        }

        // ---- 合成来件：与线上同一套收包路径（ApplySnapshot / OnPacket），不是"直接改画面状态" ----

        private void FeedSnapshot(ref SnapshotFrame frame, uint tick)
        {
            FillPlanFrame(ref frame, tick);
            _loop.ApplySnapshot(frame);
        }

        private void FeedMatchState(PacketEncoder encoder, byte[] payload, ref PacketHeader header, uint tick)
        {
            encoder.Begin(payload);
            encoder.U8(2);                          // phase = playing
            encoder.U8((byte)(tick / 20u % 4u + 1u));   // wave
            encoder.U16(3000);                      // intermissionMs
            encoder.U8(4);
            for (var i = 0; i < PlayerCount; i++)
            {
                encoder.U16((ushort)(i + 1));
                var name = PlayerName(i);
                encoder.U8((byte)name.Length);
                encoder.Bytes(name);
                encoder.U8(1);                      // ready
                encoder.U8(0);                      // weapon
                encoder.U8(255);                    // hp
                encoder.U16((ushort)(i * 3));       // kills
                encoder.U8(12);                     // mag
                encoder.U16(60);                    // reserve
                encoder.U8(0);                      // reloadLeft
                encoder.U8(0);                      // rage
                encoder.U8(0);                      // rageLeft
                encoder.U8(0);                      // downed
                encoder.U8(0);                      // reviveRatio
            }
            encoder.End();
            header.Type = PacketType.MatchState;
            _loop.OnPacket(header, payload);
        }

        // 昵称必须与 MatchStateCodec 的 MinNameBytes/MaxNameBytes（1..12）相容。
        private static string PlayerName(int index)
        {
            return index == 0 ? BenchLocalName : "bot" + (index + 1).ToString(CultureInfo.InvariantCulture);
        }

        // MatchState 载荷：u8 phase + u8 wave + u16 intermission + u8 count + 4 × (16 定长 + 昵称字节)。
        // 解码器要求 Remaining == 0（多余字节 = BadLength），所以缓冲必须**恰好**这么长。
        private static byte[] BuildMatchStatePayload()
        {
            var bytes = 5;
            for (var i = 0; i < PlayerCount; i++) bytes += MatchStateCodec.FixedRecordBytes + PlayerName(i).Length;
            return new byte[bytes];
        }

        // 每帧一发战斗事件（1 个 PlayerHit + 1 个 SheepKilled）：走 type=6 的真正收包路径，于是
        // Hud.PushEvent 与 GameLoop.EventApplied → PresentationLayer.OnEventApplied → Effects.SpawnHit 真的做功。
        // 载荷固定 33B：u32 tick + u8 count + PlayerHit 18B + SheepKilled 10B。
        private void FeedEvents(PacketEncoder encoder, byte[] payload, ref PacketHeader header, ref uint eventId, uint tick)
        {
            encoder.Begin(payload);
            encoder.U32(tick);
            encoder.U8(2);                          // count
            encoder.U32(eventId++);                 // PlayerHit
            encoder.U8((byte)Ac.Net.EventType.PlayerHit);
            encoder.U16(1);                         // subject = 本地玩家
            encoder.U16(8);                         // target = 一只羊
            encoder.U16(40);                        // damage
            encoder.U8(1);                          // flags = headshot
            encoder.I16(0);
            encoder.I16(100);
            encoder.I16(300);
            encoder.U32(eventId++);                 // SheepKilled
            encoder.U8((byte)Ac.Net.EventType.SheepKilled);
            encoder.U16(12);                        // target
            encoder.U16(1);                         // subject
            encoder.U8(0);                          // kindFlags
            encoder.End();
            header.Type = PacketType.Event;
            _loop.OnPacket(header, payload);
        }

        // 计划场景内容：4 玩家（本地 pid 1）+ 60 羊（36 grunt / 12 ram / 8 elite / 4 king）。
        // 羊群聚在相机前方 6m 处的环带（半径 2.5~6.5m）：全体落在视锥与屏幕占比阈值内，
        // 屏幕上的可见数才真的是 60（环带放到 9m 时近侧的羊会被上下视锥切掉，实测只剩 29 只）。
        private static void FillPlanFrame(ref SnapshotFrame frame, uint tick)
        {
            if (frame.Entities == null || frame.Entities.Length < SnapshotView.MaxRecordsPerFrame)
                frame.Entities = new FrameEntity[SnapshotView.MaxRecordsPerFrame];
            if (frame.RemovedIds == null) frame.RemovedIds = new ushort[SnapshotView.MaxRemovedPerFrame];
            frame.Tick = tick;
            frame.ServerTimeMs = tick * 50u;                        // 20Hz 服务端时钟（快照按帧注入，时钟仍按 50ms 走）
            frame.LastAckedSeq = (ushort)tick;
            frame.BaselineTick = 0u;                                // 每帧全量：差分帧会让站着不动的实体从最新帧里消失
            frame.EntityCount = (ushort)(PlayerCount + SheepCount);
            frame.RemovedCount = 0;
            var playerX = new short[] { 0, 600, -600, 0 };
            var playerZ = new short[] { -800, 600, 600, 1000 };
            for (var i = 0; i < PlayerCount; i++)
            {
                var e = default(FrameEntity);
                e.Id = (ushort)(i + 1);
                e.XCm = playerX[i];
                e.ZCm = playerZ[i];
                e.HpRatioUnits = 255;
                frame.Entities[i] = e;
            }
            for (var i = 0; i < SheepCount; i++)
            {
                var angle = i * (2.0 * Math.PI / SheepCount) + tick * 0.004;
                var radius = 2.5 + (i % 6) * 0.8;
                byte state;
                if (i < 36) state = (byte)SheepAnim.Idle;            // grunt
                else if (i < 48) state = (byte)SheepAnim.Run;        // ram
                else if (i < 56) state = (byte)SheepAnim.Attack;     // elite
                else state = (byte)SheepAnim.KingIdle;               // king
                var e = default(FrameEntity);
                e.Id = (ushort)(i + 5);
                e.KindFlags = PresentationLayer.WireKindSheep;
                e.XCm = (short)(Math.Cos(angle) * radius * 100.0);
                e.ZCm = (short)(Math.Sin(angle) * radius * 100.0 + 600.0);
                e.YawUnits = Quantize.QuantizeAngle(angle);
                e.HpRatioUnits = 255;
                e.State = state;
                frame.Entities[i + PlayerCount] = e;
            }
        }

        // 定长小端写入器：合成来件的字节写进**恰好长度**的载荷缓冲。
        // 每次 new List<byte>() 是基准自己的分配，会把"来件代价"量成"基准代价"；而多留一个字节，
        // MatchStateCodec/EventCodec 的 Remaining == 0 判定就会把整包判成 BadLength。
        private sealed class PacketEncoder
        {
            private byte[] _buffer;
            private int _length;
            internal void Begin(byte[] target) { _buffer = target; _length = 0; }
            // 长度不符必须炸出来：静默多写/少写都会变成"解码失败"而看不出原因。
            internal void End()
            {
                if (_length != _buffer.Length)
                    throw new InvalidOperationException("payload size mismatch: wrote " + _length + " of " + _buffer.Length);
            }
            private void Ensure(int extra)
            {
                if (_length + extra <= _buffer.Length) return;
                throw new InvalidOperationException("payload overflow: " + (_length + extra) + " > " + _buffer.Length);
            }
            internal void U8(byte value) { Ensure(1); _buffer[_length++] = value; }
            internal void U16(ushort value) { Ensure(2); _buffer[_length++] = (byte)(value & 0xFF); _buffer[_length++] = (byte)((value >> 8) & 0xFF); }
            internal void I16(short value) { U16(unchecked((ushort)value)); }
            internal void U32(uint value)
            {
                Ensure(4);
                _buffer[_length++] = (byte)(value & 0xFF); _buffer[_length++] = (byte)((value >> 8) & 0xFF);
                _buffer[_length++] = (byte)((value >> 16) & 0xFF); _buffer[_length++] = (byte)((value >> 24) & 0xFF);
            }
            internal void Bytes(string ascii)
            {
                Ensure(ascii.Length);
                for (var i = 0; i < ascii.Length; i++) _buffer[_length++] = (byte)ascii[i];
            }
        }
    }

    // 分配窗口：引擎计数器 Memory/'GC Allocated In Frame' 的最小封装。
    // 为什么不在 player 里直接用 Ac.Tests.AllocMeter：Ac.Tests 是 Editor-only 程序集（asmdef includePlatforms），
    // player 引用不到。这里读的是**同一个引擎计数器**、判据也一样（窗口必须落在同一个引擎帧内，跨帧一律报不可测），
    // 所以两边的读数可以直接比。语义变更时两处必须一起改。
    public static class BenchAlloc
    {
        public const string MarkerName = "GC Allocated In Frame";

        private static ProfilerRecorder _recorder;
        private static bool _ready;
        private static string _unavailable;
        private static int _windowFrame = -1;

        private static void Ensure()
        {
            if (_ready) return;
            _ready = true;
            try
            {
                _recorder = ProfilerRecorder.StartNew(ProfilerCategory.Memory, MarkerName, 1024, ProfilerRecorderOptions.SumAllSamplesInFrame);
            }
            catch (Exception error)
            {
                _unavailable = error.GetType().Name + ": " + error.Message;
                return;
            }
            if (!_recorder.Valid) { _unavailable = "no 'Memory/" + MarkerName + "' counter in this process"; return; }
            WarmUp();
        }

        // 第一次调用（JIT + 首次读 Time.frameCount）自己会分配几百字节，且记在调用者当时的窗口里；
        // 在任何测量窗口打开之前先付掉，否则第一个窗口永远假红。
        private static void WarmUp()
        {
            try
            {
                _windowFrame = Time.frameCount;
                string reason;
                BytesSince(_recorder.CurrentValue, out reason);
            }
            catch (Exception) { }
        }

        public static long Begin()
        {
            Ensure();
            _windowFrame = Time.frameCount;
            return _recorder.Valid ? _recorder.CurrentValue : -1L;
        }

        // release player 里没有 profiler 计数器（Ensure 会把原因写进 _unavailable）。调用方据此换测量手段。
        public static bool CounterAvailable { get { Ensure(); return _recorder.Valid; } }

        public static long ReadHeapBytes() { return GC.GetTotalMemory(false); }

        public static long BytesSince(long start, out string reason)
        {
            Ensure();
            if (!_recorder.Valid || start < 0) { reason = _unavailable ?? "alloc counter unavailable"; return -1L; }
            if (Time.frameCount != _windowFrame)
            {
                reason = "engine frame boundary inside the window (" + _windowFrame + " -> " + Time.frameCount
                    + "): the per-frame counter resets there";
                return -1L;
            }
            var now = _recorder.CurrentValue;
            if (now < start) { reason = "counter went backwards inside the window (" + start + " -> " + now + ")"; return -1L; }
            reason = null;
            return now - start;
        }
    }
}
