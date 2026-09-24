using System;

namespace Ac.Net
{
    // C03 §5.3 的五态枚举（顺序即文档表序，数值不入线，仅用于自检与面板显示）。
    public enum ConnectionState
    {
        Disconnected = 0,
        Connecting = 1,
        Connected = 2,
        Zombie = 3,
        Reconnecting = 4,
    }

    // 状态机只决定「该发什么控制包」，字节编码归传输层：这样状态机可以在虚拟时钟下单独驱动。
    public interface ISessionControlSink
    {
        void SendHello(uint clientNonce, uint reconnectToken);
        void SendKeepAlive();
        void SendResume(uint reconnectToken);
    }

    // C03 §5.3 的连接状态机 + §5.6 的心跳与断线判定。
    public sealed class SessionStateMachine
    {
        public const int KeepAliveIntervalMs = 500;
        public const int TimeoutMs = 3000;
        public const int GracePeriodMs = 30000;
        public const int HelloRetryIntervalMs = 1000;
        public const int HelloMaxAttempts = 5;
        public const int ResumeRetryIntervalMs = 1000;

        private readonly ISessionControlSink _sink;
        private readonly ReconnectTokenStore _tokens = new ReconnectTokenStore();

        private double _nextHelloMs;
        private double _nextKeepAliveMs;
        private double _graceStartMs;
        private double _nextResumeMs;

        public SessionStateMachine(ISessionControlSink sink)
        {
            if (sink == null) throw new ArgumentNullException("sink");
            _sink = sink;
            State = ConnectionState.Disconnected;
        }

        public ConnectionState State { get; private set; }
        public ushort Session { get; private set; }
        public uint ClientNonce { get; private set; }
        internal uint ServerTick { get; private set; }
        internal uint Salt { get; private set; }
        public int HelloAttempts { get; private set; }
        internal int ResumeAttempts { get; private set; }
        public double LastRecvMs { get; private set; }
        internal double GraceStartMs { get { return _graceStartMs; } }
        public string Endpoint { get; private set; }
        public bool DisconnectedByServer { get; private set; }
        public ReconnectTokenStore Tokens { get { return _tokens; } }

        public event Action<ConnectionState, ConnectionState> StateChanged;

        // §5.5：nonce 每次连接尝试只生成一次，Hello 重发复用同一个——服务端按 nonce 在 5s 内去重。
        public void StartConnect(string endpoint, uint clientNonce, double nowMs)
        {
            Endpoint = endpoint;
            ClientNonce = clientNonce;
            Session = 0;
            ServerTick = 0;
            Salt = 0;
            HelloAttempts = 0;
            ResumeAttempts = 0;
            DisconnectedByServer = false;
            LastRecvMs = nowMs;
            _nextHelloMs = nowMs;
            _nextKeepAliveMs = nowMs;
            Transition(ConnectionState.Connecting, nowMs);
        }

        public bool OnHelloAck(ushort session, uint serverTick, uint salt, double nowMs)
        {
            if (State != ConnectionState.Connecting && State != ConnectionState.Reconnecting) return false;
            if (session == 0) return false;   // §5.3：会话号非 0 才算握手成立
            Session = session;
            ServerTick = serverTick;
            Salt = salt;
            ResumeAttempts = 0;
            LastRecvMs = nowMs;
            _nextKeepAliveMs = nowMs + KeepAliveIntervalMs;
            _tokens.Remember(Endpoint, HandshakeCodec.ReconnectToken(ClientNonce, salt));
            Transition(ConnectionState.Connected, nowMs);
            return true;
        }

        // 收到任何包含本会话的合法包即刷新存活时间。Zombie / Reconnecting 下收到包都直接回 Connected：
        // §5.5 的服务端在 Resume 成功时不回 HelloAck（只补全量快照），所以「对端又开始用本会话说话了」
        // 才是重连成功的通用判据；HelloAck 只是其中一种（首次握手）。
        public void OnPacket(ushort session, double nowMs)
        {
            if (Session == 0 || session != Session) return;
            LastRecvMs = nowMs;
            if (State == ConnectionState.Zombie || State == ConnectionState.Reconnecting)
            {
                Transition(ConnectionState.Connected, nowMs);
            }
        }

