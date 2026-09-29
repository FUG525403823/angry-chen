using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Threading;
using Ac.Core;
using Ac.UI;
using Ac.Net;

namespace Ac.Tests
{
    // C15 §5 的 6 步联调（真 UDP，真服务端），**一条会话走完**：连接 → 大厅 → 对局 → 波次 → 结算 → 重连。
    // 为什么是一条而不是六条用例：服务端给离开的玩家留 30s 宽限（ADR-006），宽限里的名额仍然占着
    //（实测：4 个 bot 离开后再连 4 个会话，收不到任何快照）。六条独立用例各自开一条会话，第二、三条
    // 就会被前一条的宽限名额挡住 —— 那不是被测系统的缺陷，是测试自己造的。checklist 本身就是线性流程。
    //
    // 默认不跑：AC_JOINT_UDP 未设置 ⇒ 打印 [joint] skipped 并放行（无服务端时 selftest 仍然全绿）；
    // 设了却连不上/对不上 ⇒ 一律 FAIL，不允许静默降级。
    //
    // 环境变量（client/tools/joint-acceptance.ps1 注入）：
    //  AC_JOINT_UDP=host:port   AC_JOINT_SERVER_LINE=ac_server 0.1.0 protocol=1 tick=50ms
    //  AC_JOINT_NAME（默认 12 字节"牧羊人阿"）  AC_JOINT_BOTS（房间里已有的队友数）
    //  AC_JOINT_BOX_MS（对局观察时间盒，默认 90000）
    public static class JointSuite
    {
        public const string EnvUdp = "AC_JOINT_UDP";
        public const string EnvServerLine = "AC_JOINT_SERVER_LINE";
        public const string EnvName = "AC_JOINT_NAME";
        public const string EnvBots = "AC_JOINT_BOTS";
        public const string EnvBoxMs = "AC_JOINT_BOX_MS";
        public const string EnvTeam = "AC_JOINT_TEAM";

        private const double ConnectTimeoutMs = 5000.0;   // 第 1 步：≤ 5s 进大厅
        private const int CommandHz = 20;                 // 命令上行频率
        private const byte ReadyBit = (byte)CommandButtons.Ready;

        public static void Register()
        {
            SelfTest.Add("joint.acceptance", ChecksAcceptance);
        }

