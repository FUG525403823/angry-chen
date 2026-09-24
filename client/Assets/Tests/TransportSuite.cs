using System;
using System.Collections.Generic;
using System.Net;
using System.Net.Sockets;
using System.Threading;
using Ac.Core;
using Ac.Net;

namespace Ac.Tests
{
    // C03 §6/§7 的回环用例：真套接字握手、丢包重传、1200B 分片重组、p99 统计。
    internal static class TransportSuite
    {
        public static void Register()
        {
            SelfTest.Add("net.handshake", ChecksHandshake);
            SelfTest.Add("net.retransmit_rto", ChecksRetransmitRto);
            SelfTest.Add("net.fragment_1200", ChecksFragment1200);
            SelfTest.Add("net.stats_p99", ChecksStatsP99);
            SelfTest.Add("net.backlog_cap", ChecksBacklogCap);
        }

        // ---- 1. 握手与状态机（真套接字回环）----------------------------------------------------------

        // 服务端桩件直接用生产侧的 UdpTransport.RealUdpSocket（不 connect，靠 SendTo/ReceiveFrom 回环对话）：
        // 测试不再自带一份套接字实现，避免桩件与生产路径各走一套语义。

        // 审计 M8：积压超上限且无快照可丢时，Send 恒 true、积压无限增长（与自己 :470 的注释相反）。
        private static void ChecksBacklogCap()
        {
            var link = new MemoryLink();
            var clock = new Clock();
            var client = new UdpTransport(link.Create(1), clock.Now);
            var peer = new MiniPeer(link.Create(2));
            peer.Attach(link);
            peer.Start();
            link.Pump();
            client.Connect("mem", 0);

            // 先无丢包跑通握手；否则 Send 会因为"未连接"返回 false，测出来的拒收是假的。
            for (var i = 0; i < 40 && client.State != ConnectionState.Connected; i++)
            {
                clock.Advance(50.0);
                link.NowMs = clock.NowMs;
                client.Poll(UdpTransport.MaxInboundPacketsPerPoll);
                peer.Pump(link.NowMs);
                link.Pump();
            }
            SelfTest.True(client.State == ConnectionState.Connected, "握手必须先成功", client.State.ToString());

            // 之后发送永远失败：积压只增不减。**不推进时钟**，避免把状态推成 Reconnecting
            // 而让"拒收"来自别的原因。纯命令载荷：没有任何可丢的快照。
            link.FailSends = true;
            var payload = new byte[1000];
            var accepted = 0;
            var rejected = 0;
            for (var i = 0; i < 200; i++)
            {
                if (client.Send(PacketType.Command, payload)) accepted += 1;
                else rejected += 1;
            }
            SelfTest.True(accepted > 0, "封顶之前必须受理", accepted.ToString());
            // 旧实现这里 rejected == 0（恒 true），积压会一路涨到 200KB 以上。
            SelfTest.True(rejected > 0, "无快照可丢时必须拒收", rejected.ToString());
            SelfTest.True(client.State == ConnectionState.Connected, "拒收必须来自封顶而不是断开", client.State.ToString());
        }

