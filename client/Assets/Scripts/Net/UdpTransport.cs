using System;
using System.Collections.Generic;
using System.Net;
using System.Net.Sockets;

namespace Ac.Net
{
    // C03 §5.5 的数据报缝：生产侧是实现非阻塞套接字的 RealUdpSocket，测试侧是内存链路
    //（注入丢包/延迟/乱序 + 虚拟时钟）。传输层只依赖这五个动作，所以回环对拍不需要真网卡。
    public interface IDatagramSocket
    {
        bool IsBound { get; }
        int Port { get; }
        bool HasDatagram { get; }
        bool Bind(int port);
        void Connect(string host, int port);
        int Receive(byte[] buffer);
        bool Send(byte[] datagram, int length);
        void Close();
    }

    // C03 §3/§9 的传输缝：非阻塞收发、握手与心跳、分片、可靠性，全部由 Poll 驱动（不阻塞主线程）。
    public sealed class UdpTransport : ISessionControlSink
    {
        public const int MaxInboundPacketsPerPoll = 64;
        public const int OutboundBacklogBytes = 65536;

        // 真套接字：Blocking = false，且不设置任何读写超时（§5.5 的硬要求，也是 §6 第 3 条扫描的判据）。
        public sealed class RealUdpSocket : IDatagramSocket
        {
            private Socket _socket;

            public bool IsBound { get { return _socket != null; } }
            public int Port { get { return _socket == null ? 0 : ((IPEndPoint)_socket.LocalEndPoint).Port; } }

            public bool Bind(int port)
            {
                if (_socket != null) return true;
                _socket = new Socket(AddressFamily.InterNetwork, SocketType.Dgram, ProtocolType.Udp);
                // 非阻塞是硬要求：任何阻塞读都会把帧时间打成尖刺（§5.5）。
                _socket.Blocking = false;
                // 绑 Any 而不是 Loopback：客户端要能连局域网/外网服务端（§9 的 Connect(host, port)）。
                // 回环用例仍然连 127.0.0.1，行为不变。
                _socket.Bind(new IPEndPoint(IPAddress.Any, port));
                return true;
            }

            public void Connect(string host, int port)
            {
                if (_socket == null) Bind(0);
                _socket.Connect(new IPEndPoint(Resolve(host), port));
                _connected = true;
            }

            private static IPAddress Resolve(string host)
            {
                IPAddress parsed;
                if (IPAddress.TryParse(host, out parsed)) return parsed;
                var addresses = Dns.GetHostAddresses(host);
                for (var i = 0; i < addresses.Length; i++)
                {
                    if (addresses[i].AddressFamily == AddressFamily.InterNetwork) return addresses[i];
                }
                if (addresses.Length > 0) return addresses[0];
                throw new SocketException((int)SocketError.HostNotFound);
            }

            public bool HasDatagram
            {
                get { return _socket != null && _socket.Poll(0, SelectMode.SelectRead); }
            }

            // 返回 0 表示当前没有可读数据报（非阻塞语义），不抛异常。
            public int Receive(byte[] buffer)
            {
                if (_socket == null || !_socket.Poll(0, SelectMode.SelectRead)) return 0;
                try
                {
                    if (_connected) return _socket.Receive(buffer);
                    // 未 connect 的对端（回环用例的服务端桩件）：记下来源地址，回包用 SendTo。
                    EndPoint from = new IPEndPoint(IPAddress.Any, 0);
                    var received = _socket.ReceiveFrom(buffer, ref from);
                    if (received > 0) _lastFrom = from;
                    return received;
                }
                catch (SocketException)
                {
                    return 0;   // ICMP 端口不可达等错误按「没收到包」处理，交给超时判定
                }
            }

            public bool Send(byte[] datagram, int length)
            {
                if (_socket == null) return false;
                if (!_connected)
                {
                    if (_lastFrom == null) return true;   // 还没有对端地址，静默丢掉（与内核行为一致）
                    try
                    {
                        return _socket.SendTo(datagram, 0, length, SocketFlags.None, _lastFrom) == length;
                    }
                    catch (SocketException)
                    {
                        return false;
                    }
                }
                try
                {
                    return _socket.Send(datagram, 0, length, SocketFlags.None) == length;
                }
                catch (SocketException error)
                {
                    // 内核缓冲满会自愈，所以不抛给调用方；但持续失败必须留下线索（每个套接字只报一次）。
                    if (!_sendErrorReported)
                    {
                        _sendErrorReported = true;
                        UnityEngine.Debug.LogWarning("[udp] 发送失败：" + error.SocketErrorCode);
                    }
                    return false;   // 留在积压队列里，下一次 Poll 重试
                }
            }