        private static void ChecksAcceptance()
        {
            if (!Configured(out var host, out var port)) return;

            var link = new Link();
            link.Transport.Connect(host, port);
            var started = link.NowMs;
            var connected = link.PumpUntil(delegate { return link.Transport.State == ConnectionState.Connected; }, ConnectTimeoutMs);
            var connectMs = link.NowMs - started;
            SelfTest.True(connected, "第 1 步：5s 内进入 Connected", link.Transport.State.ToString());
            SelfTest.True(connectMs <= ConnectTimeoutMs, "第 1 步：握手耗时 ≤ 5000ms", connectMs.ToString("F0"));

            // 第 1 步：两侧版本行必须相容（proto == protocol、MAJOR.MINOR 相等）。
            var serverLine = Env(EnvServerLine);
            SelfTest.True(!string.IsNullOrEmpty(serverLine), "第 1 步：" + EnvServerLine + " 必须由脚本注入", "空");
            string reason;
            SelfTest.True(VersionInfo.CompatibleWith(serverLine, out reason), "第 1 步：版本行相容", reason ?? "不相容");
            Console.WriteLine("[joint] step1 ok connectMs=" + connectMs.ToString("F0") + " session=" + link.Transport.Session
                + " client=" + VersionInfo.VersionLine + " server=" + serverLine);

            // 第 2 步：大厅。kJoin 上昵称（type=11），MatchState 必须出现该昵称的玩家行且 ready=false。
            var name = Name();
            SelfTest.True(link.Transport.SendJoin(name), "第 2 步：kJoin 必须被受理（昵称 1..12 字节）", name);
            SelfTest.True(link.PumpUntil(delegate { return LocalRow(link, name).HasValue; }, 5000.0),
                "第 2 步：5s 内 MatchState 出现本地昵称的玩家行", link.MatchStateSummary());
            var row = LocalRow(link, name);
            SelfTest.True(!row.Value.Ready, "第 2 步：新进大厅 ready=false", row.Value.Ready.ToString());
            SelfTest.True(link.PlayerCount > 0 && link.PlayerCount <= MatchStateCodec.MaxPlayers,
                "第 2 步：玩家行数 1..4", link.PlayerCount.ToString());
            var pid = row.Value.Pid;
            Console.WriteLine("[joint] step2 ok pid=" + pid + " name=" + name + " rows=" + link.PlayerCount
                + " readies=" + link.ReadySummary() + " bots=" + Env(EnvBots));
            Console.WriteLine("[joint] step2 诊断 " + link.DiagnosticSummary());

            // 队友由脚本在此刻前后拉起（客户端先进大厅，队友才进场），所以第 2 步之后必须先等到齐：
            // 不满员时新会话是**合法**的，拿它去断言"被拒"只会得到一个假红。
            var bots = 0;
            int.TryParse(Env(EnvBots), out bots);
            var team = bots + 1;
            var parsedTeam = 0;
            if (int.TryParse(Env(EnvTeam), out parsedTeam) && parsedTeam > 0) team = parsedTeam;
            var teamReady = link.PumpUntil(delegate { return link.PlayerCount >= team; }, 60000.0);
            Console.WriteLine("[joint] step2c 队友 " + link.PlayerCount + "/" + team + " 到齐=" + (teamReady ? 1 : 0)
                + " readies=" + link.ReadySummary());
            Console.WriteLine("[joint] step2c 诊断 " + link.DiagnosticSummary());
            SelfTest.True(teamReady, "第 2 步：60s 内队友必须进房（脚本用 ac_bot 起满房）", link.MatchStateSummary()
                + " | " + link.DiagnosticSummary());

            // 第 2 步（名额上限）：房间被队友占满时，第 5 个会话必须收到明确回执（ADR-012），
            // 且不得进玩家表。此前这里是"静默丢弃准入结果"——客户端挂在"已连接但收不到 MatchState"。
            ExpectRefused(host, port, "第 2 步：满员房", "第 5 人", link);

            // 第 3 步：带 Ready 位的命令把房间推进到 playing；顺带量快照率。
            // 房主是第一个进房的人 —— 上面已经等到齐，现在按准备才是 checklist 第 3 步的场景（4 人同局）。
            // **Ready 位必须到这里才举**：提前举会让"房内只有自己"也算全员准备（areAllPlayersReady 只看在房玩家），
            // 房主一个人就把对局开了，随后进场的队友全部撞上"对局进行中"被拒（上一轮联调实测到的就是这个）。
            link.ArmReady = true;
            var box = 90000.0;
            double parsedBox;
            if (double.TryParse(Env(EnvBoxMs), out parsedBox) && parsedBox > 1000.0) box = parsedBox;
            var deadline = link.NowMs + box;
            var rateStart = 0.0;
            var rateBase = 0;
            var worstRateHz = double.MaxValue;
            var maxWave = 0;
            var sawIntermission = false;
            var intermissionMs = 0;
            var midMatchRefused = false;
            // 「推进到 playing」必须**锁存**，不能在箱子末尾读瞬时相位：全员准备后对局会自己重开
            // （一局全灭 → 立刻 resetMatchForRestart → 又进 loading/playing），90s 的箱子结束时相位
            // 可能正好落回 lobby —— 上一轮联调就是这么红的（actual=phase=0，可 /api/matches/recent
            // 里那两局都真跑过）。
            var sawPlaying = false;
            var lastReportMs = 0.0;

            while (link.NowMs < deadline)
            {
                var now = link.NowMs;
                link.KeepAlive();   // 20Hz 上行（带不带 Ready 位由 ArmReady 决定）+ 收包

                if (link.MatchState.Phase == Hud.PhaseIntermission && link.MatchState.IntermissionMs > 0)
                {
                    sawIntermission = true;
                    intermissionMs = link.MatchState.IntermissionMs;
                }
                if (link.MatchState.Phase == Hud.PhasePlaying) sawPlaying = true;
                if (link.MatchState.Wave > maxWave) maxWave = link.MatchState.Wave;

                // 第 4 步（对局进行中的新会话）：这正是联调抓到的原始症状 —— 房间在 playing 时新会话只拿到
                // HelloAck、永远收不到 MatchState（rows=0 matchStates=0）。ADR-012 之后必须收到 reason=8。
                if (!midMatchRefused && link.MatchState.Phase == Hud.PhasePlaying && link.SnapshotCount > 0)
                {
                    midMatchRefused = true;
                    ExpectRefused(host, port, "第 4 步：对局进行中", "新会话", link);
                }

                if (link.MatchState.Phase == Hud.PhasePlaying || link.MatchState.Phase == Hud.PhaseIntermission)
                {
                    if (rateStart <= 0.0) { rateStart = now; rateBase = link.SnapshotCount; }
                    if (now - rateStart >= 5000.0)
                    {
                        var hz = (link.SnapshotCount - rateBase) * 1000.0 / (now - rateStart);
                        if (hz < worstRateHz) worstRateHz = hz;
                        rateStart = now;
                        rateBase = link.SnapshotCount;
                    }
                }
                if (link.MatchState.Phase == Hud.PhaseEnded) break;
                // 每 10s 打一行进度：联调失败时"卡在哪一步"必须在一次运行里就能看出来（上一次是跑完
                // 90s 才红，日志里只有最后一帧相位，白烧一轮）。
                if (now - lastReportMs >= 10000.0)
                {
                    lastReportMs = now;
                    Console.WriteLine("[joint] step3 t=" + (int)(now / 1000.0) + "s " + link.MatchStateSummary()
                        + " " + link.ReadySummary() + " serverTick=" + link.ServerTick
                        + " commands=" + link.CommandsSent + " snaps=" + link.SnapshotCount
                        + " state=" + link.Transport.State);
                }
                // 本步的验收项全部拿到就收工（sawPlaying = Ready 位真的开了局；midMatchRefused = 第 4 步的
                // 对局中拒绝已探到；worstRateHz 有值 = 至少量满一个 5s 快照率窗口）。否则箱子会一路跑到
                // 对局重开，最后落在哪个相位全看运气。
                if (sawPlaying && midMatchRefused && worstRateHz != double.MaxValue) break;
                Thread.Sleep(2);
            }

            SelfTest.True(sawPlaying, "第 3 步：带 Ready 位的命令必须把房间推进到 playing",
                "phase=" + link.MatchState.Phase + " wave=" + maxWave + " " + link.MatchStateSummary());
            SelfTest.True(link.SnapshotCount > 0, "第 3 步：对局里必须收到快照", "0 份");
            if (worstRateHz != double.MaxValue)
                SelfTest.True(worstRateHz >= 10.0, "第 3 步：快照率 ≥ 10Hz（C15 允许自适应降档）", worstRateHz.ToString("F1") + " Hz");
            SelfTest.True(link.Transport.Stats.RttMs <= 120.0, "第 3 步：ping ≤ 120ms", link.Transport.Stats.RttMs.ToString("F1"));
            SelfTest.True(link.Transport.Stats.PacketLossPermille <= 50, "第 3 步：丢包 ≤ 5%", link.Transport.Stats.PacketLossPermille.ToString());
            Console.WriteLine("[joint] step3 ok phase=" + link.MatchState.Phase + " snapshots=" + link.SnapshotCount
                + " events=" + link.EventCount + " rttMs=" + link.Transport.Stats.RttMs.ToString("F1")
                + " lossPermille=" + link.Transport.Stats.PacketLossPermille
                + " worstSnapshotHz=" + (worstRateHz == double.MaxValue ? "-" : worstRateHz.ToString("F1"))
                + " commandsSent=" + link.CommandsSent);

            // 第 4 步：波次。这里只断言"波次机制在对局里真的跑起来了"，**不断言 checklist 的"第 3 波"**：
            //  · 推进波次要把本波羊清空，而装载 bot（ac_bot）只按 1/4 概率乱开火、不瞄准（bot.cpp:174），
            //    脚本化的本机也同样不会瞄准 —— 全员被吃 → match_controller 的 allDowned 直接判羊群获胜
            //    （实测 3 bot 的上一局：26.5s、waveReached=1、每个 bot downs=1）。"第 3 波"需要人类会话。
            //  · 四类羊形**线上不可区分**：kind 只有 player/sheep/projectile/pickup 四档（SnapshotCodec 的
            //    位序注释），羊形只能从 state 推出「羊王(9..12) vs 其余」，ram/elite 两档在快照里不存在
            //（PresentationLayer.SheepFormOf 的注释已登记同一冲突）。四类由服务端侧的 S07/S09 对拍覆盖。
            SelfTest.True(maxWave >= 1, "第 4 步：波次机制必须跑起来（≥ 1）", maxWave.ToString());
            Console.WriteLine("[joint] step4 ok wave=" + maxWave + " intermissionSeen=" + (sawIntermission ? 1 : 0)
                + " intermissionMs=" + intermissionMs + " sheepForms=" + link.SheepFormSummary()
                + "（第 3 波需人类会话；四类羊形线上不可区分）");

            // 第 5 步：结算。客户端没有 HTTP 客户端，四字段比对由脚本侧读 /api/matches/recent 完成；
            // 这里把客户端自己看到的口径打出来给脚本核。
            Console.WriteLine("[joint] result ended=" + (link.MatchState.Phase == Hud.PhaseEnded ? 1 : 0)
                + " phase=" + link.MatchState.Phase + " wave=" + maxWave
                + " kills=" + link.KillsSummary() + " pid=" + pid);

            // 第 6 步：断网 10s（整段不 Poll：不心跳、不收包），恢复后必须复用同一 session 与 pid。
            var session = link.Transport.Session;
            Console.WriteLine("[joint] step6 断网 10s（session=" + session + " pid=" + pid + "）");
            Thread.Sleep(10000);
            SelfTest.True(link.PumpUntil(delegate { return link.Transport.State == ConnectionState.Connected; }, 15000.0),
                "第 6 步：恢复后必须回到 Connected（30s 宽限期内 Resume）", link.Transport.State.ToString());
            SelfTest.True(link.Transport.Session == session, "第 6 步：必须复用同一个 session", link.Transport.Session.ToString());
            SelfTest.True(link.PumpUntil(delegate { return LocalRow(link, name).HasValue; }, 5000.0),
                "第 6 步：恢复后玩家行必须回来", link.MatchStateSummary());
            SelfTest.True(LocalRow(link, name).Value.Pid == pid, "第 6 步：pid 必须与断网前相同",
                LocalRow(link, name).Value.Pid + " vs " + pid);
            Console.WriteLine("[joint] step6 ok session=" + link.Transport.Session + " pid=" + pid
                + "（40s 断网的名额释放由脚本按 /health 的 graceActive 核对）");
            link.Close();
        }

