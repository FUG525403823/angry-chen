using System;
using System.IO;
using UnityEngine;
using Unity.Profiling;
using Ac.Core;

namespace Ac.Boot
{
    // Pure sampling seam: each call describes the previous completed engine frame.
    public sealed class ExpeditionPerformanceSamples
    {
        public readonly double[] P95 = new double[FrameBudget.Runs];
        public readonly double[] P99 = new double[FrameBudget.Runs];
        public readonly long[] DrawMax = new long[FrameBudget.Runs];
        public readonly long[] TriangleMax = new long[FrameBudget.Runs];
        private readonly double[] _wall = new double[FrameBudget.SampleFrames];
        private int _lastFrame = -1, _warmup, _sample;
        private bool _failed;
        public int Runs { get; private set; }
        public int Missing { get; private set; }
        public bool Complete { get { return Runs == FrameBudget.Runs; } }
        public string Status { get { return !Complete || Missing > 0 ? "UNVERIFIED" : _failed ? "FAIL" : "PASS"; } }
        private void ResetPartial()
        {
            _warmup = _sample = 0;
            DrawMax[Runs] = TriangleMax[Runs] = 0;
        }
        public int RequestedNode { get; private set; }
        public ExpeditionPerformanceSamples(int requestedNode = 1) { RequestedNode = requestedNode; }
        public void Add(int frame, double wallMs, long draws, long triangles, byte mode, byte stage, byte node)
        {
            AddCore(frame, wallMs, draws, triangles, mode,
                (RequestedNode == 1 || RequestedNode == 3) && node == RequestedNode ? stage : (byte)0);
        }
        public void Add(int frame, double wallMs, long draws, long triangles, byte mode, byte stage)
        {
            Add(frame, wallMs, draws, triangles, mode, stage, 1);
        }
        private void AddCore(int frame, double wallMs, long draws, long triangles, byte mode, byte stage)
        {
            if (Complete) return;
            var consecutive = _lastFrame >= 0 && frame == _lastFrame + 1;
            _lastFrame = frame;
            if (mode != 1 || stage != 2) { ResetPartial(); return; }
            if (!consecutive || wallMs <= 0 || double.IsNaN(wallMs) || double.IsInfinity(wallMs))
            {
                if (_warmup > 0 || _sample > 0) Missing++;
                ResetPartial(); return;
            }
            if (_warmup < FrameBudget.WarmupFrames) { _warmup++; return; }
            if (draws <= 0 || triangles <= 0) Missing++;
            _wall[_sample++] = wallMs;
            if (draws > DrawMax[Runs]) DrawMax[Runs] = draws;
            if (triangles > TriangleMax[Runs]) TriangleMax[Runs] = triangles;
            if (_sample != FrameBudget.SampleFrames) return;
            Array.Sort(_wall);
            P95[Runs] = _wall[(int)Math.Ceiling(_sample * .95) - 1];
            P99[Runs] = _wall[(int)Math.Ceiling(_sample * .99) - 1];
            _failed |= P95[Runs] > FrameBudget.FrameP95BudgetMs || P99[Runs] > FrameBudget.FrameP99BudgetMs
                || DrawMax[Runs] > FrameBudget.DrawCallBudget || TriangleMax[Runs] > FrameBudget.TriangleBudget;
            Runs++; _sample = _warmup = 0;
        }
    }