            private bool _sendErrorReported;
            private bool _connected;
            private EndPoint _lastFrom;

            public void Close()
            {
                if (_socket == null) return;
                try
                {
                    _socket.Close();
                }
                catch (SocketException)
                {
                }
                _socket = null;
            }
        }

        private readonly IDatagramSocket _socket;
        private readonly Func<double> _clockMs;
        private readonly SessionStateMachine _session;
        private readonly FragmentReassembler _reassembler = new FragmentReassembler();
        private readonly Dictionary<int, ReliabilityChannel> _channels = new Dictionary<int, ReliabilityChannel>();
        private readonly Queue<byte[]> _backlog = new Queue<byte[]>();
        private readonly byte[] _receiveBuffer = new byte[Fragmenter.MaxDatagramBytes * 2];

        private int _backlogBytes;
        private ushort _fragIdCounter;
        private uint _lastKeepAliveMsgId;
        private double _lastKeepAliveSentMs = -1.0;

        public UdpTransport(IDatagramSocket socket)
            : this(socket, null)
        {
        }

        public UdpTransport(IDatagramSocket socket, Func<double> clockMs)
        {
            if (socket == null) throw new ArgumentNullException("socket");
            _socket = socket;
            _clockMs = clockMs ?? DefaultClock;
            Stats = new NetStats();
            _session = new SessionStateMachine(this);
            _session.StateChanged += OnStateChanged;
            Stats.Reset(NowMs);
        }

        public NetStats Stats { get; private set; }
        public SessionStateMachine Machine { get { return _session; } }
        public ConnectionState State { get { return _session.State; } }
        public ushort Session { get { return _session.Session; } }
        public string Endpoint { get { return _session.Endpoint; } }
        internal int BacklogBytes { get { return _backlogBytes; } }

        public event Action<ConnectionState, ConnectionState> StateChanged;
        public event Action<PacketHeader, byte[]> ApplicationPacket;

        public bool Connect(string host, int port)
        {
            if (!_socket.IsBound && !_socket.Bind(0)) return false;
            _socket.Connect(host, port);
            var endpoint = host + ":" + port.ToString(System.Globalization.CultureInfo.InvariantCulture);
            Stats.Reset(NowMs);
            // nonce 不消费 ai/spawn/fx 任何流：握手取随机数会让 FX 与生成的对拍结果不可复现
            //（S04 §5.5 对 salt 有同一条纪律）。用端点哈希 + 时钟派生，重发 Hello 复用同一个值。
            var nonce = HandshakeCodec.DeriveNonce(endpoint, NowMs);
            _session.StartConnect(endpoint, nonce, NowMs);
            _session.Tick(NowMs);   // 首次 Hello 立即发出，不等下一个心跳周期
            return true;
        }

        // §9 的 Poll(maxPackets)：先跑状态机（心跳/重传/超时），再收包，最后滚动统计。
        public int Poll(int maxPackets)
        {
            var now = NowMs;
            _session.Tick(now);

            var processed = 0;
            while (processed < maxPackets)
            {
                if (!_socket.HasDatagram) break;
                var length = _socket.Receive(_receiveBuffer);
                if (length <= 0) break;
                processed += 1;
                HandleDatagram(length, now);
            }

            FlushBacklog();
            TickChannels(now);

            var expiredGroups = _reassembler.Expire(now);
            for (var i = 0; i < expiredGroups; i++) Stats.OnInvalidPacket();

            ReliabilityChannel rtoChannel;
            Stats.SetRtoMs(_channels.TryGetValue((int)PacketType.Command, out rtoChannel)
                ? rtoChannel.CurrentRtoMs
                : ReliabilityChannel.RtoTableMs[0]);
            Stats.SetState(_session.State);
            Stats.Tick(now);
            return processed;
        }