        // ---- 环境与工具 ------------------------------------------------------------------------------

        // ADR-012：被拒的会话必须拿到明确回执（Disconnect reason=8），且不得进玩家表。
        // 修好之前这里是"静默丢弃"：会话在册、HelloAck 已回，客户端只会一直等 MatchState。
        // keepAlive 是本机那条主链路：探别人期间它必须继续心跳/命令，否则本机会被自己的探针探成失联。
        private static void ExpectRefused(string host, int port, string step, string who, Link keepAlive)
        {
            var link = new Link();
            link.Transport.Connect(host, port);
            var deadline = link.NowMs + 5000.0;
            while (link.NowMs < deadline && !link.Transport.Machine.DisconnectedByServer)
            {
                link.Transport.Poll(UdpTransport.MaxInboundPacketsPerPoll);
                if (keepAlive != null) keepAlive.KeepAlive();
                Thread.Sleep(2);
            }
            SelfTest.True(link.Transport.Machine.DisconnectedByServer,
                step + "：" + who + " 必须收到服务端回执（不得静默）",
                link.Transport.State.ToString() + " matchStates=" + link.MatchStateCount);
            SelfTest.True(link.Transport.Machine.LastDisconnectReason == DisconnectReason.RoomUnavailable,
                step + "：" + who + " 的回执原因必须是 8（roomUnavailable）",
                ((long)link.Transport.Machine.LastDisconnectReason).ToString());
            SelfTest.True(!LocalRow(link, "intruder9").HasValue, step + "：" + who + " 不得进玩家表",
                link.MatchStateSummary());
            Console.WriteLine("[joint] " + step + " ok " + who + " 被拒 reason="
                + (long)link.Transport.Machine.LastDisconnectReason + " players=" + link.PlayerCount
                + " matchStates=" + link.MatchStateCount);
            link.Close();
        }

