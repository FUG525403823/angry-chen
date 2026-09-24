using Ac.Core;
using Ac.Net;
using Ac.Sim;
using Ac.UI;
using Ac.View;

namespace Ac.Boot
{
    // 运行期装配根（审计 H6 的缺口）：把"输入 → 预测/和解 → 快照镜像 → 视图 → HUD"这条帧回路真正接起来。
    // 在它出现之前，这些类型每一种都只有测试在调用，游戏循环根本不存在——C01–C15 没有任何一份计划交付它。
    //
    // 这里刻意**不引用 UnityEngine**：帧基准（Ac.Tests.FrameBench）与无头用例跑的是同一条回路，
    // 免得"基准测的路径"和"游戏跑的路径"变成两条代码。
    public sealed class GameLoop
    {
        public const int MaxInboundPerPoll = 64;

        private readonly SnapshotView _view;
        private readonly EntityViews _views;
        private readonly Hud _hud;
        private readonly FrameProfiler _profiler;
        private readonly Predictor _predictor = new Predictor();
        private readonly Reconciler _reconciler = new Reconciler();
        private readonly CommandBuffer _commands = new CommandBuffer();
        private readonly Interpolation.RenderClock _clock = new Interpolation.RenderClock();
        private SnapshotFrame _scratchFrame;

        private HudSample _sample;
        private StepCommand _pending;
        private bool _hasPending;
        private double _nowMs;
        private byte _phase = Hud.PhaseLobby;
        private int _wave;
        private int _intermissionMs;

        public GameLoop(SnapshotView view, EntityViews views, Hud hud, FrameProfiler profiler)
        {
            _view = view;
            _views = views;
            _hud = hud;
            _profiler = profiler;
        }

        public UdpTransport Transport { get; set; }      // 离线（单机/帧基准）时为 null
        public EventIdTracker Events { get; set; }
        public ushort LocalPlayerId { get; set; }

        public int Frames { get; private set; }
        public int SnapshotsApplied { get; private set; }
        public int EventsApplied { get; private set; }
        public int DecodeFailures { get; private set; }
        public int HardCorrects { get; private set; }
        public double LastReconcileErrorM { get; private set; }
        public FrameProfiler Profiler { get { return _profiler; } }
        public SnapshotView View { get { return _view; } }
        public EntityViews Views { get { return _views; } }
        public Hud Hud { get { return _hud; } }
        public HudSample Sample { get { return _sample; } }

        public void QueueCommand(in StepCommand command)
        {
            _pending = command;
            _hasPending = true;
        }

        // 喂一帧权威快照。网络路径与帧基准都走这里，保证测的是同一条路。
        public bool ApplySnapshot(in SnapshotFrame frame)
        {
            if (!_view.ApplyFrame(frame)) return false;
            _clock.OnSnapshot(frame.ServerTimeMs, _nowMs);
            SnapshotsApplied += 1;
            return true;
        }

        // type=5/6/10 的收包入口（会话层只负责把载荷交上来）。
        public void OnPacket(PacketHeader header, byte[] payload)
        {
            switch (header.Type)
            {
                case PacketType.Snapshot:
                {
                    SnapshotPayload decoded;
                    if (SnapshotCodec.Decode(payload, Events, out decoded) != DecodeFailure.Ok) { DecodeFailures += 1; return; }
                    _scratchFrame = default(SnapshotFrame);
                    if (!SnapshotCodec.TryToFrame(decoded, ref _scratchFrame)) { DecodeFailures += 1; return; }
                    ApplySnapshot(_scratchFrame);
                    return;
                }
                case PacketType.MatchState:
                {
                    MatchStatePayload state;
                    if (MatchStateCodec.Decode(payload, out state) != DecodeFailure.Ok) { DecodeFailures += 1; return; }
                    _phase = state.Phase;
                    _wave = state.Wave;
                    _intermissionMs = state.IntermissionMs;
                    return;
                }
                case PacketType.Event:
                {
                    EventFrame frame;
                    if (EventCodec.DecodeFrame(payload, Events, out frame) != DecodeFailure.Ok) { DecodeFailures += 1; return; }
                    if (frame.Entries == null) return;
                    for (var i = 0; i < frame.Entries.Length; i++)
                    {
                        // 只认冻结事件域里的条目：解码器的数组可能带未用槽位，未知类型直接跳过。
                        if (!EventCodec.IsKnownEvent(frame.Entries[i].Type)) continue;
                        ApplyEvent(frame.Entries[i]);
                    }
                    return;
                }
            }
        }

        // EventEntry → HudEvent 的接线（此前只存在于测试里，没有任何生产侧适配器）。
        private void ApplyEvent(in EventEntry entry)
        {
            var hudEvent = default(HudEvent);
            hudEvent.Type = entry.Type;
            hudEvent.HitFlags = entry.Flags;
            hudEvent.Wave = entry.Wave;
            hudEvent.WaveSize = entry.Budget;
            hudEvent.ReviveRatio255 = entry.Ratio255;
            _hud.PushEvent(hudEvent);
            EventsApplied += 1;
        }

        public void Frame(double dtMs)
        {
            _profiler.Begin();

            // ① input：本帧命令进预测器与未确认缓冲
            if (_hasPending)
            {
                _predictor.Advance((int)dtMs, _pending);
                _commands.Push(_pending);
                _hasPending = false;
            }
            _profiler.Mark(FrameStage.Input);

            // ② sync：收包（离线时什么都没发生）
            if (Transport != null) Transport.Poll(MaxInboundPerPoll);
            _profiler.Mark(FrameStage.Sync);

            // ③ predict：本地权威 + 和解
            LocalAuthority authority;
            if (LocalAuthority.TryProject(_view, out authority))
            {
                var result = _reconciler.Reconcile(authority, _commands, _predictor);
                LastReconcileErrorM = result.ErrorM;
                if (result.HardCorrect) HardCorrects += 1;
                EntityView local;
                if (_views.TryGet(LocalPlayerId, out local) && result.ErrorM > 0.0)
                {
                    _views.ApplyCorrection(local, _predictor.State.X - result.OffsetX, _predictor.State.Y - result.OffsetY, _predictor.State.Z - result.OffsetZ);
                }
            }
            _profiler.Mark(FrameStage.Predict);

            // ④ 视图：渲染时钟推进 + 镜像同步
            _nowMs += dtMs;
            _clock.Advance(_nowMs);
            _views.SyncFrame(_view, _clock, dtMs);
            _profiler.Mark(FrameStage.Sync);   // 镜像同步算 sync 段；draw 段在没有渲染器时不打点（见 FrameBench）

            // ⑤ HUD：采样 → 应用 → 推进
            FillSample();
            _hud.Apply(_sample);
            _hud.Tick((float)dtMs);
            _profiler.Mark(FrameStage.Hud);

            _profiler.End();
            Frames += 1;
        }

        private void FillSample()
        {
            FrameEntity local;
            if (_view.TryGetEntity(LocalPlayerId, out local))
            {
                _sample.HpRatio255 = local.HpRatioUnits;
                _sample.Downed = (local.KindFlags & EntityRecord.DownedFlag) != 0;
                _sample.RageMode = (local.KindFlags & EntityRecord.RageModeFlag) != 0;
                _sample.Reloading = (local.KindFlags & EntityRecord.ReloadingFlag) != 0;
                _sample.Charging = (local.KindFlags & EntityRecord.ChargingFlag) != 0;
            }
            _sample.Phase = _phase;
            _sample.Wave = _wave;
            _sample.IntermissionMs = _intermissionMs;
        }
    }
}