        // 服务端明确断开：会话与令牌一起作废，回 Disconnected（§5.5 的 reason 语义）。
        public void OnDisconnect(DisconnectReason reason, double nowMs)
        {
            DisconnectedByServer = true;
            _tokens.Forget(Endpoint);
            Session = 0;
            Transition(ConnectionState.Disconnected, nowMs);
        }

        // Resume 被拒（Disconnect reason=2）或宽限期已过：令牌作废并回退到新会话握手（§8 的令牌纪律）。
        public void OnResumeRejected(double nowMs)
        {
            _tokens.Forget(Endpoint);
            Session = 0;
            Transition(ConnectionState.Disconnected, nowMs);
        }

        // §5.3：累计重传达到 MaxRetransmits → 判对端失联 → 进入宽限期（Zombie），随后尝试 Resume。
        public void OnRetransmitExhausted(double nowMs)
        {
            if (State != ConnectionState.Connected) return;
            EnterGrace(nowMs);
        }

        // 每个 Poll 调用一次。返回本次发出的控制包个数。
        public int Tick(double nowMs)
        {
            var sent = 0;
            switch (State)
            {
                case ConnectionState.Connecting:
                    if (nowMs >= _nextHelloMs)
                    {
                        if (HelloAttempts >= HelloMaxAttempts)
                        {
                            Transition(ConnectionState.Disconnected, nowMs);
                            break;
                        }
                        _sink.SendHello(ClientNonce, 0u);   // 首次连接令牌填 0
                        HelloAttempts += 1;
                        _nextHelloMs = nowMs + HelloRetryIntervalMs;
                        sent += 1;
                    }
                    break;

                case ConnectionState.Connected:
                    if (nowMs - LastRecvMs >= TimeoutMs)
                    {
                        EnterGrace(nowMs);
                        break;
                    }
                    if (nowMs >= _nextKeepAliveMs)
                    {
                        _sink.SendKeepAlive();
                        _nextKeepAliveMs = nowMs + KeepAliveIntervalMs;
                        sent += 1;
                    }
                    break;

                case ConnectionState.Zombie:
                    if (nowMs - _graceStartMs >= GracePeriodMs)
                    {
                        GiveUp(nowMs);
                        break;
                    }
                    sent += StartResume(nowMs);
                    break;

                case ConnectionState.Reconnecting:
                    if (nowMs - _graceStartMs >= GracePeriodMs)
                    {
                        GiveUp(nowMs);
                        break;
                    }
                    if (nowMs >= _nextResumeMs) sent += SendResume(nowMs);
                    break;
            }
            return sent;
        }

        public void Reset(double nowMs)
        {
            Session = 0;
            HelloAttempts = 0;
            ResumeAttempts = 0;
            LastRecvMs = nowMs;
            _tokens.Forget(Endpoint);
            Transition(ConnectionState.Disconnected, nowMs);
        }

        private void EnterGrace(double nowMs)
        {
            _graceStartMs = nowMs;
            Transition(ConnectionState.Zombie, nowMs);
        }

        // 宽限期内发起 Resume（§5.3 的 Reconnecting 进入条件之一）。没有可用令牌就直接放弃。
        private int StartResume(double nowMs)
        {
            if (State != ConnectionState.Zombie) return 0;
            uint token;
            if (!_tokens.TryTake(Endpoint, out token))
            {
                GiveUp(nowMs);
                return 0;
            }
            Transition(ConnectionState.Reconnecting, nowMs);
            return SendResume(nowMs) == 0 ? 0 : 1;
        }

        private int SendResume(double nowMs)
        {
            uint token;
            if (!_tokens.TryTake(Endpoint, out token))
            {
                GiveUp(nowMs);
                return 0;
            }
            _sink.SendResume(token);
            ResumeAttempts += 1;
            _nextResumeMs = nowMs + ResumeRetryIntervalMs;
            return 1;
        }

        private void GiveUp(double nowMs)
        {
            _tokens.Forget(Endpoint);
            Session = 0;
            Transition(ConnectionState.Disconnected, nowMs);
        }

        private void Transition(ConnectionState next, double nowMs)
        {
            if (State == next) return;
            var previous = State;
            State = next;
            var handler = StateChanged;
            if (handler != null) handler(previous, next);
        }
    }
}