        private static bool Configured(out string host, out int port)
        {
            host = null;
            port = 0;
            var value = Env(EnvUdp);
            if (string.IsNullOrEmpty(value))
            {
                Console.WriteLine("[joint] skipped：" + EnvUdp + " 未设置（无服务端时不跑联调）");
                return false;
            }
            var colon = value.LastIndexOf(':');
            if (colon <= 0) throw new InvalidOperationException(EnvUdp + " 必须是 host:port，实际=" + value);
            host = value.Substring(0, colon);
            port = int.Parse(value.Substring(colon + 1));
            return true;
        }

        internal static string Env(string key) { return Environment.GetEnvironmentVariable(key) ?? string.Empty; }

        private static string Name()
        {
            var name = Env(EnvName);
            return string.IsNullOrEmpty(name) ? "牧羊人阿" : name;   // 12 字节：昵称上限的边界值
        }

        // LocalIdentity 的口径：昵称严格相等取最小 pid（服务端不保证唯一，验收只按昵称取行）。
        private static MatchStatePlayer? LocalRow(Link link, string name)
        {
            var players = link.MatchState.Players;
            if (players == null) return null;
            MatchStatePlayer? found = null;
            for (var i = 0; i < players.Length; i++)
            {
                if (!string.Equals(players[i].Name, name, StringComparison.Ordinal)) continue;
                if (!found.HasValue || players[i].Pid < found.Value.Pid) found = players[i];
            }
            return found;
        }