        // §9 的 Send(channel, payload)：超 1176 字节自动分片；返回 false 表示未受理（未连接或超长）。
        public bool Send(PacketType channel, byte[] payload)
        {
            var payloadBytes = payload == null ? 0 : payload.Length;
            if (channel != PacketType.Hello && channel != PacketType.Resume && channel != PacketType.Disconnect)
            {
                if (_session.State != ConnectionState.Connected) return false;
            }
            bool reliable;
            uint msgId;
            var datagrams = BuildDatagrams(channel, payload, 0, payloadBytes, out reliable, out msgId);
            if (datagrams.Count == 0) return false;
            if (reliable)
            {
                // 分片消息的每一片共用同一个 msgId，都挂在逻辑通道的重传表上：分片本身不另开一条可靠流。
                var stream = ChannelFor(channel);
                foreach (var datagram in datagrams) stream.Track(msgId, datagram, NowMs);
            }
            return Enqueue(datagrams);
        }

        public void Close()
        {
            _socket.Close();
            _session.Reset(NowMs);
            _backlog.Clear();
            _backlogBytes = 0;
            ReassemblerReset();
        }

        // ---- ISessionControlSink：状态机只说要发什么，字节与通道归这里 ------------------------------------

        void ISessionControlSink.SendHello(uint clientNonce, uint reconnectToken)
        {
            var header = new PacketHeader();
            header.Version = PacketHeader.ProtocolVersion;
            header.Type = PacketType.Hello;
            header.Flags = (PacketFlags)PacketHeader.RequiredFlags(PacketType.Hello);
            header.Session = 0;
            header.Seq = 0;
            SendRaw(PacketWriter.Build(header, HandshakeCodec.EncodeHello(clientNonce, reconnectToken), 0, HandshakeCodec.HelloPayloadBytes));
        }

        void ISessionControlSink.SendResume(uint reconnectToken)
        {
            Send(PacketType.Resume, HandshakeCodec.EncodeResume(reconnectToken));
        }

        void ISessionControlSink.SendKeepAlive()
        {
            var channel = CommandChannel;
            var header = new PacketHeader();
            header.Version = PacketHeader.ProtocolVersion;
            header.Type = PacketType.KeepAlive;
            header.Flags = (PacketFlags)PacketHeader.RequiredFlags(PacketType.KeepAlive);
            header.Session = _session.Session;
            header.Seq = NextSeq(PacketType.KeepAlive);
            // ackOnly 不进重传表（S04 §5.6），但照样消费一个 msgId 并带上本端 ack 位图。
            header.MsgId = channel.NextMsgId();
            header.AckBase = channel.AckBase;
            header.AckBits = channel.AckBits;
            _lastKeepAliveMsgId = header.MsgId;
            _lastKeepAliveSentMs = NowMs;
            SendRaw(PacketWriter.Build(header));
        }

        // ---- 收包路径 -----------------------------------------------------------------------------------

        private void HandleDatagram(int length, double now)
        {
            var bytes = new byte[length];
            Array.Copy(_receiveBuffer, bytes, length);
            var reader = new PacketReader(bytes);
            PacketHeader header;
            var failure = PacketHeader.Read(reader, out header);
            if (failure != DecodeFailure.Ok)
            {
                Stats.OnInvalidPacket();
                if (failure == DecodeFailure.BadVersion) SendVersionMismatch(bytes);
                return;
            }

            // §5.2 的会话校验：Hello 必须 session=0；其余包必须带本会话。HelloAck 例外——它正是会话号的
            // 分配者（Connecting 时本端还不知道会话号），所以只要求「尚未握手时接受它、已握手时与本会话一致」。
            var sessionAccepted = header.Type == PacketType.Hello
                ? header.Session == 0
                : header.Type == PacketType.HelloAck
                    ? (_session.Session == 0 || header.Session == _session.Session)
                    : header.Session == _session.Session;
            if (!sessionAccepted)
            {
                Stats.OnInvalidPacket();
                return;
            }

            byte[] payload;
            reader.TryReadBytes(reader.Remaining, out payload);

            if (header.IsMoreFragments)
            {
                byte[] message;
                var reassembly = _reassembler.Accept(header, payload, 0, payload.Length, now, out message);
                if (reassembly == DecodeFailure.Truncated) return;
                if (reassembly != DecodeFailure.Ok)
                {
                    Stats.OnInvalidPacket();
                    return;
                }
                // §5.4：片头 type 恒为 Fragment，逻辑类型只能由 reliable 位推回（事件分片可靠、快照分片不可靠）。
                header.Type = header.IsReliable ? PacketType.Event : PacketType.Snapshot;
                header.Flags = header.IsReliable ? PacketFlags.Reliable : PacketFlags.None;
                header.FragId = 0;
                header.FragIndex = 0;
                header.FragCount = 0;
                payload = message;
                // 片本身的 msgId 属于整条消息：收齐后才登记进 ack 窗口，并由下面的 Dispatch 按 msgId 去重
                //（否则重传的整条消息会被二次投递）。
                Dispatch(header, payload, now, true);
                return;
            }

            Dispatch(header, payload, now, false);
        }