    // Opt-in only. Does not replace the driver, render manually, or change pacing.
    public sealed class ExpeditionPerformanceObserver : MonoBehaviour
    {
        private static bool _parsed;
        private static string _output;
        private static int _requestedNode = 1;
        private GameLoop _loop;
        private ExpeditionPerformanceSamples _samples;
        private ProfilerRecorder _draws, _triangles;
        private double _previousWall;
        private int _previousFrame = -1;
        private byte _previousMode, _previousStage, _previousNode;
        private bool _written;
        private int _nodeMin = int.MaxValue, _nodeMax, _cardsMin = int.MaxValue, _cardsMax;
        private int _playersMin = int.MaxValue, _playersMax;
        private int _widthMin = int.MaxValue, _widthMax, _heightMin = int.MaxValue, _heightMax;
        private int _qualityMin = int.MaxValue, _qualityMax, _vsyncMin = int.MaxValue, _vsyncMax;
        private int _fpsMin = int.MaxValue, _fpsMax = int.MinValue;
        public static void Attach(GameObject root, GameLoop loop)
        {
            if (!_parsed)
            {
                _parsed = true;
                var args = Environment.GetCommandLineArgs();
                for (var i = 0; i < args.Length; i++)
                {
                    if (args[i] == "-expeditionPerformanceOut" && i + 1 < args.Length) _output = args[i + 1];
                    if (args[i] == "-expeditionPerformanceNode")
                    {
                        int node;
                        _requestedNode = i + 1 < args.Length && int.TryParse(args[i + 1], out node) && (node == 1 || node == 3) ? node : -1;
                    }
                }
            }
            if (string.IsNullOrEmpty(_output) || root.GetComponent<ExpeditionPerformanceObserver>() != null) return;
            var observer = root.AddComponent<ExpeditionPerformanceObserver>();
            observer._loop = loop;
            observer._samples = new ExpeditionPerformanceSamples(_requestedNode);
            if (_requestedNode != 1 && _requestedNode != 3) Debug.LogWarning("Expedition performance node must be 1 or 3; report remains UNVERIFIED.");
            try { observer._draws = ProfilerRecorder.StartNew(ProfilerCategory.Render, "Draw Calls Count", 1, ProfilerRecorderOptions.Default | ProfilerRecorderOptions.SumAllSamplesInFrame); } catch (Exception) { }
            try { observer._triangles = ProfilerRecorder.StartNew(ProfilerCategory.Render, "Triangles Count", 1, ProfilerRecorderOptions.Default | ProfilerRecorderOptions.SumAllSamplesInFrame); } catch (Exception) { }
        }
        private void Update()
        {
            if (_loop == null || _written) return;
            var wall = System.Diagnostics.Stopwatch.GetTimestamp() / (double)System.Diagnostics.Stopwatch.Frequency * 1000.0;
            var frame = Time.frameCount;
            if (_previousFrame >= 0)
            {
                var adjacent = frame == _previousFrame + 1;
                _samples.Add(adjacent ? _previousFrame : frame, wall - _previousWall,
                    adjacent && _draws.Valid && _draws.Count > 0 ? _draws.LastValue : -1,
                    adjacent && _triangles.Valid && _triangles.Count > 0 ? _triangles.LastValue : -1,
                    _previousMode, _previousStage, _previousNode);
            }
            _previousFrame = frame; _previousWall = wall;
            var state = _loop.Expedition.State;
            _previousMode = state == null ? (byte)0 : state.Mode;
            _previousStage = state == null ? (byte)0 : state.Stage;
            _previousNode = state == null ? (byte)0 : state.Node;
            if (_previousMode == 1 && _previousStage == 2 && _previousNode == _requestedNode)
            {
                Range(state.Node, ref _nodeMin, ref _nodeMax);
                Range(_loop.Expedition.LocalCardMask, ref _cardsMin, ref _cardsMax);
                Range(state.PlayerCount, ref _playersMin, ref _playersMax);
                Range(Screen.width, ref _widthMin, ref _widthMax); Range(Screen.height, ref _heightMin, ref _heightMax);
                Range(QualitySettings.GetQualityLevel(), ref _qualityMin, ref _qualityMax);
                Range(QualitySettings.vSyncCount, ref _vsyncMin, ref _vsyncMax);
                Range(Application.targetFrameRate, ref _fpsMin, ref _fpsMax);
            }
            if (_samples.Complete) WriteReport();
        }
        private static void Range(int value, ref int min, ref int max) { if (value < min) min = value; if (value > max) max = value; }
        [Serializable] private sealed class Report
        {
            public string scopedFrameGraphicsVerdict, rewardStatus = "UNVERIFIED", allocation = "UNMEASURED", canvasHumanReview = "UNREVIEWED";
            public string scope = "Completed engine-frame wall interval and graphics; includes acceptance driver, I/O, VSync and scheduling. Not GPU time or zero-allocation proof. Combat only; no phase mixing.";
            public string sceneCoverage = "Requested-node combat only; not a worst-case scene guarantee. Card/build/player ranges are observed, not assumed. Metadata includes warmup and interrupted candidates of the requested node.";
            public int requestedNode, runs, missing;
            public int nodeMin, nodeMax, cardMaskMin, cardMaskMax, playerCountMin, playerCountMax;
            public int widthMin, widthMax, heightMin, heightMax, qualityMin, qualityMax, vSyncMin, vSyncMax, targetFpsMin, targetFpsMax;
            public double[] wallP95Ms, wallP99Ms;
            public long[] drawMax, triangleMax;
        }
        private void WriteReport()
        {
            if (_written || _samples == null) return;
            _written = true;
            var report = new Report { requestedNode = _samples.RequestedNode, scopedFrameGraphicsVerdict = _samples.Status, runs = _samples.Runs, missing = _samples.Missing,
                wallP95Ms = _samples.P95, wallP99Ms = _samples.P99, drawMax = _samples.DrawMax, triangleMax = _samples.TriangleMax,
                nodeMin = _nodeMin == int.MaxValue ? -1 : _nodeMin, nodeMax = _nodeMax, cardMaskMin = _cardsMin == int.MaxValue ? -1 : _cardsMin, cardMaskMax = _cardsMax,
                playerCountMin = _playersMin == int.MaxValue ? -1 : _playersMin, playerCountMax = _playersMax,
                widthMin = _widthMin == int.MaxValue ? -1 : _widthMin, widthMax = _widthMax, heightMin = _heightMin == int.MaxValue ? -1 : _heightMin, heightMax = _heightMax,
                qualityMin = _qualityMin == int.MaxValue ? -1 : _qualityMin, qualityMax = _qualityMax, vSyncMin = _vsyncMin == int.MaxValue ? -1 : _vsyncMin, vSyncMax = _vsyncMax,
                targetFpsMin = _fpsMin == int.MaxValue ? -1 : _fpsMin, targetFpsMax = _fpsMax == int.MinValue ? -1 : _fpsMax };
            try
            {
                var directory = Path.GetDirectoryName(_output);
                if (!string.IsNullOrEmpty(directory)) Directory.CreateDirectory(directory);
                File.WriteAllText(_output, JsonUtility.ToJson(report, true));
            }
            catch (Exception error) { Debug.LogWarning("Expedition performance report unavailable: " + error.Message); }
        }
        private void OnApplicationQuit() { WriteReport(); }
        private void OnDestroy() { WriteReport(); _draws.Dispose(); _triangles.Dispose(); }
    }
}