        // 联调诊断用的套接字壳：在传输层解析**之前**数原始数据报，并按 type 字节分流。
        // "客户端没收到" 与 "收到了但在传输层被丢掉" 是两类完全不同的故障，只有这一层能分开它们。
        private sealed class CountingSocket : IDatagramSocket
        {
            private readonly IDatagramSocket _inner;
            public readonly Dictionary<int, int> BytesByType = new Dictionary<int, int>();
            public int Datagrams;
            public int LastDatagramBytes;

            public CountingSocket(IDatagramSocket inner) { _inner = inner; }

            public bool IsBound { get { return _inner.IsBound; } }
            public int Port { get { return _inner.Port; } }
            public bool HasDatagram { get { return _inner.HasDatagram; } }
            public bool Bind(int port) { return _inner.Bind(port); }
            public void Connect(string host, int port) { _inner.Connect(host, port); }
            public void Close() { _inner.Close(); }
            public bool Send(byte[] datagram, int length) { return _inner.Send(datagram, length); }

            public int Receive(byte[] buffer)
            {
                var length = _inner.Receive(buffer);
                if (length <= 0) return length;
                Datagrams += 1;
                LastDatagramBytes = length;
                var type = length > PacketWriter.TypeOffset ? buffer[PacketWriter.TypeOffset] : -1;
                int count;
                BytesByType.TryGetValue(type, out count);
                BytesByType[type] = count + 1;
                return length;
            }

