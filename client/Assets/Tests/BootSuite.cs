using System;
using Ac.Boot;
using Ac.Core;
using Ac.Sim;
using Ac.UI;
using Ac.View;

namespace Ac.Tests
{
    // 运行期装配根（Ac.Boot.GameLoop）的无头用例：游戏循环此前完全不存在，这里给它上闸。
    internal static class BootSuite
    {
        public static void Register()
        {
            SelfTest.Add("boot.frame_loop", ChecksFrameLoop);
            SelfTest.Add("boot.steady_state_zero_alloc", ChecksSteadyStateZeroAlloc);
            SelfTest.Add("boot.stage_sinks", ChecksStageSinks);
            SelfTest.Add("boot.server_config", ChecksServerConfig);
            SelfTest.Add("boot.join_reported_once_per_name", ChecksJoinReport);
            SelfTest.Add("boot.command_uplink", ChecksCommandUplink);
        }

        // C05 §5.1/§5.2 的产品上行：采样器出的命令必须真的从传输发出去（type=4），且 `clientTick`
        // 等于**最近一条权威快照的 tick**。服务端 `security::validateClientTick` 要求与服务端 tick
        // 严格相等（预支/滞后一样丢）—— 联调里战绩 `aliveMs: 0` 就是这条链路没接线的后果。
        private static void ChecksCommandUplink()
        {
            var now = 0.0;
            var socket = new ScriptedSocket();
            var transport = new Ac.Net.UdpTransport(socket, delegate { return now; });
            SnapshotFrame frame;
            var loop = NewLoop(2, out frame);
            loop.Transport = transport;
            var sampler = new InputSampler();
            sampler.SetKey(UnityEngine.KeyCode.W, true);   // 一直按着 W：意图恒定，便于逐条比对
            loop.Sampler = sampler;

            SelfTest.True(transport.Connect("mem", 0), "Connect 成功", "Connect 返回 false");
            // 先喂一条权威快照：clientTick 只能来自它（不是本地帧计数、不是预测 tick）。
            Feed(loop, ref frame, 2, 23, true);
            for (var i = 0; i < 40 && transport.State != Ac.Net.ConnectionState.Connected; i++)
            {
                now += 16.6667;
                loop.Frame(16.6667);
            }
            SelfTest.Equal((long)Ac.Net.ConnectionState.Connected, (long)transport.State);

            var before = loop.CommandsSent;
            for (var i = 0; i < 60; i++)   // 1s @60fps
            {
                now += 16.6667;
                loop.Frame(16.6667);
            }
            var sent = loop.CommandsSent - before;
            SelfTest.True(sent >= 29 && sent <= 31, "1s 内上行 30±1 条命令（C05 §5.1）", sent.ToString());
            SelfTest.Equal(0, (long)loop.CommandSendFailures);

            var commands = socket.CommandPayloads();
            SelfTest.True(commands.Count >= 29, "传输里真的出现了 type=4 帧", commands.Count.ToString());
            var last = commands[commands.Count - 1];
            SelfTest.Equal(23, (long)last.ClientTick);              // 权威 tick，逐条一致
            SelfTest.Equal(InputSampler.AxisFull, (long)last.MoveX);
            SelfTest.True(last.Seq != 0, "seq 逐条推进（0 表示没走 NextSeq）", last.Seq.ToString());
            SelfTest.True(loop.LocalSteps > 0, "同一条命令也要进本地预测", loop.LocalSteps.ToString());

            // 断开后不许再往上行塞包：状态不是 Connected 时只喂预测，不发。
            var afterDisconnect = loop.CommandsSent;
            transport.Close();
            for (var i = 0; i < 60; i++) { now += 16.6667; loop.Frame(16.6667); }
            SelfTest.Equal((long)afterDisconnect, (long)loop.CommandsSent);
        }