        private static void ChecksHandshake()
        {
            var clock = new Clock();
            var server = new MiniPeer(new UdpTransport.RealUdpSocket());
            var socket = new UdpTransport.RealUdpSocket();
            var client = new UdpTransport(socket, clock.Now);
            var states = new List<ConnectionState>();
            client.StateChanged += delegate(ConnectionState from, ConnectionState to) { states.Add(to); };

            server.DropFirstHello = true;   // 首个 Hello 丢掉：证明 1s × 5 次重发
            server.Start();
            SelfTest.True(client.Connect("127.0.0.1", server.Port), "Connect 成功", "Connect 返回 false");

            Drive(client, server, clock, 200, 20.0);
            SelfTest.Equal((long)ConnectionState.Connected, (long)client.State);
            SelfTest.True(states.Contains(ConnectionState.Connecting), "经过 Connecting", "未经过");
            SelfTest.Equal(7, client.Session);
            SelfTest.Equal(2, server.HelloCount);            // 首次被丢，第二次后才握上
            SelfTest.Equal(1234, (long)server.ServerTick);

            // 令牌 = salt ^ clientNonce，且只在本地保存（§5.5）。
            var expectedToken = 0xCAFEBABEu ^ server.LastNonce;
            uint stored;
            SelfTest.True(client.Machine.Tokens.TryTake(client.Endpoint, out stored), "令牌已保存", "无令牌");
            SelfTest.Equal((long)expectedToken, (long)stored);

            // §5.2 的载荷长度常量与 S04 的 codec.hpp 同值（握手线格式的自检）。
            SelfTest.Equal(12, HandshakeCodec.HelloPayloadBytes);
            SelfTest.Equal(8, HandshakeCodec.HelloAckPayloadBytes);
            SelfTest.Equal(8, HandshakeCodec.ResumePayloadBytes);
            SelfTest.Equal(1, HandshakeCodec.DisconnectPayloadBytes);

            // 心跳往返给出 rtt 采样（KeepAlive -> 对端 ack 位图）。
            Drive(client, server, clock, 100, 20.0);
            SelfTest.True(client.Stats.Snapshot().RttMs > 0.0, "rtt 被采样", "rtt 仍为 0");

            // 3s 无任何包 -> Zombie；宽限期内继续尝试 Resume。
            server.Silent = true;
            Drive(client, server, clock, 200, 20.0);
            SelfTest.True(states.Contains(ConnectionState.Zombie), "进入 Zombie", "未进入");

            // Zombie 的离开路径之一：对端又活过来（本会话的包）即回 Connected。
            server.Silent = false;
            server.SendMessage(PacketType.Snapshot, new byte[4]);
            Drive(client, server, clock, 5, 20.0);
            SelfTest.Equal((long)ConnectionState.Connected, (long)client.State);
            SelfTest.True(states.Contains(ConnectionState.Connected), "Zombie 离开路径有覆盖", "未覆盖");

            // 再静默 3s：宽限期内发起 Resume（宽限期从这里重新计时）。
            server.Silent = true;
            Drive(client, server, clock, 200, 20.0);
            SelfTest.True(states.Contains(ConnectionState.Reconnecting), "宽限期内发起 Resume", "未发起");
            SelfTest.True(server.ResumeCount >= 1, "Resume 已发出", "未发出");

            // 30s 宽限期到期 -> Disconnected，令牌作废。
            Drive(client, server, clock, 500, 100.0);
            SelfTest.Equal((long)ConnectionState.Disconnected, (long)client.State);
            SelfTest.True(!client.Machine.Tokens.HasValid(client.Endpoint), "宽限期后令牌作废", "令牌仍在");

            // 重新握手，并验证「长会话（> 30s）断线后仍能 Resume」：令牌寿命不是从 HelloAck 起算的 30s。
            clock.Advance(1000.0);
            server.Silent = false;
            client.Connect("127.0.0.1", server.Port);
            Drive(client, server, clock, 100, 20.0);
            SelfTest.Equal((long)ConnectionState.Connected, (long)client.State);
            Drive(client, server, clock, 400, 100.0);   // 会话连续存活 40s
            server.Silent = true;
            Drive(client, server, clock, 200, 20.0);
            SelfTest.True(states.Contains(ConnectionState.Reconnecting), "长会话断线仍能 Resume", "直接放弃");

            // Resume 成功：S04 §5.5 的服务端只补全量快照、不回 HelloAck，所以「本会话的数据包」也要能确认重连成功。
            server.Silent = false;
            server.SendMessage(PacketType.Snapshot, new byte[4]);
            Drive(client, server, clock, 20, 20.0);
            SelfTest.Equal((long)ConnectionState.Connected, (long)client.State);

            // Resume 被拒（Disconnect reason=2）同样回到 Disconnected。
            server.RejectResume = true;
            clock.Advance(4000.0);
            Drive(client, server, clock, 100, 20.0);
            SelfTest.Equal((long)ConnectionState.Disconnected, (long)client.State);
            SelfTest.True(client.Machine.DisconnectedByServer, "记录了服务端断开", "未记录");

            client.Close();
            server.Stop();
        }

        // ---- 2. 20% 丢包下的重传与去重（内存链路 + 虚拟时钟）-----------------------------------------

