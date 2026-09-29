using System;
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
    // 呈现阶段的缝：帧回路不引用 UnityEngine，特效/音频的实现在 Ac.Boot 的 Unity 侧适配器里，
    // 通过这个接口按帧被调用（也因此测试可以注入假实现来验证"这一段真的被打点了"）。
    public interface IFrameStageSink
    {
        void Tick(double dtMs);
    }

    // 第二类呈现缝：Tick 返回"本帧是否真的做了功"。draw / overlay 用它，因为这两段的功不是恒有的——
    // 没有任何实例可提交、着色器不可用时，**必须不给 draw 打点**：打了点、值恒 0，10ms 的 draw 预算
    // 就永远"通过"（审查点名的假绿；FrameBench 的缺段判 UNVERIFIED 正是为这种情况准备的）。
    public interface IFrameWorkSink
    {
        bool Tick(double dtMs);
    }

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
        // 本地身份 = pid（= 玩家实体 id）。服务端不回 pid，只能按昵称从 MatchState 认领：
        // 过渡方案与代价写在 Ac.Net.LocalIdentity 的类注释里。
        private readonly LocalIdentity _identity = new LocalIdentity();
        private SnapshotFrame _scratchFrame;
        private ushort _localPlayerId;

        private HudSample _sample;
        private StepCommand _pending;
        private bool _hasPending;
        private double _nowMs;
        private byte _phase = Hud.PhaseLobby;
        private int _wave;
        private int _intermissionMs;
        // type 11 Join 的上报去重：_joinSessionSent = 已经上报过昵称的会话号（0 = 还没报过），
        // _joinDirty = 昵称在上报之后又变过。两个条件任一成立就补发一次。
        private ushort _joinSessionSent;
        private bool _joinDirty;

        public GameLoop(SnapshotView view, EntityViews views, Hud hud, FrameProfiler profiler)
        {
            _view = view;
            _views = views;
            _hud = hud;
            _profiler = profiler;
        }

        public UdpTransport Transport { get; set; }      // 离线（单机/帧基准）时为 null
        // 输入采样器（C05 §5.1）：**产品侧唯一的键鼠来源**。接上之后本帧的命令会①从传输发出去
        // ②进本地预测；为 null 时（帧基准/无头）一帧都不采样。此前它连 `new` 都没有生产调用者：
        // 采样器、编解码、传输三者都在，却没有任何东西把它们接起来（联调只能靠测试桩手搓命令）。
        public InputSampler Sampler { get; set; }
        public int CommandsSent { get; private set; }
        public int CommandSendFailures { get; private set; }
        public IFrameStageSink Fx { get; set; }
        public IFrameStageSink Audio { get; set; }
        public IFrameWorkSink Draw { get; set; }         // 有绘制提交才打点（见 IFrameWorkSink）
        public IFrameWorkSink Overlay { get; set; }      // 上层界面（屏幕流/调试面板）每帧都做功
        public EventIdTracker Events { get; set; }

        // 本地身份。写入视图镜像的那一步就在这里（GameLoop 是持有 SnapshotView 的那个缝），
        // 不另开一条"同时改两处"的通路：SnapshotView.LocalPlayerId 与它恒等（0 = 没有本地实体）。
        public ushort LocalPlayerId
        {
            get { return _localPlayerId; }
            set
            {
                _localPlayerId = value;
                if (_view != null) _view.SetLocalPlayer(value);
                // 视图池的本地指针此前**没有任何生产调用者**（EntityViews.SetLocalPlayer 全仓只有测试在用）。
                // 身份解析成功处就是它的第一个生产调用点，语义与 SnapshotView.SetLocalPlayer 同一处：
                // 两条镜像的本地指针必须恒等，否则插值/吸附会拿"没有本地实体"的分支。
                if (_views != null) _views.SetLocalPlayer(value);
            }
        }

        // 玩家在大厅输入的昵称（已清洗）。身份解析的输入；变化时若手里已有 MatchState 就立刻重解析。
        public string LocalName
        {
            get { return _identity.Name; }
            set
            {
                _identity.SetName(value);
                // 昵称变了就得再上报一次（type 11）；已连上时会由本帧的 FlushJoin 发出去。
                _joinDirty = true;
                if (MatchStateCount > 0) ResolveLocalPlayer();
            }
        }

        public LocalIdentity Identity { get { return _identity; } }
        public int IdentityChanges { get; private set; }

        // 身份重解析的唯一入口：MatchState 到达、昵称变化、换房/重连/自己中途进出对局都走这里。
        // 返回解析出的 pid（0 = 玩家表里没有本地昵称 ⇒ 没有本地实体）。
        public ushort ResolveLocalPlayer()
        {
            if (!_identity.Resolve(LastMatchState.Players)) return LocalPlayerId;
            LocalPlayerId = _identity.Pid;
            IdentityChanges += 1;
            return LocalPlayerId;
        }

        // 事件缝：HUD 之外，特效层也要按事件生成弹着/血屑（此前事件只有 Hud 一个消费者，Effects 在生产里零调用）。
        // 委托字段而不是事件列表：热路径上不分配。
        public Action<HudEvent> EventApplied { get; set; }

        // 最近一次 type=10 的相位载荷：屏幕流（Lobby/Intermission/Results/Roster）由它驱动，
        // 而相位本身不能只靠 Sample.Phase（HudSample 装不下 players 表）。
        public MatchStatePayload LastMatchState { get; private set; }
        public int MatchStateCount { get; private set; }

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
        // 本地已推进的模拟子步数（调试面板的 playerTick）。没有它，面板只能拿 AppliedTick 冒充本地 tick。
        public int LocalSteps { get { return _predictor.Steps; } }

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
                    LastMatchState = state;
                    MatchStateCount += 1;
                    // 相位/成员变化都在这一拍里：本地身份也在这里重解析（换房、重连、中途进出对局）。
                    ResolveLocalPlayer();
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
            hudEvent.TargetId = entry.TargetId;
            hudEvent.Wave = entry.Wave;
            hudEvent.WaveSize = entry.Budget;
            hudEvent.ReviveRatio255 = entry.Ratio255;
            _hud.PushEvent(hudEvent);
            EventsApplied += 1;
            var handler = EventApplied;
            if (handler != null) handler(hudEvent);
        }

        // C05 §5.1/§5.2 的产品接线：键鼠 → 30Hz 命令 → ①真发 type=4 ②同一条命令进本地预测。
        // 两条路用**同一份量化载荷**：不重新采样，避免"发出去的和本地预测的不是一条"（本地会漂）。
        private void PumpInput(double dtMs)
        {
            var sampler = Sampler;
            if (sampler == null) return;
            // clientTick 必须是**权威 tick**：服务端 `security::validateClientTick` 与服务端 tick 严格相等，
            // 预支或滞后一样整批丢掉（联调里战绩 `aliveMs: 0` 的根因）。来源 = 最近一条已应用快照的 tick。
            sampler.SetClientTick(_view != null ? (uint)_view.AppliedTick : 0u);
            sampler.Update(dtMs);
            // 大厅的准备位（ADR-013）：确认键一次翻转一次。服务端只在大厅相位采用这一位。
            if (sampler.ConfirmPressed && _phase == Hud.PhaseLobby) sampler.SetReadyHeld(!sampler.ReadyHeld);

            InputIntent intent;
            while (sampler.TryTakeCommand(out intent))
            {
                var payload = CommandCodec.IntentToPayload(intent);
                var transport = Transport;
                if (transport != null && transport.State == ConnectionState.Connected)
                {
                    if (transport.Send(PacketType.Command, CommandCodec.Encode(payload))) CommandsSent += 1;
                    else CommandSendFailures += 1;
                }
                QueueCommand(CommandCodec.ToStepCommand(payload));
            }
        }

        // 局内聊天（键位表 [ActionChat]）：显隐的唯一真相是相位，与 HUD 用同一条判据（本层不自己推相位）。
        // 读 `_phase`（MatchState 的权威字节）而不是 `_sample.Phase`：后者由帧末的 FillSample 写，
        // 在帧首问它会拿到上一帧的值。
        public bool ChatVisible { get { return Hud.CombatUiVisible(_phase); } }

        public void Frame(double dtMs)
        {
            _profiler.Begin();

            // ① input：采样 → 上行 + 本地预测
            PumpInput(dtMs);
            if (_hasPending)
            {
                _predictor.Advance((int)dtMs, _pending);
                _commands.Push(_pending);
                _hasPending = false;
            }
            _profiler.Mark(FrameStage.Input);

            // ② sync：收包（离线时什么都没发生）
            if (Transport != null)
            {
                Transport.Poll(MaxInboundPerPoll);
                FlushJoin();
            }
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

            // ⑤ 呈现：特效与音频各有独立预算段（§5 的 fx / audio）。没接线就不打点——
            //    "报 0 让预算永远通过"是审查点名的假绿。
            // 关键：**没接线就不打点**。打了点、值为 0，会让 fx/audio 的预算永远"通过"。
            if (Fx != null) { Fx.Tick(dtMs); _profiler.Mark(FrameStage.Fx); }
            if (Audio != null) { Audio.Tick(dtMs); _profiler.Mark(FrameStage.Audio); }

            // ⑤ HUD：采样 → 应用 → 推进
            FillSample();
            _hud.Apply(_sample);
            _hud.Tick((float)dtMs);
            _profiler.Mark(FrameStage.Hud);

            // ⑥ 上层界面与绘制提交：放在 HUD 之后，这样屏幕流拿到的是**本帧**的采样（相位/波次）。
            //    overlay 恒做功（屏幕流 + 调试面板），draw 只在真的提了绘制时打点。
            if (Overlay != null && Overlay.Tick(dtMs)) _profiler.Mark(FrameStage.Overlay);
            if (Draw != null && Draw.Tick(dtMs)) _profiler.Mark(FrameStage.Draw);

            _profiler.End();
            Frames += 1;
        }

        // type 11 Join（ADR-009「握手时序」）：把本地昵称送到服务端。两个时机都要覆盖：
        // ① 连上了（含重连换了 session）而昵称已经敲好；② 昵称在上报之后又变了。
        // 以 session 号 + 脏标记去重 ⇒ 稳态 0 包、0 分配；发送失败（未连接/名字非法）留到下一帧重试。
        private void FlushJoin()
        {
            var transport = Transport;
            if (transport == null || transport.State != ConnectionState.Connected) return;
            var session = transport.Session;
            if (!_joinDirty && session == _joinSessionSent) return;
            if (!transport.SendJoin(_identity.Name)) return;
            _joinSessionSent = session;
            _joinDirty = false;
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