        // 传输/会话的配置面（B1 审查点 5）：地址只从配置来，且编辑器/批处理一律不连服务器；
        // 调试面板热键（审查点 8）必须跟着 SettingsDefaults.KeyBindings 走，不许再硬编码 F3。
        private static void ChecksServerConfig()
        {
            string host;
            int port;
            SelfTest.True(GameBootstrap.TryParseServer("10.0.0.5:9999", out host, out port) && host == "10.0.0.5" && port == 9999,
                "host:port 解析", host + ":" + port);
            SelfTest.True(GameBootstrap.TryParseServer(" 127.0.0.1:8787 ", out host, out port) && host == "127.0.0.1" && port == 8787,
                "两端空白容错", host + ":" + port);
            SelfTest.True(GameBootstrap.TryParseServer("play.example.com:20000", out host, out port) && host == "play.example.com",
                "域名 + 端口", host);
            SelfTest.True(!GameBootstrap.TryParseServer("127.0.0.1", out host, out port), "缺端口判失败（不许悄悄连默认端口）", host);
            SelfTest.True(!GameBootstrap.TryParseServer("127.0.0.1:", out host, out port), "空端口判失败", host);
            SelfTest.True(!GameBootstrap.TryParseServer(":8787", out host, out port), "缺 host 判失败", host);
            SelfTest.True(!GameBootstrap.TryParseServer("127.0.0.1:0", out host, out port), "端口 0 判失败", host);
            SelfTest.True(!GameBootstrap.TryParseServer("127.0.0.1:70000", out host, out port), "端口越界判失败", host);
            SelfTest.True(!GameBootstrap.TryParseServer("127.0.0.1:udp", out host, out port), "非数字端口判失败", host);
            SelfTest.True(!GameBootstrap.TryParseServer("", out host, out port), "空串 = 不配置", host);

            SelfTest.True(!GameBootstrap.ShouldConnect("127.0.0.1", 8787, true, false), "编辑器不连服务器", "去连了");
            SelfTest.True(!GameBootstrap.ShouldConnect("127.0.0.1", 8787, false, true), "批处理不连服务器", "去连了");
            SelfTest.True(GameBootstrap.ShouldConnect("127.0.0.1", 8787, false, false), "出包且配置了地址才连", "没连");
            SelfTest.True(!GameBootstrap.ShouldConnect(null, 8787, false, false), "没地址不连", "去连了");
            SelfTest.True(!GameBootstrap.ShouldConnect("127.0.0.1", 0, false, false), "端口非法不连", "去连了");
            // 自检进程自己必须被判为"不连"：这条挂了就说明测试路径会去开真套接字
            SelfTest.True(!GameBootstrap.ShouldConnect(GameBootstrap.DefaultServerHost, GameBootstrap.DefaultServerPort,
                UnityEngine.Application.isEditor, UnityEngine.Application.isBatchMode), "自检进程不许连服务器", "会去连");
            SelfTest.True(GameBootstrap.DefaultServerPort > 0 && GameBootstrap.DefaultServerPort < 65536, "默认端口在范围内",
                GameBootstrap.DefaultServerPort.ToString());
            SelfTest.True(GameBootstrap.Loop == null || GameBootstrap.Loop.Transport == null,
                "自检进程里帧回路不许挂着传输（测试路径不连服务器）", "挂着传输");

            // 键位表 → KeyCode：默认表每条都要认得（表改了却忘了改映射，这里就红）。
            // 表就是这套键名的全集（InputSampler 的 15 个动作用的是同一套名字），没有第二个候选表。
            for (var i = 0; i < SettingsDefaults.KeyBindings.Length; i++)
            {
                SelfTest.True(GameBootstrap.KeyOf(SettingsDefaults.KeyBindings[i]) != UnityEngine.KeyCode.None,
                    "键位表第 " + i + " 条要认得", SettingsDefaults.KeyBindings[i]);
            }
            SelfTest.True(SettingsDefaults.KeyBindings[SettingsDefaults.ActionDebugPanel] == "F3", "默认表里调试面板就是 F3",
                SettingsDefaults.KeyBindings[SettingsDefaults.ActionDebugPanel]);
            var custom = (string[])SettingsDefaults.KeyBindings.Clone();
            custom[SettingsDefaults.ActionDebugPanel] = "O";          // 把面板热键改到表里另一个键
            SelfTest.True(GameBootstrap.DebugPanelKeyFor(custom) == UnityEngine.KeyCode.O,
                "调试面板热键必须跟着键位表走（不许硬编码 F3）", GameBootstrap.DebugPanelKeyFor(custom).ToString());
            SelfTest.True(GameBootstrap.DebugPanelKeyFor(SettingsDefaults.KeyBindings) == UnityEngine.KeyCode.F3,
                "默认表解析出 F3", GameBootstrap.DebugPanelKeyFor(SettingsDefaults.KeyBindings).ToString());
            var broken = (string[])SettingsDefaults.KeyBindings.Clone();
            broken[SettingsDefaults.ActionDebugPanel] = "NotAKey";
            SelfTest.True(GameBootstrap.DebugPanelKeyFor(broken) == UnityEngine.KeyCode.F3,
                "表里写了不认识的键名 → 退回默认表的同一条（而不是代码里的字面量）", GameBootstrap.DebugPanelKeyFor(broken).ToString());

            // 大厅准备键（ADR-013）走的是同一个 helper：默认表那条是 Return，改表就改键，采样器里没有第二份。
            SelfTest.True(SettingsDefaults.KeyBindings[SettingsDefaults.ActionReady] == "Return", "默认表里准备键就是 Return",
                SettingsDefaults.KeyBindings[SettingsDefaults.ActionReady]);
            var readyRemap = (string[])SettingsDefaults.KeyBindings.Clone();
            readyRemap[SettingsDefaults.ActionReady] = "Q";
            SelfTest.True(GameBootstrap.ReadyKeyFor(readyRemap) == UnityEngine.KeyCode.Q,
                "准备键必须跟着键位表走（不许硬编码 Return）", GameBootstrap.ReadyKeyFor(readyRemap).ToString());
            SelfTest.True(GameBootstrap.ReadyKeyFor(null) == UnityEngine.KeyCode.Return, "键位表缺失 → 退回默认表那一条",
                GameBootstrap.ReadyKeyFor(null).ToString());
            var readyBroken = (string[])SettingsDefaults.KeyBindings.Clone();
            readyBroken[SettingsDefaults.ActionReady] = "NotAKey";
            SelfTest.True(GameBootstrap.ReadyKeyFor(readyBroken) == UnityEngine.KeyCode.Return,
                "不认识的键名 → 退回默认表的同一条", GameBootstrap.ReadyKeyFor(readyBroken).ToString());
            var defaultSampler = new InputSampler();
            SelfTest.True(defaultSampler.ConfirmKey == GameBootstrap.KeyOf(SettingsDefaults.KeyBindings[SettingsDefaults.ActionReady]),
                "采样器默认准备键 = 默认表那一条", defaultSampler.ConfirmKey.ToString());
            // 灵敏度（C13 的第三项设置）：采样器自带钳制，面板给的值走的就是这条路。
            defaultSampler.SetSensitivity(99.0);
            SelfTest.True(Math.Abs(defaultSampler.SensitivityValue - InputSampler.MaxSensitivity) < 1e-9,
                "灵敏度超上限要钳到 MaxSensitivity", defaultSampler.SensitivityValue.ToString("R"));
            defaultSampler.SetSensitivity(0.0);
            SelfTest.True(Math.Abs(defaultSampler.SensitivityValue - InputSampler.MinSensitivity) < 1e-9,
                "灵敏度超下限要钳到 MinSensitivity", defaultSampler.SensitivityValue.ToString("R"));
            // 默认昵称（GameBootstrap.DefaultLocalName）：本地身份按昵称认领 MatchState 的行，所以它必须是**合法**昵称
            //（非空、≤12 字节、过 SanitizeName 不变形）—— 否则空名字会让 LocalPlayerId 恒 0，相机与键鼠一起失效。
            var defaultName = GameBootstrap.DefaultLocalName;
            SelfTest.True(Ac.UI.Lobby.IsValidName(defaultName), "默认昵称必须合法（非空、1..12 字节）", defaultName);
            SelfTest.True(Ac.UI.Lobby.SanitizeName(defaultName) == defaultName, "默认昵称不许被清洗改形", Ac.UI.Lobby.SanitizeName(defaultName));
        }