        private static void ChecksRetransmitRto()
        {
            // 位图跳变：k=32 清零后置 bit31；k=33 只清零、不置位（C# 的 uint 移位按 &31 取模，
            // 照抄 S04 伪码会让 k=33 落到 bit0，凭空捏造「已收到」）。
            var bitmap = new ReliabilityChannel();
            bitmap.NoteReceived(1u);
            bitmap.NoteReceived(34u);
            SelfTest.Equal(34, (long)bitmap.AckBase);
            SelfTest.Equal(0, (long)bitmap.AckBits);
            bitmap.NoteReceived(66u);
            SelfTest.Equal((long)(1u << 31), (long)bitmap.AckBits);

            // RTO 表逐字冻结（200 * 1.5^(n-1) 取整，禁 pow）。
            SelfTest.Equal(200, ReliabilityChannel.RtoTableMs[0]);
            SelfTest.Equal(300, ReliabilityChannel.RtoTableMs[1]);
            SelfTest.Equal(450, ReliabilityChannel.RtoTableMs[2]);
            SelfTest.Equal(675, ReliabilityChannel.RtoTableMs[3]);
            SelfTest.Equal(1000, ReliabilityChannel.RtoTableMs[4]);
            SelfTest.Equal(5, ReliabilityChannel.MaxRetransmits);

            // 单条消息的退避节奏：199ms 不动，200/500/950/1625/2625ms 各重传一次，之后判失联。
            var probe = new ReliabilityChannel();
            var sentAt = new List<double>();
            var msgId = probe.NextMsgId();
            probe.Track(msgId, new byte[] { 1, 2, 3 }, 0.0);
            double[] at = { 199.0, 200.0, 500.0, 950.0, 1625.0, 2625.0, 4000.0 };
            foreach (var t in at)
            {
                bool exhausted;
                var resent = probe.Tick(t, delegate(uint id, byte[] bytes) { sentAt.Add(t); }, out exhausted);
                if (t == 4000.0) SelfTest.True(exhausted, "重传超限已判失联", "未判失联");
            }
            SelfTest.Equal(5, sentAt.Count);
            SelfTest.BitEqual(200.0, sentAt[0]);
            SelfTest.BitEqual(2625.0, sentAt[4]);

            var link = new MemoryLink();
            link.LossRate = 0.2;
            link.LatencyMs = 50.0;
            link.JitterMs = 10.0;
            var clock = new Clock();
            var client = new UdpTransport(link.Create(1), clock.Now);
            var peer = new MiniPeer(link.Create(2));
            peer.Attach(link);
            peer.Start();
            link.Pump();
            client.Connect("mem", 0);

            // 命令上行 5 Hz。ack 位图只有 32 位，窗口时长 = 32 / 每秒 msgId 数（命令 + 心跳）必须 ≥ RTO 表
            // 总时长 2.6s，否则超出窗口的那次重传永远无法被确认、消息会被判失联。5Hz + 2Hz 心跳 = 6.4s，有余量；
            // 30Hz 命令流下窗口只有约 1s——这是冻结规范（S04 §5.3 + §5.5）的固有张力，已记入交叉链问题。
            var step = 1000.0 / 30.0;
            var steps = 1800;               // 60 秒虚拟时间
            var commands = 0;
            var eventsExpected = 0;
            var reconnects = 0;
            client.StateChanged += delegate(ConnectionState from, ConnectionState to)
            {
                if (to == ConnectionState.Reconnecting) reconnects += 1;
            };
            var eventsSeen = new List<uint>();
            client.ApplicationPacket += delegate(PacketHeader header, byte[] payload)
            {
                if (header.Type != PacketType.Event) return;
                eventsSeen.Add(header.MsgId);
            };

            for (var i = 0; i < steps; i++)
            {
                clock.Advance(step);
                link.NowMs = clock.NowMs;
                client.Poll(UdpTransport.MaxInboundPacketsPerPoll);
                if (i % 6 == 0 && client.State == ConnectionState.Connected)
                {
                    var command = new byte[14];
                    command[0] = (byte)(i & 0xFF);
                    if (client.Send(PacketType.Command, command)) commands += 1;
                }
                if (i % 3 == 0 && i < steps - 120)   // 10 Hz 事件；留出 4 秒让尾包的重传跑完
                {
                    eventsExpected += 1;
                    peer.SendEvent(new byte[] { (byte)i, (byte)(i >> 8) }, i % 2 == 0);
                }
                link.Pump();
                peer.Pump(link.NowMs);
                link.Pump();
            }

            SelfTest.Equal((long)ConnectionState.Connected, (long)client.State);
            // 32 位 ack 窗口在 30 Hz 命令流下只能覆盖约 1 秒，而 RTO 表最长 2.6 秒：超出窗口的重传
            // 永远无法被确认，于是偶发「消息判失联 -> 重连」。这是冻结规范（S04 §5.3 + §5.5）的固有张力，
            // 已记入交叉链问题；这里只断言它是有限次、且不影响零丢失。
            SelfTest.True(commands > 275, "命令全部受理", "受理 " + commands);
            SelfTest.Equal(0, reconnects);   // 窗口覆盖整个 RTO 区间时，重传都能被确认，不该出现失联判定
            SelfTest.Equal(commands, peer.CommandsSeen.Count);   // 零丢失：每条命令恰好到一次
            SelfTest.Equal(eventsExpected, eventsSeen.Count);   // 事件恰好投递一次：重传副本被去重吃掉
            SelfTest.True(link.Dropped > 0, "注入的丢包确实发生", "零丢包");

            var stats = client.Stats.Snapshot();
            var lossRatio = (double)link.Dropped / link.Sent;
            SelfTest.True(lossRatio > 0.15 && lossRatio < 0.25, "丢包比例 接近 20%", lossRatio.ToString("F3"));
            var retransmitRatio = (double)stats.Retransmits / commands;
            SelfTest.True(retransmitRatio > 0.05 && retransmitRatio < 0.6, "重传比例与丢包同量级", retransmitRatio.ToString("F3"));
            SelfTest.True(stats.Duplicates > 0, "重传副本被计为重传", "未计重传");
            SelfTest.True(client.OutstandingMessages <= 8, "重传表没有堆积", client.OutstandingMessages.ToString());
        }

        // ---- 3. 1200B 分片与重组 ---------------------------------------------------------------------