        // reassembled 只影响统计口径：fragmentsReassembled 记「收齐后真正投递的逻辑消息」，重复副本仍计 duplicates。
        private void Dispatch(in PacketHeader header, byte[] payload, double now, bool reassembled)
        {
            Stats.OnInbound(header.Type, header.Seq, payload.Length + PacketWriter.Size(header));

            if (header.IsReliable)
            {
                var channel = ChannelFor(header.Type);
                // 分片包不经过这里：它们的 msgId 属于整条消息，靠重组器的 fragIndex 幂等落位，收齐后才登记。
                if (channel.IsDuplicate(header.MsgId))
                {
                    Stats.OnDuplicate();
                    // 重复包同样证明对端还活着：不刷新存活时间的话，被重传噪音淹没的一侧会误判失联。
                    _session.OnPacket(header.Session, now);
                    return;
                }
                if (reassembled) Stats.OnFragmentReassembled();
                channel.NoteReceived(header.MsgId);
                channel.Acknowledge(header.AckBase, header.AckBits);
                // §5.1：ack 位图只描述该包自己那条通道，所以只有命令流的包才能确认本端命令流的心跳。
                if (StreamOf(header.Type) == PacketType.Command) AcknowledgeControl(header.AckBase, header.AckBits, now);
            }

            _session.OnPacket(header.Session, now);

            switch (header.Type)
            {
                case PacketType.HelloAck:
                {
                    HelloAckPayload ack;
                    if (HandshakeCodec.DecodeHelloAck(payload, out ack) != DecodeFailure.Ok)
                    {
                        Stats.OnInvalidPacket();
                        return;
                    }
                    _session.OnHelloAck(header.Session, ack.ServerTick, ack.Salt, now);
                    return;
                }
                case PacketType.Disconnect:
                {
                    DisconnectReason reason;
                    if (HandshakeCodec.DecodeDisconnect(payload, out reason) != DecodeFailure.Ok)
                    {
                        Stats.OnInvalidPacket();
                        return;
                    }
                    _session.OnDisconnect(reason, now);
                    return;
                }
                case PacketType.KeepAlive:
                    return;
                default:
                {
                    var handler = ApplicationPacket;
                    if (handler != null) handler(header, payload);
                    return;
                }
            }
        }

        // ---- 发送路径 -----------------------------------------------------------------------------------

        private List<byte[]> BuildDatagrams(PacketType channel, byte[] payload, int offset, int count,
            out bool reliable, out uint msgId)
        {
            reliable = false;
            msgId = 0;
            var flags = (PacketFlags)PacketHeader.RequiredFlags(channel);
            var header = new PacketHeader();
            header.Version = PacketHeader.ProtocolVersion;
            header.Type = channel;
            header.Flags = flags;
            header.Session = channel == PacketType.Hello ? (ushort)0 : _session.Session;
            header.Seq = NextSeq(channel);

            reliable = (flags & PacketFlags.Reliable) != 0;
            if (reliable)
            {
                var stream = ChannelFor(channel);
                header.MsgId = stream.NextMsgId();
                header.AckBase = stream.AckBase;
                header.AckBits = stream.AckBits;
                msgId = header.MsgId;
            }

            var datagrams = new List<byte[]>();
            if (!Fragmenter.NeedsFragmentation(header, count))
            {
                datagrams.Add(PacketWriter.Build(header, payload, offset, count));
                return datagrams;
            }
            if (channel == PacketType.Hello || channel == PacketType.Resume || channel == PacketType.Disconnect)
            {
                return datagrams;   // 控制包不允许分片（都远小于上限，超了就是程序错误）
            }
            _fragIdCounter = (ushort)((_fragIdCounter + 1) & 0xFFFF);
            if (_fragIdCounter == 0) _fragIdCounter = 1;
            var slice = new byte[count];
            if (count > 0) Array.Copy(payload, offset, slice, 0, count);
            if (Fragmenter.Split(header, _fragIdCounter, slice, datagrams) == 0) datagrams.Clear();
            return datagrams;
        }