            public string TypeHistogram()
            {
                var text = string.Empty;
                foreach (var pair in BytesByType)
                {
                    if (text.Length > 0) text += ",";
                    text += "t" + pair.Key + ":" + pair.Value;
                }
                return text.Length == 0 ? "-" : text;
            }
        }

        // 真套接字 + 真实时钟的联调链路：Poll 驱动、ApplicationPacket 收包。
        private sealed class Link
        {
            private readonly Stopwatch _watch = Stopwatch.StartNew();
            private readonly Dictionary<ushort, byte> _sheepStates = new Dictionary<ushort, byte>();
            private readonly CountingSocket _socket = new CountingSocket(new UdpTransport.RealUdpSocket());

            public readonly UdpTransport Transport;

            public MatchStatePayload MatchState;
            public int MatchStateCount;
            // 收到的 MatchState 帧数 / 解不开的帧数：区分"没收到"与"收到了但解不开"。
            public int MatchStateFrames;
            public int MatchStateFailures;
            public int MatchStateLastBytes;
            public string MatchStateLastFailure = "-";
            public int SnapshotCount;
            public int EventCount;
            public int KingRecords;
            public int NonKingSheepRecords;
            public int CommandsSent;
            public ushort Seq;
            public uint ClientTick;
            // 最近一帧快照里的服务端权威 tick：命令的 clientTick 只能填它（严格相等才能被采纳）。
            public uint ServerTick;
            // Ready 位开关：大厅里必须为 false（否则房主会一个人开局，队友全被"对局进行中"挡在门外）。
            public bool ArmReady;
            private double _nextSendMs;

            public Link()
            {
                Transport = new UdpTransport(_socket);
                Transport.ApplicationPacket += OnPacket;
            }

            // 20Hz 上行 + 收包；ArmReady 之前不带 Ready 位（服务端按位判定，重发幂等）。
            // clientTick 必须**回填服务端权威 tick**（validate.cpp 的 validateClientTick 是严格相等：过期
            // 或未来一律 dropped，不预支）。自己数一个自由计数器必然漂移 —— 联调实测过：客户端的准备位
            // 时灵时不灵（命令只在计数器与服务端 tick 恰好撞上时才被采纳），而对局里的移动/开火全部被丢。
            // 每次发送都重新读一次，所以漂移只在当前这一条命令上，下一条自动纠正。
            public void KeepAlive()
            {
                if (NowMs >= _nextSendMs)
                {
                    var command = default(CommandPayload);
                    command.Buttons = ArmReady ? ReadyBit : (byte)0;
                    command.Seq = Seq;
                    command.ClientTick = ServerTick;
                    ClientTick = ServerTick;
                    Transport.Send(PacketType.Command, CommandCodec.Encode(command));
                    Seq = unchecked((ushort)(Seq + 1));
                    CommandsSent += 1;
                    _nextSendMs = NowMs + 1000.0 / CommandHz;
                }
                Transport.Poll(UdpTransport.MaxInboundPacketsPerPoll);
            }

            public double NowMs { get { return _watch.Elapsed.TotalMilliseconds; } }
            public int PlayerCount { get { return MatchState.Players == null ? 0 : MatchState.Players.Length; } }

            private void OnPacket(PacketHeader header, byte[] payload)
            {
                if (header.Type == PacketType.MatchState)
                {
                    MatchStateFrames += 1;
                    MatchStateLastBytes = payload == null ? 0 : payload.Length;
                    MatchStatePayload state;
                    var failure = MatchStateCodec.Decode(payload, out state);
                    if (failure == DecodeFailure.Ok)
                    {
                        MatchState = state;
                        MatchStateCount += 1;
                    }
                    else
                    {
                        // 解不开的 MatchState 是"服务端说 4 个人、客户端只看到 1 行"这类现象的第一现场：
                        // 必须把失败原因与长度记下来，否则只能看到 staled 的旧状态。
                        MatchStateFailures += 1;
                        MatchStateLastFailure = failure.ToString();
                    }
                    return;
                }
                if (header.Type == PacketType.Snapshot)
                {
                    SnapshotPayload snapshot;
                    if (SnapshotCodec.Decode(payload, out snapshot) == DecodeFailure.Ok)
                    {
                        SnapshotCount += 1;
                        ServerTick = snapshot.Tick;   // 命令的 clientTick 回填它（见 KeepAlive）
                        Accumulate(snapshot);
                    }
                    return;
                }
                if (header.Type == PacketType.Event) EventCount += 1;
            }