        private static void ChecksFragment1200()
        {
            SelfTest.Equal(1176, Fragmenter.MaxFragmentPayload);   // 1200 - 8 - 12 - 4
            SelfTest.Equal(9408, Fragmenter.MaxLogicalMessageBytes);
            SelfTest.Equal(1, Fragmenter.FragmentCount(1176));
            SelfTest.Equal(2, Fragmenter.FragmentCount(1177));
            SelfTest.Equal(2, Fragmenter.FragmentCount(1201));
            SelfTest.Equal(8, Fragmenter.FragmentCount(9408));
            SelfTest.Equal(9, Fragmenter.FragmentCount(9409));     // 片数超限：Fragmenter.Split 会拒绝

            var payload = new byte[9408];
            for (var i = 0; i < payload.Length; i++) payload[i] = (byte)(i * 31 + 7);
            var template = MakeHeader(PacketType.Event, _session: 3);
            var datagrams = new List<byte[]>();
            SelfTest.Equal(8, Fragmenter.Split(template, 5, payload, datagrams));
            foreach (var datagram in datagrams)
            {
                SelfTest.True(datagram.Length <= 1200, "单片不超过 1200 字节", datagram.Length.ToString());
            }
            SelfTest.Equal(0, Fragmenter.Split(template, 5, new byte[9409], new List<byte[]>()));

            // 入站重组：乱序 + 重复片 + 超时。用正好 4 片的消息，验证「还缺片」与「收齐」两条路径。
            var quarter = new byte[Fragmenter.MaxFragmentPayload * 4];
            for (var i = 0; i < quarter.Length; i++) quarter[i] = (byte)(i * 13 + 5);
            var quadDatagrams = new List<byte[]>();
            SelfTest.Equal(4, Fragmenter.Split(template, 6, quarter, quadDatagrams));

            var reassembler = new FragmentReassembler();
            var order = new[] { 3, 1, 0, 2 };
            byte[] message = null;
            for (var i = 0; i < order.Length; i++)
            {
                PacketHeader header;
                var body = ReadFragment(quadDatagrams[order[i]], out header);
                byte[] partial;
                var result = reassembler.Accept(header, body, 0, body.Length, 0.0, out partial);
                if (i < order.Length - 1)
                {
                    SelfTest.Equal((long)DecodeFailure.Truncated, (long)result);
                }
                else
                {
                    SelfTest.Equal((long)DecodeFailure.Ok, (long)result);
                    message = partial;
                }
            }
            SelfTest.Equal(quarter.Length, message.Length);
            SelfTest.Equal(quarter[0], message[0]);
            SelfTest.Equal(quarter[quarter.Length - 1], message[quarter.Length - 1]);
            SelfTest.Equal(0, reassembler.ActiveGroups);

            // 重复片幂等 + 60 tick（3s）超时整组丢弃。
            PacketHeader first;
            var slice = ReadFragment(quadDatagrams[0], out first);
            byte[] ignored;
            SelfTest.Equal((long)DecodeFailure.Truncated, (long)reassembler.Accept(first, slice, 0, slice.Length, 0.0, out ignored));
            SelfTest.Equal((long)DecodeFailure.Truncated, (long)reassembler.Accept(first, slice, 0, slice.Length, 100.0, out ignored));
            SelfTest.Equal(1, reassembler.ActiveGroups);
            SelfTest.Equal(0, reassembler.Expire(2999.0));
            SelfTest.Equal(1, reassembler.Expire(3100.0));

            // 片头非法：片数超上限、fragIndex >= fragCount。
            var bad = first;
            bad.FragCount = 9;
            SelfTest.Equal((long)DecodeFailure.BadValue, (long)reassembler.Accept(bad, slice, 0, slice.Length, 0.0, out ignored));
            bad = first;
            bad.FragIndex = bad.FragCount;   // fragIndex >= fragCount
            SelfTest.Equal((long)DecodeFailure.BadValue, (long)reassembler.Accept(bad, slice, 0, slice.Length, 0.0, out ignored));

            // 端到端：真实分片经过内存链路，对端重组后字节相同；反向也走一遍，统计加一。
            var link = new MemoryLink();
            var clock = new Clock();
            var client = new UdpTransport(link.Create(1), clock.Now);
            var peer = new MiniPeer(link.Create(2));
            peer.Attach(link);
            peer.Start();
            link.Pump();
            client.Connect("mem", 0);
            DriveMemory(client, peer, link, clock, 120, 20.0);
            SelfTest.Equal((long)ConnectionState.Connected, (long)client.State);

            // 线上只看得到 type=9 与 reliable 位，逻辑类型无法回带（§5.4 的缺口）：命令走可靠位，落成事件通道。
            var big = new byte[4000];
            for (var i = 0; i < big.Length; i++) big[i] = (byte)(i * 7 + 3);
            SelfTest.True(client.Send(PacketType.Command, big), "4000 字节命令被受理", "被拒");
            DriveMemory(client, peer, link, clock, 60, 20.0);
            SelfTest.Equal(1, peer.Reassembled.Count);
            SelfTest.Equal(big.Length, peer.Reassembled[0].Length);
            var identical = true;
            for (var i = 0; i < big.Length; i++)
            {
                if (big[i] != peer.Reassembled[0][i]) { identical = false; break; }
            }
            SelfTest.True(identical, "重组后逐字节相同", "字节不一致");

            // 分片组超时以「首片」起算（S04 的 Reassembler::add 记 firstTick）：迟到的片不得把截止时刻往后推，
            // 否则对端只要每 3s 送一片就能让组永生，§8「重组内存吃满」的对策落空。
            var anchorGroup = new FragmentReassembler();
            var anchorSlices = new List<byte[]>();
            var anchorTemplate = new PacketHeader();
            anchorTemplate.Version = PacketHeader.ProtocolVersion;
            anchorTemplate.Type = PacketType.Command;
            anchorTemplate.Flags = PacketFlags.Reliable;
            anchorTemplate.Session = 7;
            anchorTemplate.Seq = 1;
            anchorTemplate.MsgId = 1;
            SelfTest.Equal(3, Fragmenter.Split(anchorTemplate, 3, new byte[Fragmenter.MaxFragmentPayload * 3], anchorSlices));
            PacketHeader anchorHeader;
            byte[] anchorMessage;
            var anchorBody = ReadFragment(anchorSlices[0], out anchorHeader);
            SelfTest.Equal((long)DecodeFailure.Truncated, (long)anchorGroup.Accept(anchorHeader, anchorBody, 0, anchorBody.Length, 1000.0, out anchorMessage));
            anchorBody = ReadFragment(anchorSlices[1], out anchorHeader);
            SelfTest.Equal((long)DecodeFailure.Truncated, (long)anchorGroup.Accept(anchorHeader, anchorBody, 0, anchorBody.Length, 3900.0, out anchorMessage));
            anchorGroup.Expire(3901.0);
            SelfTest.Equal(1, anchorGroup.ActiveGroups);   // 3s 内不得提前丢弃
            anchorGroup.Expire(4001.0);
            SelfTest.Equal(0, anchorGroup.ActiveGroups);   // 3s 到点即整组丢弃（截止时刻锚在首片）
            SelfTest.Equal(4, peer.FragmentDatagrams);   // 4000 字节 = 4 片，每片均不超过 1200

            var inbound = new byte[3000];
            for (var i = 0; i < inbound.Length; i++) inbound[i] = (byte)(i ^ 0x5A);
            var received = new List<byte[]>();
            client.ApplicationPacket += delegate(PacketHeader header, byte[] bytes) { received.Add(bytes); };
            peer.SendMessage(PacketType.Event, inbound);
            DriveMemory(client, peer, link, clock, 60, 20.0);
            SelfTest.Equal(1, received.Count);
            SelfTest.Equal(inbound.Length, received[0].Length);
            SelfTest.Equal(1, client.Stats.Snapshot().FragmentsReassembled);
        }