        private bool Enqueue(List<byte[]> datagrams)
        {
            if (datagrams.Count == 0) return false;
            var bytes = 0;
            foreach (var datagram in datagrams) bytes += datagram.Length;
            // §5.1：超上限先丢最旧的快照腾地方；连快照都没有就**拒收**（不入队、返回 false）。
            // 旧实现写着"拒收"却恒返回 true，命令流的积压因此可以无限增长。
            if (_backlogBytes + bytes > OutboundBacklogBytes && !MakeRoom(bytes)) return false;
            foreach (var datagram in datagrams)
            {
                _backlog.Enqueue(datagram);
                _backlogBytes += datagram.Length;
            }
            FlushBacklog();
            return true;
        }

        // 丢最旧的快照，直到能装下这一批 bytes；一个都丢不动（全是命令/事件/可靠分片）时返回 false。
        private bool MakeRoom(int bytes)
        {
            while (_backlogBytes + bytes > OutboundBacklogBytes)
            {
                if (!DropOldestSnapshot()) return false;   // 命令与事件不丢
            }
            return true;
        }

        // 保序重建：旧实现把非快照数据报 Enqueue 回**队尾**，于是 [cmd1, snap, cmd2] 会被旋转成
        // [cmd2, cmd1] —— 同通道命令乱序，服务端按 seq 判定会直接丢掉旧命令（validate.cpp 的 kStaleTick）。
        // 暂存队列是常驻字段，稳态下不分配。
        private readonly Queue<byte[]> _scratch = new Queue<byte[]>();

        private bool DropOldestSnapshot()
        {
            _scratch.Clear();
            var dropped = false;
            while (_backlog.Count > 0)
            {
                var datagram = _backlog.Dequeue();
                if (!dropped && IsDroppableSnapshot(datagram))
                {
                    _backlogBytes -= datagram.Length;
                    Stats.OnDroppedSnapshot();
                    dropped = true;
                    continue;
                }
                _scratch.Enqueue(datagram);
            }
            while (_scratch.Count > 0) _backlog.Enqueue(_scratch.Dequeue());
            return dropped;
        }

        // 快照本体（type 5）与**不可靠通道**的分片（type 9 且可靠位未置）都可丢：
        // 丢一片等于丢整条快照，这正是快照通道允许的补偿；可靠分片（命令/事件）永不丢。
        private static bool IsDroppableSnapshot(byte[] datagram)
        {
            var type = datagram[PacketWriter.TypeOffset];
            if (type == (byte)PacketType.Snapshot) return true;
            if (type != (byte)PacketType.Fragment) return false;
            // flags 是 u16 小端：Reliable 等低位就在 FlagsOffset 那个字节上。
            return (datagram[PacketWriter.FlagsOffset] & (byte)PacketFlags.Reliable) == 0;
        }

        private void FlushBacklog()
        {
            while (_backlog.Count > 0)
            {
                var datagram = _backlog.Peek();
                if (!_socket.Send(datagram, datagram.Length)) return;
                _backlog.Dequeue();
                _backlogBytes -= datagram.Length;
                Stats.OnOutbound(datagram.Length);
            }
        }

        private void SendRaw(byte[] datagram)
        {
            if (_socket.Send(datagram, datagram.Length))
            {
                Stats.OnOutbound(datagram.Length);
                return;
            }
            _backlog.Enqueue(datagram);
            _backlogBytes += datagram.Length;
            MakeRoom(0);
        }

        private void TickChannels(double now)
        {
            // 先复制一份再遍历：失联回调会经状态机新开一条可靠流（Resume），不能边遍历边改字典。
            var channels = new List<ReliabilityChannel>(_channels.Values);
            foreach (var channel in channels)
            {
                bool exhausted;
                var resent = channel.Tick(now, ResendDatagram, out exhausted);
                for (var i = 0; i < resent; i++) Stats.OnRetransmit();
                if (exhausted) _session.OnRetransmitExhausted(now);
            }
        }

        private void ResendDatagram(uint msgId, byte[] datagram)
        {
            SendRaw(datagram);
        }

