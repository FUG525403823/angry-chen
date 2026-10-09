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
        // 本地开火镜像用的射速倍率：服务端是 `rage ? kRageFireRateMultiplier(1.25) : 1`，
        // 本地不镜像 rage（残余记录见 client-v2-remaining-work.md 的 A21）。
        private const float LocalFireRateMultiplier = 1.0f;

        private readonly SnapshotView _view;
        private readonly EntityViews _views;
        private readonly Hud _hud;
        private readonly FrameProfiler _profiler;
        private readonly Predictor _predictor = new Predictor();
        private readonly Reconciler _reconciler = new Reconciler();
        private readonly CommandBuffer _commands = new CommandBuffer();
        // 和解的触发条件（见 Frame 的 ③）：已应用快照的 tick 变过才回滚重放一次。
        private uint _lastReconciledTick;
        private bool _reconciledOnce;
        // 本地预测的累计子步数（只计 Advance 的预测步，不算和解重放）。
        // 残留①③修复用它做「步数对齐投影」：权威 tick T 的位置是服务端 T−1 步，客户端此刻
        // 已经预测到 _predictionSteps 步，把权威按本地速度外推 (extraSteps×50ms) 到客户端当前
        // 网格，相位级的 0/1 子步差就不进误差了（见 Frame ③ 的注释）。
        // 投影只补偿本会话的 RTT 往返相位：extraSteps 超过这个窗口就说明权威 tick 与本地预测
        // 不是同一条时钟（大厅 world tick 冻结 / 换会话 / 时钟漂移），再按它外推就是错上加错。
        private const int MaxProjectionSteps = 4;   // 4 步 = 200ms：覆盖任何真实 RTT 的往返相位
        private int _predictionSteps;
        private readonly Interpolation.RenderClock _clock = new Interpolation.RenderClock();
        // 本地身份只接受 MatchState 的权威 localPid；昵称单独用于 Join。
        private readonly LocalIdentity _identity = new LocalIdentity();
        private string _localName = string.Empty;
        private UdpTransport _transport;
        private ushort _identitySession;
        private SnapshotFrame _scratchFrame;
        private int _mirrorReconnectCount;   // 镜像对应的重连次数（重连一次就复位镜像与 tick 原点）
        private ushort _localPlayerId;

        private HudSample _sample;
        private StepCommand _pending;
        private bool _hasPending;
        private double _nowMs;
        // C06 §5(c)：本地武器镜像（射速/换弹/弹匣/散布）+ 弹药本地账（未确认开火）。
        private readonly LocalWeapon _weapon = new LocalWeapon();
        private readonly AmmoLedger _ammo = new AmmoLedger();
        private bool _ammoDataReady;
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
            // 复用帧的数组**必须在这里分配一次**：`SnapshotCodec.TryToFrame` 要求 Entities/RemovedIds 非空，
            // 而 OnPacket 此前每包把它重置成 `default(SnapshotFrame)`（引用字段为 null）⇒ **每一条下行
            // 快照都被判失败丢弃**（只进 DecodeFailures，界面上什么都看不见）。出包实跑的症状：
            // inboundBytesPerSec 19240（快照真的在收）却 entityCount=0 / serverTick=0，
            // 世界镜像恒空 ⇒ 相机没有本地实体可跟（停在装配原点，画面是几何体内壁）、HUD HP 恒 0。
            // 用例 `boot.snapshot_reaches_mirror` 钉住这条缝；跨帧复用也满足帧预算的 0 B/帧。
            _scratchFrame.Entities = new FrameEntity[SnapshotView.MaxRecordsPerFrame];
            _scratchFrame.RemovedIds = new ushort[SnapshotView.MaxRecordsPerFrame];
        }

        public UdpTransport Transport
        {
            get { return _transport; }
            set
            {
                if (ReferenceEquals(_transport, value)) return;
                if (_transport != null) _transport.StateChanged -= OnConnectionChanged;
                _transport = value;
                ClearIdentity();
                _identitySession = 0;
                _joinDirty = true;
                _joinSessionSent = 0;
                if (_transport != null)
                {
                    _transport.StateChanged += OnConnectionChanged;
                    _identitySession = _transport.Session;
                }
            }
        }

        private void OnConnectionChanged(ConnectionState previous, ConnectionState next)
        {
            if (next == ConnectionState.Disconnected || next == ConnectionState.Connecting)
            {
                ClearIdentity();
                _identitySession = 0;
                _joinDirty = true;
                _joinSessionSent = 0;
            }
            else if (next == ConnectionState.Connected)
            {
                if (_identitySession != 0 && _identitySession != _transport.Session) ClearIdentity();
                _identitySession = _transport.Session;
            }
            // 会话层换状态的可见性（实跑排障用）：断线/重连不静默。
            UnityEngine.Debug.Log("Ac.Boot: 连接状态 " + previous + " -> " + next
                + "（session=" + (_transport == null ? 0 : _transport.Session) + "）");
        }

        private void ClearIdentity()
        {
            LastMatchState = default(MatchStatePayload);
            MatchStateCount = 0;
            _ammoDataReady = false;
            _weapon.Reset();
            _ammo.Reset();
            if (_identity.Resolve(0)) IdentityChanges += 1;
            LocalPlayerId = 0;
            _hasPending = false;
            _pending = default(StepCommand);
            // A23 残留修复：步数对齐投影的计数只对「本会话的预测网格」有意义。断线/换会话后
            // 权威 tick 会重置（新房间从 0 起）或延续旧房间；不把 _predictionSteps 归零的话，
            // 重连后第一次和解的 extraSteps = 旧累计 − 新 tick，可能算成几十上百步的外推 ⇒
            // 权威被投影甩出几十米 ⇒ 硬纠正 ⇒ 画面闪烁/传送（实跑：频繁断线重连时屏幕狂闪）。
            // 归零后 extraSteps ≤ 0 不再投影，新会话从权威原始位置起算，由平滑器接管小误差。
            _predictionSteps = 0;
        }
        // 输入采样器（C05 §5.1）：**产品侧唯一的键鼠来源**。接上之后本帧的命令会①从传输发出去
        // ②进本地预测；为 null 时（帧基准/无头）一帧都不采样。此前它连 `new` 都没有生产调用者：
        // 采样器、编解码、传输三者都在，却没有任何东西把它们接起来（联调只能靠测试桩手搓命令）。
        public InputSampler Sampler { get; set; }
        // 最后一条采样到的移动意图（给视图模型的行走摆动用）
        public double LastMoveX { get; private set; }
        public double LastMoveY { get; private set; }
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

        // 玩家在大厅输入的昵称（已清洗），只用于 Join，不参与身份解析。
        public string LocalName
        {
            get { return _localName; }
            set
            {
                _localName = value ?? string.Empty;
                _joinDirty = true;
            }
        }

        public LocalIdentity Identity { get { return _identity; } }
        public int IdentityChanges { get; private set; }

        // 应用已校验 MatchState 的权威身份；0 表示未绑定。
        public ushort ResolveLocalPlayer()
        {
            if (!_identity.Resolve(LastMatchState.LocalPid)) return LocalPlayerId;
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
        // 和解次数（每条新权威快照一次，见 Frame ③）与最近一次重放条数。给用例一条"确实和解过、
        // 且重放了几步"的判据：少了它们，那些"位置平滑/误差很小"的断言在没有和解时也会真空通过。
        public int Reconciles { get; private set; }
        public int LastReplayed { get; private set; }
        // 本地武器镜像与弹药账（C06 §5(c)）：`LocalShotFired` 是**当帧**开火信号——枪口火焰、曳光、
        // 后坐、准星散布都跟它走（权威命中要等一个 RTT，MatchState 的弹匣要等 1Hz，都不能用来驱动反馈）。
        // 用例靠 ShotCount / LastShotSeq / AmmoRejectedTotal 判断"真的响过"，而不是断言一个恒 false 的位。
        public bool LocalShotFired { get; private set; }
        public int LocalShotSlot { get; private set; }
        public int ShotCount { get; private set; }
        public int LastShotSeq { get; private set; }
        public LocalWeapon Weapon { get { return _weapon; } }
        public AmmoView Ammo { get; private set; }
        public int AmmoRejectedTotal { get { return _ammo.RejectedTotal; } }
        public int AmmoPendingShots { get { return _ammo.PendingShots; } }
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
                    // 只复用、**不重建**：数组在构造期分配一次，TryToFrame 负责把计数与内容抄进来。
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
                    var previousPid = LocalPlayerId;
                    ResolveLocalPlayer();
                    if (previousPid != LocalPlayerId) { _weapon.Reset(); _ammo.Reset(); }
                    _ammoDataReady = false;
                    if (state.Players != null && LocalPlayerId != 0)
                    {
                        for (var i = 0; i < state.Players.Length; i++)
                        {
                            var player = state.Players[i];
                            if (player.Pid != LocalPlayerId) continue;
                            _weapon.SyncAuthority(player.Mag, player.Reserve, player.Weapon, player.ReloadLeft10Ms, _nowMs);
                            _ammo.Reconcile(player.Mag, player.Reserve, player.Weapon, (int)_nowMs);
                            _ammoDataReady = true;
                            break;
                        }
                    }
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
            hudEvent.SubjectId = entry.SubjectId;
            hudEvent.TargetId = entry.TargetId;
            hudEvent.Wave = entry.Wave;
            hudEvent.WaveSize = entry.Budget;
            hudEvent.ReviveRatio255 = entry.Ratio255;
            hudEvent.HitX = entry.HitX;
            hudEvent.HitY = entry.HitY;
            hudEvent.HitZ = entry.HitZ;
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
                // 最后一条意图的移动轴（视图模型的行走摆动取它）
                LastMoveX = intent.MoveX;
                LastMoveY = intent.MoveY;
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

        // C06 §5(c)：本地武器镜像 → **当帧**开火反馈。按钮位的判读留在这里（与 resolve.cpp 的分工一致：
        // weapon.cpp 只提供 tryFire/tryStartReload/switchSlot，不认识 buttons），调用顺序也照抄服务端
        // tick：先 updateWeapon（换弹完成 + 散布衰减），再 switchSlot → tryStartReload → tryFire。
        //
        // 为什么按**帧**驱动而不是按 50ms 子步：射速由 `nextFireAllowedAtMs` 把关（服务端也是这么把关的），
        // 所以调用频率不改变射速，只改变"按下到出膛"的粒度。按子步驱动的话，30Hz 采样 + 20Hz 子步
        // 会让按下最多等 83ms 才见枪口火焰——那正是"点了半天才响"的手感。
        private void PumpWeapon(double dtMs)
        {
            var sampler = Sampler;
            var nowMs = _nowMs;
            // rage 倍率（kRageFireRateMultiplier = 1.25）暂不镜像：本地只按 1.0 计时，狂暴窗口里本地
            // 射速比服务端慢 1/5，多出来的那点在弹药账里被权威拉回（见 A21 的残余记录）。
            _weapon.Update(nowMs, dtMs, LocalFireRateMultiplier);
            if (sampler == null) return;
            var buttons = sampler.CurrentButtons;
            if ((buttons & InputSampler.ButtonSwitchWeapon) != 0) _weapon.SwitchSlot(sampler.CurrentSwitchTo, nowMs);
            if ((buttons & InputSampler.ButtonReload) != 0) _weapon.TryStartReload(nowMs);
            if ((buttons & InputSampler.ButtonFire) == 0) return;
            if (!_weapon.TryFire(nowMs, LocalFireRateMultiplier)) return;

            LocalShotFired = true;
            LocalShotSlot = _weapon.Slot;
            ShotCount += 1;
            // 序号取采样器当前 seq：服务端会用**它收到的那条命令**的 seq 派生抖动，两者最多差一条命令
            // （30Hz），不影响本地的枪口/曳光（命中点由权威事件回填，见 ApplyEvent）。
            LastShotSeq = sampler.Seq;
            _ammo.NoteLocalShot(sampler.Seq, LocalShotSlot, (int)nowMs);
            // 后坐当帧顶视角（C09 §5 的 0.35°/±0.2°）：下一帧上行就带上这个偏移，服务端射线跟着后坐走。
            sampler.ApplyAimPunch(WeaponTable.RecoilPitchPerShotDeg, WeaponTable.RecoilYawJitterDeg);
        }

        // C05 §5.4/§5.5 的落地：把**预测姿态**与本机**命令视角**交给实体视图。在这之前渲染路径一直
        // 拿权威快照当本地玩家的姿态（`HasPrediction`/`Predicted*` 在生产里没有任何写入者），
        // 于是本地玩家只能按 20Hz 吸附、鼠标视角被 RTT + 50ms 拖住 —— 实跑就是"移动一顿一顿"。
        //  - 位置取 `Predictor.RenderPosition`（50ms 子步 + 渲染外推），不是裸 state：外推把累加器里的
        //    零头也画出来，画面才在帧率上连续（C05 §5.4）。
        //  - 视角直接取采样器的当前 yaw/pitch（C05 §5.5 冻结：「相机旋转 yaw/pitch 直接取自本机命令
        //    姿态」）。上行命令里的 yaw/pitch 也是这一份，所以"看到的画面"与"打出去的射线"同源。
        //  - 只有本帧确实有本地实体时才置位；否则清掉，免得拿陈旧预测继续渲染。
        private void PublishLocalPrediction()
        {
            EntityView local;
            if (LocalPlayerId == 0 || !_views.TryGet(LocalPlayerId, out local) || local == null)
            {
                _views.ClearPrediction();
                return;
            }
            double x;
            double y;
            double z;
            _predictor.RenderPosition(out x, out y, out z);
            var sampler = Sampler;
            var yaw = sampler != null ? sampler.YawRad : _predictor.State.YawRad;
            var pitch = sampler != null ? sampler.PitchRad : _predictor.State.PitchRad;
            local.HasPrediction = true;
            local.PredictedX = x;
            local.PredictedY = y;
            local.PredictedZ = z;
            local.PredictedYawRad = yaw;
            local.PredictedPitchRad = pitch;
            // 姿态本身也写：最新快照是**差分**帧，本地玩家没变化时不在它的记录里，而 SyncFrame 的
            // 插值循环只遍历差分记录 ⇒ 不写就会停在上一次写入的位置（站着不动看不出来，起步那一拍会顿）。
            local.X = x;
            local.Y = y;
            local.Z = z;
            local.YawRad = yaw;
            local.PitchRad = pitch;
        }

        // 局内聊天（键位表 [ActionChat]）：显隐的唯一真相是相位，与 HUD 用同一条判据（本层不自己推相位）。
        // 读 `_phase`（MatchState 的权威字节）而不是 `_sample.Phase`：后者由帧末的 FillSample 写，
        // 在帧首问它会拿到上一帧的值。
        public bool ChatVisible { get { return Hud.CombatUiVisible(_phase); } }

        // 同一条判据的对外名字：进对局自动锁指针、断线横幅都用它（与 HUD 不各推一套相位）。
        public bool CombatVisible { get { return Hud.CombatUiVisible(_phase); } }

        // 连接状态：给 UI 用。断线时大厅/HUD 必须**说出来** —— 实跑事故就是"缓存着旧的一队人 + 0 B/s +
        // 按键无反应"而界面上没有任何提示，玩家只能认为游戏坏了。
        public bool IsConnected { get { return Transport != null && Transport.State == ConnectionState.Connected; } }
        public ConnectionState Connection { get { return Transport == null ? ConnectionState.Disconnected : Transport.State; } }
        public int ReconnectAttempts { get { return Transport == null ? 0 : Transport.ReconnectAttempts; } }

        public void Frame(double dtMs)
        {
            _profiler.Begin();
            // 当帧信号：上一帧的"响过"不能留到这一帧（否则枪口火焰会一直亮）。
            LocalShotFired = false;

            // ① input：采样 → 上行 + 本地预测 + 本地武器镜像
            PumpInput(dtMs);
            PumpWeapon(dtMs);
            if (_hasPending)
            {
                // 命令缓冲里存的必须是「每个 50ms 子步进的输入」：C06 §5(b) 的重放是逐条
                // `localStep(50ms)`，条数即步数。此前每帧只入队一条（≈30Hz，取决于帧率）而预测按
                // 20Hz 出步，重放出来的时间片比真实经过时间多约 1.5× ⇒ 预测位置被持续拽偏。
                // 上行只发30Hz，预测必须每渲染帧推进；没有新命令时继续持有最近输入。
                // （缓冲现在只保留「步数对齐投影」的计数用，重放已被投影取代，见 Reconciler。）
                var steps = _predictor.Advance(dtMs, _pending);
                _predictionSteps += steps;
                for (var i = 0; i < steps; i++) _commands.Push(_pending);
            }
            _profiler.Mark(FrameStage.Input);

            // ② sync：收包（离线时什么都没发生）
            if (Transport != null)
            {
                // 会话换了（重连 / 服务端重启后的新会话）⇒ 镜像与 tick 原点一并复位。不复位就会
                // "连上了却什么都动不了"：旧的高 tick 让新服务端的低 tick 快照过不了单调过滤，
                // 而命令带着高 tick 又被判 kFutureTick 全丢（实跑实测）。
                // 只在“真的重连过”时复位镜像与 tick 原点（计数器只在 BeginReconnect 里加）。
                // 不能用“会话号变了”做触发：断线那一帧会话会变 0，把刚应用的快照也一并丢掉
                // （用例 boot.command_uplink / boot.command_tick_from_snapshot 抓到的就是这个）。
                var reconnects = Transport.ReconnectAttempts;
                if (reconnects != _mirrorReconnectCount)
                {
                    _mirrorReconnectCount = reconnects;
                    _view.ResetForNewSession();
                    // 重连换了会话：未确认开火与本地武器镜像一并复位（旧会话的 seq 在新会话里毫无意义，
                    // 留着会让弹药账把新会话的前 20 条命令都当成"已确认"而丢弃）。
                    _weapon.Reset();
                    _ammo.Reset();
                    _ammoDataReady = false;
                }
                Transport.Poll(MaxInboundPerPoll);
                FlushJoin();
            }
            // 本帧之前有没有**新的**权威快照进来：和解只在"有新权威"时发生（C06 §8：重放只在收到
            // 权威快照时发生）。判据用已应用快照的 tick，不用"本帧收到过包"：快照也可能由装配根或
            // 帧基准直接喂（`ApplySnapshot`），那条路同样必须触发和解，否则本地预测就与权威失联。
            var appliedTick = _view.AppliedTick;
            var hasNewAuthority = !_reconciledOnce || appliedTick != _lastReconciledTick;
            _profiler.Mark(FrameStage.Sync);

            // ③ predict：本地权威 + 和解
            LocalAuthority authority;
            if (hasNewAuthority && LocalAuthority.TryProject(_view, out authority))
            {
                _reconciledOnce = true;
                _lastReconciledTick = appliedTick;
                Reconciles += 1;
                // 残留①③修复：把权威投影到客户端当前预测网格再对账（Reconciler 落地投影后的
                // 基线，重放被投影取代，见 Reconciler.Reconcile 的注释）。权威快照是服务端
                // tick T 时刻（T−1 步）的位置，客户端此刻已预测到 _predictionSteps 步；直接拿它
                // 当基线，相位级的 0/1 子步差（0.005/0.225m 交替）每次都进误差 ⇒ 平滑器被周期性
                // 灌偏移，移动画面来回抖。这里按步数差外推（本地速度 × extraSteps×50ms）：恒定
                // 移动时投影位置 ≡ 本地预测位置 ⇒ 误差恒 ≈ 0（丢包也一样——服务端持最近命令，
                // 丢包不缺步）；输入真的变了（转向/变速）才出现误差，该纠正的纠正。墙钟年龄
                // （now−serverTimeMs）投影在到达抖动下仍残留相位差（0.005~0.155m），步数对齐
                // 投影与预测网格同构，无残留。
                // 但"步数对齐"的前提是权威 tick 与本地预测同一条 20Hz 时钟。实跑发现这个前提
                // 在大厅不成立：服务端世界只在 kPlaying 步进（room.cpp 的 stepWorld），大厅里
                // world->tick 冻结不变，而客户端一按方向键 _predictionSteps 照常累计 ⇒
                // extraSteps = _predictionSteps − (冻结tick−1) 每步 +1 无限膨胀 ⇒ 权威被外推得
                // 越来越远，预测被反复拽回 ⇒ 画面一直闪 + 移动"失效"。投影本意只补偿本会话的
                // RTT 往返相位（≈ 0..几个 tick）；超过窗口（时钟失联/冻结/换会话/漂移）就退回
                // 原始权威，把小的相位差交给平滑器，而不是把一条失联时钟的膨胀当成相位差灌进去。
                var extraSteps = _predictionSteps - ((int)authority.Tick - 1);
                // 步数对齐的前提是两条 20Hz 时钟同原点。实跑发现会失联的场景：
                //  · 大厅走动：服务端世界只在 kPlaying 步进（room.cpp stepWorld），大厅里 world->tick
                //    冻结，而客户端 _predictionSteps 照常累计 ⇒ 把大厅积累的偏移带进对局；
                //  · 时钟漂移：两台机器的 20Hz 不是严格同频，长期对局步数差缓慢漂移；
                //  · 换会话/换房间：tick 原点重置（ClearIdentity 已归零，这里兜底）。
                // 失联时 extraSteps 带着与相位无关的大偏移：要么远超窗口（正向），要么为负
                // （权威 tick 已领先预测）。把偏移当相位差灌进投影会把玩家外推到错误位置。
                // 处理：按权威 tick 重锚预测步数，丢掉失联偏移，此后投影恢复小窗口补偿。
                if (extraSteps > MaxProjectionSteps || extraSteps < 0)
                {
                    _predictionSteps = (int)authority.Tick - 1;
                    extraSteps = 0;
                }
                else if (extraSteps > 0)
                {
                    var extrapMs = extraSteps * LocalStep.StepDtMs;
                    authority.X += _predictor.State.Vx * extrapMs / 1000.0;
                    authority.Y += _predictor.State.Vy * extrapMs / 1000.0;
                    authority.Z += _predictor.State.Vz * extrapMs / 1000.0;
                }
                var result = _reconciler.Reconcile(authority, _commands, _predictor);
                LastReplayed = result.Replayed;
                LastReconcileErrorM = result.ErrorM;
                if (result.HardCorrect) HardCorrects += 1;
                // 实机排障观测：节流记录每次和解的时钟对齐与误差（大厅/对局 tick 冻结、漂移、
                // 硬纠频次都能从这里看出来）。HardCorrect 单独记（不节流），其他每 25 次记一条。
                // 批处理自检（isBatchMode）跳过：Debug.Log 会分配，破坏 steady-state 零分配用例。
                if (!UnityEngine.Application.isBatchMode
                    && (result.HardCorrect || (Reconciles % 25) == 0))
                {
                    UnityEngine.Debug.Log("Ac.Boot: 和解 tick=" + authority.Tick
                        + " 预测步=" + _predictionSteps + " 投影步=" + extraSteps
                        + " 误差=" + result.ErrorM.ToString("F4") + "m 硬纠=" + result.HardCorrect);
                }
                EntityView local;
                if (_views.TryGet(LocalPlayerId, out local) && result.ErrorM > 0.0)
                {
                    // 交给平滑器的是**姿态位移量**（C06 §5(b)：和解前 − 重放后），不是"重放后 ± 位移"
                    // 这类拼出来的位置。此前传的是 `State - Offset`（= 2×重放后 − 和解前），每帧往
                    // 偏移里灌进约一个帧位移量级，量级越过 1.0m 吸附阈值就 Reset 归零 ⇒ 渲染位置在
                    // 「权威+大偏移」与「权威+0」之间来回跳，实跑就是"移动一下疯狂闪烁、来回闪烁"。
                    _views.ApplyPoseDelta(local, result.OffsetX, result.OffsetY, result.OffsetZ);
                }
                // 弹药账的确认水位：服务端"已收到哪条命令"的最新值就在这份权威投影里。
                // 没有这一步，未确认开火永远等不到确认，只有超时丢弃一条路（弹匣会一直偏低）。
                _ammo.NoteServerAck(authority.LastAckedSeq);
            }
            _profiler.Mark(FrameStage.Predict);

            // ④ 视图：渲染时钟推进 + 镜像同步
            _nowMs += dtMs;
            _clock.Advance(dtMs);
            PublishLocalPrediction();   // 必须早于 SyncFrame：SyncFrame 读 Predicted*，有预测值就不吸附镜像
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
            if (!transport.SendJoin(_localName)) return;
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

            _sample.AmmoDataReady = _ammoDataReady;
            _sample.Rage = 0;
            _sample.RageLeft100Ms = 0;
            if (_ammoDataReady)
            {
                Ammo = new AmmoView { Mag = _weapon.ActiveMag, Reserve = _weapon.Reserve, Slot = _weapon.Slot, GateMag = _weapon.ActiveMag };
                _sample.Mag = Ammo.Mag;
                _sample.Reserve = Ammo.Reserve;
                _sample.MagSize = WeaponTable.MagSizeOf(Ammo.Slot);
                _sample.Reloading = _weapon.IsReloading;
                var remaining = _weapon.ReloadRemainingMs(_nowMs);
                _sample.ReloadLeft10Ms = (int)System.Math.Ceiling(remaining / 10.0);
                _sample.ReloadProgress = _weapon.IsReloading ? 1f - (float)(remaining / WeaponTable.ReloadMs[_weapon.Slot]) : 0f;
                _sample.SpreadDeg = WeaponTable.CrosshairSpreadDeg(Ammo.Slot, (float)_weapon.SpreadDeg);
            }
            else
            {
                Ammo = default(AmmoView);
                _sample.Mag = 0;
                _sample.Reserve = 0;
                _sample.Reloading = false;
                _sample.ReloadLeft10Ms = 0;
                _sample.ReloadProgress = 0f;
            }
            var players = LastMatchState.Players;
            if (players == null || LocalPlayerId == 0) return;
            for (var i = 0; i < players.Length; i++)
            {
                if (players[i].Pid != LocalPlayerId) continue;
                _sample.Rage = players[i].Rage;
                _sample.RageLeft100Ms = players[i].RageLeft100Ms;
                return;
            }
        }
    }
}