        // ---- 4. NetStats 字段与 p99 ------------------------------------------------------------------

        private static void ChecksStatsP99()
        {
            var stats = new NetStats();
            stats.Reset(0.0);

            // 200 个采样：190 个 50ms + 10 个 200ms -> 索引 ceil(0.99*200)-1 = 197 -> 200ms。
            for (var i = 0; i < 190; i++) stats.OnRttSample(50.0);
            for (var i = 0; i < 10; i++) stats.OnRttSample(200.0);
            SelfTest.BitEqual(200.0, stats.RttP99Ms());
            SelfTest.Equal(200, stats.RttSampleCount);

            // 环满后只保留最近 512 个：先灌 600 个 20ms，p99 落到 20ms。
            for (var i = 0; i < 600; i++) stats.OnRttSample(20.0);
            SelfTest.Equal(512, stats.RttSampleCount);
            SelfTest.BitEqual(20.0, stats.RttP99Ms());

            // 固定分布下的解析值：1% 分位落在 200ms 那一档，误差不超过 1ms。
            stats.Reset(0.0);
            for (var i = 0; i < 200; i++) stats.OnRttSample(i < 196 ? 50.0 : 200.0);
            SelfTest.True(Math.Abs(stats.RttP99Ms() - 200.0) <= 1.0, "p99 与解析值差不超过 1ms", stats.RttP99Ms().ToString("F2"));

            // 丢包率：序号 1,2,3,6 -> 期望 5 个、实收 4 个 -> 200‰。
            stats.Reset(0.0);
            ushort[] seqs = { 1, 2, 3, 6 };
            foreach (var seq in seqs) stats.OnInbound(PacketType.Event, seq, 100);
            stats.Tick(1000.0);
            SelfTest.Equal(200, stats.PacketLossPermille);
            SelfTest.BitEqual(400.0, stats.BytesInPerSec);    // 400 字节 / 1s

            // 字节速率与单调计数。
            stats.Reset(0.0);
            stats.OnOutbound(1000);
            stats.OnInbound(PacketType.KeepAlive, 1, 0);
            stats.OnRetransmit();
            stats.OnRetransmit();
            stats.OnDuplicate();
            stats.OnDroppedSnapshot();
            stats.OnFragmentReassembled();
            stats.OnInvalidPacket();
            stats.SetRtoMs(450.0);
            stats.SetState(ConnectionState.Connected);
            stats.Tick(1000.0);
            var snapshot = stats.Snapshot();
            SelfTest.BitEqual(1000.0, snapshot.BytesOutPerSec);
            SelfTest.Equal(2, snapshot.Retransmits);
            SelfTest.Equal(1, snapshot.Duplicates);
            SelfTest.Equal(1, snapshot.DroppedSnapshots);
            SelfTest.Equal(1, snapshot.FragmentsReassembled);
            SelfTest.Equal(1, snapshot.InvalidPackets);
            SelfTest.BitEqual(450.0, snapshot.RtoMs);
            SelfTest.Equal((long)ConnectionState.Connected, (long)snapshot.State);

            // 面板数据面：四行文本（§5.4）。
            var lines = Ac.UI.NetworkPanel.BuildLines(snapshot);
            SelfTest.Equal(4, lines.Length);
            SelfTest.True(lines[0].StartsWith("rtt ", StringComparison.Ordinal), "首行是 rtt", lines[0]);
            SelfTest.True(lines[1].StartsWith("p99 ", StringComparison.Ordinal), "次行是 p99", lines[1]);
            SelfTest.True(lines[2].StartsWith("丢包 ", StringComparison.Ordinal), "三行是丢包", lines[2]);
            SelfTest.True(lines[3].StartsWith("收/发 ", StringComparison.Ordinal), "四行是收/发", lines[3]);
        }