        private static GameLoop NewLoop(int entities, out SnapshotFrame frame)
        {
            var loop = new GameLoop(new SnapshotView(), new EntityViews(), new Hud(), new FrameProfiler());
            loop.LocalPlayerId = 1;
            frame = default(SnapshotFrame);
            frame.Entities = new FrameEntity[SnapshotView.MaxRecordsPerFrame];
            // RemovedIds 也必须非空：镜像对 null 一律按坏帧拒收（SnapshotView.cs:92）。
            frame.RemovedIds = new ushort[SnapshotView.MaxRemovedPerFrame];
            return loop;
        }

        private static void Feed(in GameLoop loop, ref SnapshotFrame frame, int entities, uint tick, bool full)
        {
            frame.Tick = tick;
            frame.ServerTimeMs = tick * 16u;
            frame.LastAckedSeq = (ushort)tick;
            frame.BaselineTick = full ? 0u : tick - 1u;
            frame.EntityCount = entities;
            frame.RemovedCount = 0;
            for (var i = 0; i < entities; i++)
            {
                var e = default(FrameEntity);
                e.Id = (ushort)(i + 1);
                e.XCm = (short)(100 * i + (int)tick % 7);
                e.ZCm = (short)(-100 * i);
                e.YawUnits = (ushort)(tick * 13u & 0xFFFFu);
                e.PitchUnits = 32768;
                e.HpRatioUnits = (byte)(255 - i);
                e.KindFlags = (byte)(i == 0 ? 0 : 1);
                frame.Entities[i] = e;
            }
            SelfTest.True(loop.ApplySnapshot(frame), "快照应当被镜像接受", "被丢弃");
        }