        private void AcknowledgeControl(uint ackBase, uint ackBits, double now)
        {
            if (_lastKeepAliveSentMs < 0.0) return;
            if (!ReliabilityChannel.IsAcked(_lastKeepAliveMsgId, ackBase, ackBits)) return;
            Stats.OnRttSample(now - _lastKeepAliveSentMs);
            _lastKeepAliveSentMs = -1.0;
        }

        private void OnStateChanged(ConnectionState previous, ConnectionState next)
        {
            if (next != ConnectionState.Connected) _reassembler.Reset();   // 半截分片组一律作废
            if (next == ConnectionState.Connecting || next == ConnectionState.Disconnected)
            {
                // 会话结束（或另起一次握手）才丢在途消息；Reconnecting 期间保留重传表，
                // Resume 恢复的是同一个会话，msgId 继续单调，回来之后照旧重传。
                foreach (var pair in _channels) pair.Value.DropOutstanding();
            }
            var handler = StateChanged;
            if (handler != null) handler(previous, next);
        }

        private void ReassemblerReset()
        {
            _reassembler.Reset();
            foreach (var pair in _channels) pair.Value.Reset();
            _channels.Clear();
        }

        public int OutstandingMessages
        {
            get
            {
                var total = 0;
                foreach (var pair in _channels) total += pair.Value.OutstandingCount;
                return total;
            }
        }

        // 通道口径：msgId 与 seq 都按包类型分流（§5.2「各一条发送 seq 与一条接收 seq」），
        // 但控制包（Resume/Disconnect/KeepAlive）与 Command 共用同一条可靠流——它们必须与本端命令
        // 处在同一条单调 msgId 序列里，否则对端按 msgId 去重时会互相顶掉。
        public static PacketType StreamOf(PacketType type)
        {
            switch (type)
            {
                case PacketType.Event: return PacketType.Event;
                case PacketType.Snapshot: return PacketType.Snapshot;
                default: return PacketType.Command;
            }
        }

        // 一条逻辑流一个对象：发送序列（SendMsgId/重传表）与接收窗口（AckBase/AckBits/去重）同处一室，
        // 与 S04 的 Channel 相同。因此收到 ack 位图直接落回本流的重传表，不会跑到别的流上。
        private ReliabilityChannel ChannelFor(PacketType type)
        {
            var key = (int)StreamOf(type);
            ReliabilityChannel channel;
            if (_channels.TryGetValue(key, out channel)) return channel;
            channel = new ReliabilityChannel();
            _channels[key] = channel;
            return channel;
        }

        private ReliabilityChannel CommandChannel
        {
            get { return ChannelFor(PacketType.Command); }
        }

        private readonly Dictionary<int, ushort> _seqStreams = new Dictionary<int, ushort>();

        // §5.2：每通道一条发送 seq，从 1 起、mod 2^16 回绕（0 只用于握手前的包）。
        private ushort NextSeq(PacketType type)
        {
            var key = (int)type;
            ushort seq;
            if (!_seqStreams.TryGetValue(key, out seq)) seq = 0;
            seq = (ushort)((seq + 1) & (ReliabilityChannel.SeqModulo - 1));
            _seqStreams[key] = seq;
            return seq;
        }

        // §5.2：入站包 version 不符一律回 Disconnect(reason=1)。不追踪重传——对端版本不符时本会话已无意义。
        private void SendVersionMismatch(byte[] datagram)
        {
            var header = new PacketHeader();
            header.Version = PacketHeader.ProtocolVersion;
            header.Type = PacketType.Disconnect;
            header.Flags = (PacketFlags)PacketHeader.RequiredFlags(PacketType.Disconnect);
            header.Session = datagram.Length >= 6 ? (ushort)(datagram[4] | (datagram[5] << 8)) : (ushort)0;
            header.Seq = NextSeq(PacketType.Disconnect);
            header.MsgId = CommandChannel.NextMsgId();
            header.AckBase = CommandChannel.AckBase;
            header.AckBits = CommandChannel.AckBits;
            SendRaw(PacketWriter.Build(header,
                HandshakeCodec.EncodeDisconnect(DisconnectReason.VersionMismatch), 0, HandshakeCodec.DisconnectPayloadBytes));
        }

        private static double DefaultClock()
        {
            return Environment.TickCount & 0x7FFFFFFF;
        }

        private double NowMs { get { return _clockMs(); } }

    }
}