        // ---- 驱动与桩件 ------------------------------------------------------------------------------

        private sealed class Clock
        {
            internal double NowMs;
            internal double Now() { return NowMs; }
            internal void Advance(double deltaMs) { NowMs += deltaMs; }
        }

        private static PacketHeader MakeHeader(PacketType type, ushort _session)
        {
            var header = new PacketHeader();
            header.Version = PacketHeader.ProtocolVersion;
            header.Type = type;
            header.Flags = (PacketFlags)PacketHeader.RequiredFlags(type);
            header.Session = _session;
            header.Seq = 1;
            header.MsgId = 1;
            return header;
        }

        private static byte[] ReadFragment(byte[] datagram, out PacketHeader header)
        {
            var reader = new PacketReader(datagram);
            var failure = PacketHeader.Read(reader, out header);
            if (failure != DecodeFailure.Ok) throw new InvalidOperationException("分片头读取失败：" + failure);
            byte[] body;
            reader.TryReadBytes(reader.Remaining, out body);
            return body;
        }

        private static void Drive(UdpTransport client, MiniPeer server, Clock clock, int iterations, double stepMs)
        {
            for (var i = 0; i < iterations; i++)
            {
                clock.Advance(stepMs);
                client.Poll(UdpTransport.MaxInboundPacketsPerPoll);
                server.Pump(clock.NowMs);
                Thread.Sleep(1);
            }
        }

        private static void DriveMemory(UdpTransport client, MiniPeer peer, MemoryLink link, Clock clock, int iterations, double stepMs)
        {
            for (var i = 0; i < iterations; i++)
            {
                clock.Advance(stepMs);
                link.NowMs = clock.NowMs;
                client.Poll(UdpTransport.MaxInboundPacketsPerPoll);
                peer.Pump(link.NowMs);
                link.Pump();
            }
        }

        // 内存链路的自动应答从端（测试用；用 PacketWriter/HandshakeCodec 直接构包）。
        private sealed class MiniPeer
        {
            private readonly IDatagramSocket _socket;
            private readonly byte[] _buffer = new byte[Fragmenter.MaxDatagramBytes * 2];
            private readonly Dictionary<int, ReliabilityChannel> _channels = new Dictionary<int, ReliabilityChannel>();
            private readonly FragmentReassembler _reassembler = new FragmentReassembler();
            private readonly List<byte[]> _fragmentBuffer = new List<byte[]>();
            private ushort _seq;
            private ushort _session = 7;
    
            internal MiniPeer(IDatagramSocket socket) { _socket = socket; }
            internal MemoryLink Link;
            internal bool DropFirstHello;
            internal bool Silent;
            internal bool RejectResume;
            internal int HelloCount;
            internal int ResumeCount;
            internal int ResumeRejectCount;
            internal int FragmentDatagrams;
            internal readonly List<byte[]> Reassembled = new List<byte[]>();

            private ReliabilityChannel ChannelFor(PacketType type)
            {
                // 直接复用生产侧的通道映射：桩件与客户端必须同口径（控制包与 Command 共用一条流）。
                var key = (int)UdpTransport.StreamOf(type);
                ReliabilityChannel channel;
                if (_channels.TryGetValue(key, out channel)) return channel;
                channel = new ReliabilityChannel();
                _channels[key] = channel;
                return channel;
            }
            internal uint LastNonce;
            private double _nowMs;
            internal uint ServerTick = 1234;
            internal uint Salt = 0xCAFEBABE;
            internal readonly List<byte[]> CommandsSeen = new List<byte[]>();
                internal int Port { get { return _socket.Port; } }
    
            internal void Attach(MemoryLink link) { Link = link; }