        private static void ChecksFrameLoop()
        {
            SnapshotFrame frame;
            var loop = NewLoop(8, out frame);
            for (uint tick = 1; tick <= 12; tick++)
            {
                Feed(loop, ref frame, 8, tick, tick % 4 == 0);
                loop.Frame(1000.0 / 60.0);
            }
            SelfTest.Equal(12, (long)loop.Frames);
            SelfTest.Equal(12, (long)loop.SnapshotsApplied);
            SelfTest.Equal(0, (long)loop.DecodeFailures);

            EntityView local;
            SelfTest.True(loop.Views.TryGet(1, out local), "本地实体必须进了视图", "缺失");
            // 采样来自镜像：本地玩家 hp 255、不是倒地
            SelfTest.Equal(255, (long)loop.Sample.HpRatio255);
            SelfTest.True(!loop.Sample.Downed, "未倒地不许被标成倒地", "标成倒地了");
            SelfTest.Equal(8, (long)loop.Views.ActiveCount);
            SelfTest.Equal(0, (long)loop.EventsApplied);
            SelfTest.Equal(0, (long)loop.HardCorrects);   // 没有权威帧时不产生硬纠正
        }

        // 呈现阶段的缝：接了 sink 就必须每帧被驱动；没接就必须**一帧都不打点**
        // （打了点、值恒 0，等于让 fx/audio 的预算永远通过 —— 审查点名的假绿）。
        private sealed class CountingSink : IFrameStageSink
        {
            internal int Ticks;
            public void Tick(double dtMs) { Ticks += 1; }
        }

        private static void ChecksStageSinks()
        {
            SnapshotFrame frame;
            var loop = NewLoop(4, out frame);

            for (uint tick = 1; tick <= 5; tick++) { Feed(loop, ref frame, 4, tick, tick == 1); loop.Frame(1000.0 / 60.0); }
            SelfTest.True(loop.Profiler.P95Ms(FrameStage.Fx) == 0f, "未接 sink 时 fx 段不许打点", loop.Profiler.P95Ms(FrameStage.Fx).ToString("R"));

            var audio = new CountingSink();
            loop.Audio = audio;
            for (uint tick = 6; tick <= 10; tick++) { Feed(loop, ref frame, 4, tick, false); loop.Frame(1000.0 / 60.0); }
            SelfTest.Equal(5, (long)audio.Ticks);
            SelfTest.True(loop.Profiler.P95Ms(FrameStage.Audio) > 0f, "接了 sink 就该打点", loop.Profiler.P95Ms(FrameStage.Audio).ToString("R"));

            loop.Audio = null;
            for (uint tick = 11; tick <= 12; tick++) { Feed(loop, ref frame, 4, tick, false); loop.Frame(1000.0 / 60.0); }
            SelfTest.Equal(5, (long)audio.Ticks);
        }