            // 差分快照会省略没变的实体 ⇒ 羊形统计必须跨帧累积（全量帧先清空）。
            private void Accumulate(SnapshotPayload snapshot)
            {
                if (snapshot.IsFull) _sheepStates.Clear();
                var records = snapshot.Records;
                for (var i = 0; i < records.Length; i++)
                {
                    var record = records[i];
                    if ((record.KindFlags & EntityRecord.KindMask) != 1) continue;   // 1 = 羊
                    _sheepStates[record.Id] = record.State;
                }
                for (var i = 0; i < snapshot.RemovedIds.Length; i++) _sheepStates.Remove(snapshot.RemovedIds[i]);
                KingRecords = 0;
                NonKingSheepRecords = 0;
                foreach (var pair in _sheepStates)
                {
                    if (pair.Value >= (byte)Ac.View.SheepAnim.KingIdle) KingRecords += 1;
                    else NonKingSheepRecords += 1;
                }
            }

            public bool PumpUntil(Func<bool> done, double timeoutMs)
            {
                var deadline = NowMs + timeoutMs;
                while (NowMs < deadline)
                {
                    KeepAlive();
                    if (done()) return true;
                    Thread.Sleep(2);
                }
                return done();
            }

            public void Close() { Transport.Close(); }

            public string MatchStateSummary()
            {
                return "phase=" + MatchState.Phase + " wave=" + MatchState.Wave + " rows=" + PlayerCount
                    + " matchStates=" + MatchStateCount;
            }

            // 传输层与 MatchState 解码的一次性诊断快照：联调失败时"卡在哪一层"必须当场看得见。
            public string DiagnosticSummary()
            {
                return "msFrames=" + MatchStateFrames + " msOk=" + MatchStateCount
                    + " msFail=" + MatchStateFailures + " msLastBytes=" + MatchStateLastBytes
                    + " msLastFail=" + MatchStateLastFailure
                    + " snapshots=" + SnapshotCount + " events=" + EventCount
                    + " commands=" + CommandsSent + " seq=" + Seq + " tick=" + ClientTick
                    + " state=" + Transport.State
                    + " dup=" + Transport.Stats.Duplicates
                    + " invalid=" + Transport.Stats.InvalidPackets
                    + " retx=" + Transport.Stats.Retransmits
                    + " loss=" + Transport.Stats.PacketLossPermille
                    + " rtt=" + Transport.Stats.RttMs.ToString("F1")
                    + " outMatchState=" + Transport.OutstandingReliable(PacketType.MatchState)
                    + " outSnapshot=" + Transport.OutstandingReliable(PacketType.Snapshot)
                    + " rawIn=" + _socket.Datagrams + " rawLast=" + _socket.LastDatagramBytes
                    + " byType=" + _socket.TypeHistogram();
            }

            public string SheepFormSummary()
            {
                return "king=" + KingRecords + " nonKing=" + NonKingSheepRecords;
            }

            public string ReadySummary()
            {
                var players = MatchState.Players;
                if (players == null || players.Length == 0) return "-";
                var text = string.Empty;
                for (var i = 0; i < players.Length; i++)
                {
                    if (i > 0) text += ",";
                    text += players[i].Ready ? "1" : "0";
                }
                return text;
            }

            public string KillsSummary()
            {
                var players = MatchState.Players;
                if (players == null || players.Length == 0) return "-";
                var text = string.Empty;
                for (var i = 0; i < players.Length; i++)
                {
                    if (i > 0) text += ",";
                    text += players[i].Name + ":" + players[i].Kills;
                }
                return text;
            }
        }
    }
}