            internal void Start()
            {
                if (_socket is UdpTransport.RealUdpSocket)
                {
                    _socket.Bind(0);
                    return;
                }
            }

            internal void Stop()
            {
                _socket.Close();
            }

            internal void Pump(double nowMs)
            {
                _nowMs = nowMs;
                while (_socket.HasDatagram)
                {
                    var length = _socket.Receive(_buffer);
                    if (length <= 0) break;
                    var datagram = new byte[length];
                    Array.Copy(_buffer, datagram, length);
                    Handle(datagram, nowMs);
                }
                _reassembler.Expire(nowMs);
                bool exhausted;
                foreach (var pair in _channels) pair.Value.Tick(nowMs, ResendControl, out exhausted);
            }

            private void ResendControl(uint msgId, byte[] datagram)
            {
                _socket.Send(datagram, datagram.Length);
            }

            private void Handle(byte[] datagram, double nowMs)
            {
                _nowMs = nowMs;
                var reader = new PacketReader(datagram);
                PacketHeader header;
                if (PacketHeader.Read(reader, out header) != DecodeFailure.Ok) return;
                byte[] body;
                reader.TryReadBytes(reader.Remaining, out body);

                switch (header.Type)
                {
                    case PacketType.Hello:
                    {
                        HelloPayload hello;
                        if (HandshakeCodec.DecodeHello(body, out hello) != DecodeFailure.Ok) return;
                        HelloCount += 1;
                        LastNonce = hello.ClientNonce;
                        if (DropFirstHello && HelloCount == 1) return;   // 注入：首包丢掉
                        if (Silent) return;
                        SendHelloAck(nowMs);
                        return;
                    }
                    case PacketType.Resume:
                    {
                        ResumeCount += 1;
                        if (Silent) return;
                        uint token;
                        if (HandshakeCodec.DecodeResume(body, out token) != DecodeFailure.Ok) return;
                        if (RejectResume || token != (Salt ^ LastNonce))
                        {
                            ResumeRejectCount += 1;
                            SendControl(PacketType.Disconnect, HandshakeCodec.EncodeDisconnect(DisconnectReason.TokenInvalid), nowMs);
                            return;
                        }
                        // S04 §5.5：Resume 成功后服务端只补全量快照、不回 HelloAck——桩件必须与之一致。
                        SendMessage(PacketType.Snapshot, new byte[8]);
                        return;
                    }
                    case PacketType.KeepAlive:
                        Acknowledge(header);
                        if (!Silent) SendControl(PacketType.KeepAlive, new byte[0], nowMs);
                        return;
                }

                // 分片的 msgId 属于整条消息：逐片不登记、不投递，收齐后才登记（见下面的重组分支）。
                var isNew = header.IsMoreFragments || Acknowledge(header);
                if (!Silent) SendControl(PacketType.KeepAlive, new byte[0], nowMs);   // 补 ack，回程给客户端做 rtt

                if (header.IsMoreFragments)
                {
                    FragmentDatagrams += 1;
                    byte[] message;
                    var result = _reassembler.Accept(header, body, 0, body.Length, nowMs, out message);
                    if (result != DecodeFailure.Ok) return;
                    var inbound = ChannelFor(header.Type);
                    if (inbound.IsDuplicate(header.MsgId)) return;   // 整条消息重传：重组完成但已投递过
                    inbound.NoteReceived(header.MsgId);
                    if (!Silent) SendControl(PacketType.KeepAlive, new byte[0], nowMs);   // 收齐后立刻确认整条消息
                    Reassembled.Add(message);
                    return;
                }
                if (!isNew) return;   // 重传的重复命令：只补 ack，不二次投递
                Deliver(header.Type, body);
            }

            private void Deliver(PacketType type, byte[] body)
            {
                if (type == PacketType.Command) CommandsSeen.Add(body);
                }

            // 返回 true 表示这是新消息（可以投递）；重复的 msgId 直接丢弃，不二次投递。
            private bool Acknowledge(in PacketHeader header)
            {
                if (!header.IsReliable) return true;
                var channel = ChannelFor(header.Type);
                if (channel.IsDuplicate(header.MsgId)) return false;
                channel.NoteReceived(header.MsgId);
                return true;
            }

            private void SendHelloAck(double nowMs)
            {
                var payload = HandshakeCodec.EncodeHelloAck(ServerTick, Salt);
                SendControl(PacketType.HelloAck, payload, nowMs);
            }

            private void SendControl(PacketType type, byte[] payload, double nowMs)
            {
                if (type == PacketType.HelloAck) _session = 7;
                var header = new PacketHeader();
                header.Version = PacketHeader.ProtocolVersion;
                header.Type = type;
                header.Flags = (PacketFlags)PacketHeader.RequiredFlags(type);
                header.Session = _session;
                header.Seq = NextSeq();
                var channel = ChannelFor(type);
                header.MsgId = channel.NextMsgId();
                header.AckBase = channel.AckBase;
                header.AckBits = channel.AckBits;
                var datagram = PacketWriter.Build(header, payload, 0, payload.Length);
                _socket.Send(datagram, datagram.Length);
            }