        private static void ChecksSteadyStateZeroAlloc()
        {
            SnapshotFrame frame;
            var loop = NewLoop(64, out frame);
            for (uint tick = 1; tick <= 60; tick++)
            {
                Feed(loop, ref frame, 64, tick, tick % 40 == 0);
                loop.Frame(1000.0 / 60.0);
            }
            // 读数来自 AllocMeter（引擎的 GC Allocated In Frame 计数器，逐字节精确）。
            // 注意：GC.GetAllocatedBytesForCurrentThread() 在本机恒为 0，看不见任何分配——别再用它。
            var before = AllocMeter.Begin();
            for (uint tick = 61; tick <= 180; tick++)
            {
                Feed(loop, ref frame, 64, tick, tick % 40 == 0);
                loop.Frame(1000.0 / 60.0);
            }
            // C14 §5：稳态每帧 0 B 分配。这条闸挂了就说明帧回路里有隐藏分配。
            AllocMeter.AssertZero(before);
        }

        // ADR-009「握手时序」的 type 11 Join 接线：帧回路必须在**连上之后**把昵称报上去，
        // 昵称再变时补报一次，且同一会话同一昵称只发一次（稳态不许有包）。
        // 这条用例钉的是 GameLoop.FlushJoin 的两个时机，不是编解码本身（那是 net.join_wire）。
        private static void ChecksJoinReport()
        {
            var now = 0.0;
            var socket = new ScriptedSocket();
            var transport = new Ac.Net.UdpTransport(socket, delegate { return now; });
            SnapshotFrame frame;
            var loop = NewLoop(1, out frame);
            loop.Transport = transport;

            // 顺序 A：昵称先敲好、之后才连上（"进大厅就打字、连接晚一拍"）。
            loop.LocalName = "alpha";
            SelfTest.True(transport.Connect("mem", 0), "Connect 成功", "Connect 返回 false");
            for (var i = 0; i < 40 && transport.State != Ac.Net.ConnectionState.Connected; i++)
            {
                now += 50.0;
                loop.Frame(50.0);
            }
            SelfTest.Equal((long)Ac.Net.ConnectionState.Connected, (long)transport.State);
            var joins = socket.JoinNames();
            SelfTest.Equal(1, joins.Count);
            SelfTest.Equal("alpha", joins[0]);

            // 稳态：昵称没变，跑 20 帧不许再出现任何 Join。
            for (var i = 0; i < 20; i++) { now += 50.0; loop.Frame(50.0); }
            SelfTest.Equal(1, socket.JoinNames().Count);

            // 顺序 B：昵称在大厅里被改了 ⇒ 必须补报一次（否则服务端一直叫旧名字）。
            loop.LocalName = "beta";
            loop.Frame(50.0);
            joins = socket.JoinNames();
            SelfTest.Equal(2, joins.Count);
            SelfTest.Equal("beta", joins[1]);

            // 空昵称不上线：清洗器可能把整串都过滤掉，此时必须**不发**而不是发空名。
            loop.LocalName = string.Empty;
            loop.Frame(50.0);
            SelfTest.Equal(2, socket.JoinNames().Count);
        }

        // 脚本化的最小服务端桩件：把 UdpTransport 推进到 Connected，并对可靠消息回 ack。
        // ack 不是可选的装饰：不确认的话客户端会按 RTO 重传，Join 于是出现在日志里好几次——
        // 那是"重传"，不是"重复上报"，不 ack 就测不出这两者的区别。
        private sealed class ScriptedSocket : Ac.Net.IDatagramSocket
        {
            private readonly System.Collections.Generic.Queue<byte[]> _inbound =
                new System.Collections.Generic.Queue<byte[]>();
            private readonly System.Collections.Generic.List<byte[]> _sent =
                new System.Collections.Generic.List<byte[]>();
            private readonly Ac.Net.ReliabilityChannel _received = new Ac.Net.ReliabilityChannel();
            private uint _nextMsgId = 1;
            private bool _bound;