            internal void SendEvent(byte[] payload, bool duplicate)
            {
                SendMessage(PacketType.Event, payload);
                if (duplicate) SendMessage(PacketType.Event, payload, replay: true);
            }

            internal void SendMessage(PacketType type, byte[] payload, bool replay = false)
            {
                var channel = ChannelFor(type);
                var header = new PacketHeader();
                header.Version = PacketHeader.ProtocolVersion;
                header.Type = type;
                header.Flags = (PacketFlags)PacketHeader.RequiredFlags(type);
                header.Session = _session;
                header.Seq = NextSeq();
                header.MsgId = replay ? _lastMsgId : channel.NextMsgId();
                header.AckBase = channel.AckBase;
                header.AckBits = channel.AckBits;

                var datagrams = new List<byte[]>();
                if (PacketWriter.Size(header) + payload.Length <= Fragmenter.MaxDatagramBytes)
                {
                    datagrams.Add(PacketWriter.Build(header, payload, 0, payload.Length));
                }
                else
                {
                    Fragmenter.Split(header, 1, payload, datagrams);
                }
                foreach (var datagram in datagrams)
                {
                    _socket.Send(datagram, datagram.Length);
                    if (!replay) channel.Track(header.MsgId, datagram, _nowMs);
                }
                if (!replay) _lastMsgId = header.MsgId;
            }

            private uint _lastMsgId;

            private ushort NextSeq()
            {
                _seq = (ushort)((_seq + 1) & 0xFFFF);
                return _seq;
            }
        }

        // C03 §5.7 的内存适配器：丢包按确定性 LCG 抽样，延迟带抖动，可注入乱序。
        private sealed class MemoryLink
        {
            private sealed class Pending
            {
                internal int To;
                internal byte[] Bytes;
                internal double DueMs;
                internal long Order;
            }

            private readonly List<Pending> _pending = new List<Pending>();
                private long _order;
            private uint _rng = 0x9E3779B9u;

            internal double NowMs;
            internal double LossRate;
            internal bool FailSends;      // 硬失败：Send 返回 false（socket 不可用的场景）
            internal double LatencyMs;
            internal double JitterMs;
            internal int Dropped;
            internal int Sent;

            internal IDatagramSocket Create(int id)
            {
                    return new MemoryEndpoint(this, id);
            }

            private double NextUnit()
            {
                _rng = _rng * 1664525u + 1013904223u;
                return (_rng >> 8) / 16777216.0;
            }

            internal void Transmit(int from, byte[] bytes)
            {
                Sent += 1;
                if (NextUnit() < LossRate)
                {
                    Dropped += 1;
                    return;
                }
                var pending = new Pending();
                pending.To = from == 1 ? 2 : 1;
                pending.Bytes = bytes;
                pending.DueMs = NowMs + LatencyMs + (NextUnit() * 2.0 - 1.0) * JitterMs;
                pending.Order = _order++;
                _pending.Add(pending);
            }

            internal void Pump()
            {
                for (var delivered = true; delivered; )
                {
                    delivered = false;
                    var best = -1;
                    for (var i = 0; i < _pending.Count; i++)
                    {
                        if (_pending[i].DueMs > NowMs) continue;
                        if (best < 0 || _pending[i].Order < _pending[best].Order) best = i;
                    }
                    if (best < 0) return;
                    var item = _pending[best];
                    _pending.RemoveAt(best);
                    MemoryEndpoint target = _endpoints[item.To];
                    target.Enqueue(item.Bytes);
                    delivered = true;
                }
            }

            private readonly Dictionary<int, MemoryEndpoint> _endpoints = new Dictionary<int, MemoryEndpoint>();

            private sealed class MemoryEndpoint : IDatagramSocket
            {
                private readonly MemoryLink _link;
                private readonly int _id;
                private readonly Queue<byte[]> _inbox = new Queue<byte[]>();

                internal MemoryEndpoint(MemoryLink link, int id)
                {
                    _link = link;
                    _id = id;
                    link._endpoints[id] = this;
                }

                internal void Enqueue(byte[] bytes) { _inbox.Enqueue(bytes); }

                public bool IsBound { get { return true; } }
                public int Port { get { return 40000 + _id; } }
                public bool HasDatagram { get { return _inbox.Count > 0; } }
                public bool Bind(int port) { return true; }
                public void Connect(string host, int port) { }
        
                public int Receive(byte[] buffer)
                {
                    if (_inbox.Count == 0) return 0;
                    var bytes = _inbox.Dequeue();
                    Array.Copy(bytes, buffer, bytes.Length);
                    return bytes.Length;
                }

                public bool Send(byte[] datagram, int length)
                {
                    if (_link.FailSends) return false;
                    var copy = new byte[length];
                    Array.Copy(datagram, copy, length);
                    _link.Transmit(_id, copy);
                    return true;
                }

                public void Close() { }
            }
        }
    }
}