            public bool IsBound { get { return _bound; } }
            public int Port { get { return 40404; } }
            public bool HasDatagram { get { return _inbound.Count > 0; } }
            public bool Bind(int port) { _bound = true; return true; }
            public void Connect(string host, int port) { _bound = true; }

            public int Receive(byte[] buffer)
            {
                if (_inbound.Count == 0) return 0;
                var next = _inbound.Dequeue();
                Array.Copy(next, buffer, next.Length);
                return next.Length;
            }

            public bool Send(byte[] datagram, int length)
            {
                var copy = new byte[length];
                Array.Copy(datagram, copy, length);
                _sent.Add(copy);
                var reader = new Ac.Net.PacketReader(copy);
                Ac.Net.PacketHeader header;
                if (Ac.Net.PacketHeader.Read(reader, out header) != Ac.Net.DecodeFailure.Ok) return true;
                if (header.IsReliable) _received.NoteReceived(header.MsgId);
                if (header.Type == Ac.Net.PacketType.Hello)
                {
                    var payload = Ac.Net.HandshakeCodec.EncodeHelloAck(1234u, 0xCAFEBABEu);
                    _inbound.Enqueue(Control(Ac.Net.PacketType.HelloAck, payload));
                    return true;
                }
                // 可靠消息一律确认（KeepAlive 恒为 reliable|ackOnly，进不了重传表）。
                if (header.IsReliable) _inbound.Enqueue(Control(Ac.Net.PacketType.KeepAlive, null));
                return true;
            }

            private byte[] Control(Ac.Net.PacketType type, byte[] payload)
            {
                var header = new Ac.Net.PacketHeader();
                header.Version = Ac.Net.PacketHeader.ProtocolVersion;
                header.Type = type;
                header.Flags = (Ac.Net.PacketFlags)Ac.Net.PacketHeader.RequiredFlags(type);
                header.Session = 7;
                header.Seq = 1;
                header.MsgId = _nextMsgId++;
                header.AckBase = _received.AckBase;
                header.AckBits = _received.AckBits;
                if (payload == null) return Ac.Net.PacketWriter.Build(header);
                return Ac.Net.PacketWriter.Build(header, payload, 0, payload.Length);
            }

            public void Close() { _bound = false; }

            // 客户端发出的 type=4 载荷按序解出来（§5.2 固定 14 字节）。
            internal System.Collections.Generic.List<Ac.Net.CommandPayload> CommandPayloads()
            {
                var list = new System.Collections.Generic.List<Ac.Net.CommandPayload>();
                for (var i = 0; i < _sent.Count; i++)
                {
                    var reader = new Ac.Net.PacketReader(_sent[i]);
                    Ac.Net.PacketHeader header;
                    if (Ac.Net.PacketHeader.Read(reader, out header) != Ac.Net.DecodeFailure.Ok) continue;
                    if (header.Type != Ac.Net.PacketType.Command) continue;
                    byte[] body;
                    if (!reader.TryReadBytes(reader.Remaining, out body)) continue;
                    Ac.Net.CommandPayload payload;
                    if (Ac.Net.CommandCodec.Decode(body, out payload) != Ac.Net.DecodeFailure.Ok) continue;
                    list.Add(payload);
                }
                return list;
            }

            // 客户端发出的 type=11 载荷按序解出来（nameLen u8 + UTF-8 字节）。
            internal System.Collections.Generic.List<string> JoinNames()
            {
                var names = new System.Collections.Generic.List<string>();
                var utf8 = new System.Text.UTF8Encoding(false, true);
                for (var i = 0; i < _sent.Count; i++)
                {
                    var reader = new Ac.Net.PacketReader(_sent[i]);
                    Ac.Net.PacketHeader header;
                    if (Ac.Net.PacketHeader.Read(reader, out header) != Ac.Net.DecodeFailure.Ok) continue;
                    if (header.Type != Ac.Net.PacketType.Join) continue;
                    byte[] body;
                    if (!reader.TryReadBytes(reader.Remaining, out body)) continue;
                    if (body.Length < 2) continue;
                    names.Add(utf8.GetString(body, 1, body[0]));
                }
                return names;
            }
        }
    }
}
