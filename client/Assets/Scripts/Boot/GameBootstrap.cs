using System;
using System.Globalization;
using Ac.Core;
using Ac.Net;
using Ac.Sim;
using Ac.UI;
using Ac.View;
using UnityEngine;

namespace Ac.Boot
{
    // 运行期根：仓库里 0 个 .unity 场景、0 个 MonoBehaviour（审计 H6），所以这里必须自举——
    // 不依赖任何场景内容，BeforeSceneLoad 时把帧回路挂上，任何人按 Play 或出包都能跑起来。
    public static class GameBootstrap
    {
        public const string RootName = "Ac.Boot";

        // 服务器地址只从配置来：命令行 -server host:port → 环境变量 AC_SERVER → 这两个默认值。
        public const string DefaultServerHost = "127.0.0.1";
        public const int DefaultServerPort = 8787;
        public const string ServerArgument = "-server";
        public const string ServerEnvironment = "AC_SERVER";

        public static GameLoop Loop { get; private set; }
        // B1：呈现层（相机/场地/羊群/特效/屏幕流/调试面板）。此前它整个不存在，所以画面是空的。
        public static PresentationLayer Presentation { get; private set; }
        public static SettingsStore Settings { get; private set; }
        // 解析后的连接目标（ResolveServer 的结果）。null host = 本进程不连接。
        public static string ServerHost { get; private set; }
        public static int ServerPort { get; private set; }
        // 调试面板热键：从 SettingsDefaults.KeyBindings[ActionDebugPanel] 的表里解析，不在代码里写 F3。
        public static KeyCode DebugPanelKey { get; private set; }
        // 大厅准备键（ADR-013）：同样从键位表（[ActionReady]，默认表那条是 Return）解析，
        // 采样器里不写死键 —— 设置面板改了它就跟着改。
        public static KeyCode ReadyKey { get; private set; }

        private static bool _settingsSubscribed;

        [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.BeforeSceneLoad)]
        public static void Start()
        {
            if (Settings == null)
            {
                Settings = new SettingsStore();
                Settings.Load(Application.persistentDataPath);
            }
            if (!_settingsSubscribed)
            {
                Settings.Changed += OnSettingsChanged;
                _settingsSubscribed = true;
            }
            var settings = Settings.Get();
            DebugPanelKey = DebugPanelKeyFor(settings.KeyBindings);
            ReadyKey = ReadyKeyFor(settings.KeyBindings);
            if (Loop == null) Loop = new GameLoop(new SnapshotView(), new EntityViews(), new Hud(), new FrameProfiler());
            // 传输/会话（C03）：接上之后 Transport.Poll 才会真的收包，type=10 的相位与 type=5 的快照
            // 才有入口进 GameLoop（否则屏幕流恒为 lobby、draw 恒不打点——"装配了但没驱动"）。
            AttachTransport(Loop);
            // 输入（C05 §5.1/§5.6）：采样器此前没有任何生产调用者（连 `new` 都没有）⇒ 上行、预测、准备位、
            // 指针锁定四条路径全断。这里把它接进帧回路；离线（单机/帧基准）也照接 —— 采样器自己按焦点说话。
            if (Loop.Sampler == null) Loop.Sampler = new InputSampler();
            // 键位与灵敏度都来自设置快照：面板里改了要真的生效（C13 的三项设置此前只有存盘没有下游）。
            Loop.Sampler.ConfirmKey = ReadyKey;
            Loop.Sampler.SetSensitivity(settings.Sensitivity);
            // 呈现层：造出 Unity 对象，再把三条呈现缝与帧回路接起来（没接上的段就不打点）
            if (Presentation == null) Presentation = PresentationLayer.Create(settings);
            Presentation.Attach(Loop);
            Presentation.ApplySettings(settings);
            Loop.Fx = Presentation.FxSink;
            Loop.Draw = Presentation.DrawSink;
            Loop.Overlay = Presentation.OverlaySink;
            // 音频：设备不可用（无头/Null Device）时不要假装有，保持该段未打点。
            if (Loop.Audio == null)
            {
                try
                {
                    Ac.Audio.Mixer.EnsureStarted();      // 静态启动：设备就绪后才有 mix
                    var mixer = new Ac.Audio.Mixer();
                    Loop.Audio = new MixerSink(mixer);
                }
                catch (Exception ex)
                {
                    Debug.LogWarning("Ac.Boot: audio unavailable, 'audio' stage stays unmarked: " + ex.Message);
                }
            }
            if (GameObject.Find(RootName) != null) return;
            var root = new GameObject(RootName);
            UnityEngine.Object.DontDestroyOnLoad(root);
            root.AddComponent<GameLoopDriver>();
        }

        private static void OnSettingsChanged(SettingsKey key)
        {
            // 键位表改了要重新解析两个热键；灵敏度改了要立刻进采样器 ——
            // 否则"设置面板里改了没反应"，存盘的值与实际行为两张皮。
            if (key == SettingsKey.KeyBindings)
            {
                var keyBindings = Settings.Get().KeyBindings;
                DebugPanelKey = DebugPanelKeyFor(keyBindings);
                ReadyKey = ReadyKeyFor(keyBindings);
                if (Loop != null && Loop.Sampler != null) Loop.Sampler.ConfirmKey = ReadyKey;
            }
            else if (key == SettingsKey.Sensitivity)
            {
                if (Loop != null && Loop.Sampler != null) Loop.Sampler.SetSensitivity(Settings.Get().Sensitivity);
            }
        }

        // 键位表 → KeyCode。表里只有这 15 个名字（C13 §5）；没见过的名字一律 KeyCode.None，
        // 由调用方退回默认表里的那一档，而不是在这里写死一个键。
        public static KeyCode KeyOf(string name)
        {
            switch (name)
            {
                case "W": return KeyCode.W;
                case "S": return KeyCode.S;
                case "A": return KeyCode.A;
                case "D": return KeyCode.D;
                case "LeftShift": return KeyCode.LeftShift;
                case "Space": return KeyCode.Space;
                case "Mouse0": return KeyCode.Mouse0;
                case "R": return KeyCode.R;
                case "E": return KeyCode.E;
                case "F": return KeyCode.F;
                case "Q": return KeyCode.Q;
                case "Return": return KeyCode.Return;
                case "O": return KeyCode.O;
                case "F3": return KeyCode.F3;
                default: return KeyCode.None;
            }
        }

        // 键位表第 action 条 → KeyCode。表项缺失/无法识别时退回**默认表的同一条**（不是退回字面量）：
        // 改默认表就改热键，代码里没有第二份。
        public static KeyCode KeyBindingFor(string[] keyBindings, int action)
        {
            if (action < 0 || action >= SettingsDefaults.KeyBindings.Length) action = SettingsDefaults.ActionReady;
            var name = keyBindings != null && action < keyBindings.Length ? keyBindings[action] : null;
            var code = KeyOf(name);
            return code != KeyCode.None ? code : KeyOf(SettingsDefaults.KeyBindings[action]);
        }

        public static KeyCode DebugPanelKeyFor(string[] keyBindings)
        {
            return KeyBindingFor(keyBindings, SettingsDefaults.ActionDebugPanel);
        }

        public static KeyCode ReadyKeyFor(string[] keyBindings)
        {
            return KeyBindingFor(keyBindings, SettingsDefaults.ActionReady);
        }

        // "host:port"。缺 host / 缺 port / 端口非法一律判失败（宁可离线，也不要连到一个没写的地址）。
        public static bool TryParseServer(string text, out string host, out int port)
        {
            host = null;
            port = 0;
            if (string.IsNullOrEmpty(text)) return false;
            var trimmed = text.Trim();
            var colon = trimmed.LastIndexOf(':');
            if (colon <= 0 || colon == trimmed.Length - 1) return false;
            int parsed;
            if (!int.TryParse(trimmed.Substring(colon + 1), NumberStyles.Integer, CultureInfo.InvariantCulture, out parsed)) return false;
            if (parsed < 1 || parsed > 65535) return false;
            host = trimmed.Substring(0, colon);
            port = parsed;
            return true;
        }

        // 编辑器与批处理（自检 / 帧基准）**一律不连**：测试路径不许真的开套接字去连服务器。
        public static bool ShouldConnect(string host, int port, bool isEditor, bool isBatchMode)
        {
            if (isEditor || isBatchMode) return false;
            return !string.IsNullOrEmpty(host) && port > 0;
        }

        private static void ResolveServer()
        {
            var configured = ArgumentValue(ServerArgument);
            if (string.IsNullOrEmpty(configured)) configured = Environment.GetEnvironmentVariable(ServerEnvironment);
            if (string.IsNullOrEmpty(configured))
            {
                ServerHost = DefaultServerHost;
                ServerPort = DefaultServerPort;
                return;
            }
            string host;
            int port;
            if (TryParseServer(configured, out host, out port))
            {
                ServerHost = host;
                ServerPort = port;
                return;
            }
            Debug.LogWarning("Ac.Boot: 服务器地址 '" + configured + "' 无法解析（期望 host:port），本进程保持离线");
            ServerHost = null;
            ServerPort = 0;
        }

        private static void AttachTransport(GameLoop loop)
        {
            if (loop == null || loop.Transport != null) return;
            ResolveServer();
            if (!ShouldConnect(ServerHost, ServerPort, Application.isEditor, Application.isBatchMode))
            {
                // 不连不是失败：单机 / 编辑器 / 自检就是这条路径，Transport 保持 null（离线语义）。
                return;
            }
            try
            {
                var transport = new UdpTransport(new UdpTransport.RealUdpSocket());
                if (!transport.Connect(ServerHost, ServerPort))
                {
                    Debug.LogWarning("Ac.Boot: 传输层未绑定本地端口，保持离线");
                    return;
                }
                // 收包路径只有这一条：会话层（HelloAck/KeepAlive/Disconnect）在 UdpTransport 内部消化，
                // 应用包（快照/事件/match state）交给帧回路的既有入口 OnPacket。
                transport.ApplicationPacket += loop.OnPacket;
                loop.Transport = transport;
            }
            catch (Exception ex)
            {
                Debug.LogWarning("Ac.Boot: 传输启动失败（" + ServerHost + ":" + ServerPort + "），保持离线: " + ex.Message);
            }
        }

        private static string ArgumentValue(string name)
        {
            var args = Environment.GetCommandLineArgs();
            for (var i = 0; i < args.Length - 1; i++)
            {
                if (args[i] == name) return args[i + 1];
            }
            return null;
        }

        // 编辑器里反复 Play 时子场景加载完成后兜底一次（域重载被关掉时静态字段会残留）。
        [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.AfterSceneLoad)]
        private static void EnsureStarted()
        {
            // 残留的呈现层指向的是上一局已经销毁的 Unity 对象（Root 是 Unity 的"假 null"），必须重造
            if (Presentation != null && Presentation.Root == null)
            {
                Presentation.Dispose();
                Presentation = null;
            }
            if (Loop == null || Presentation == null) Start();
        }
    }

    internal sealed class MixerSink : IFrameStageSink
    {
        private readonly Ac.Audio.Mixer _mixer;
        internal MixerSink(Ac.Audio.Mixer mixer) { _mixer = mixer; }
        public void Tick(double dtMs) { _mixer.Tick((float)dtMs); }
    }

    // 唯一的 MonoBehaviour：只做"每帧把 dt 交给帧回路"这一件事，逻辑全在 GameLoop 里（可无头测试）。
    public sealed class GameLoopDriver : MonoBehaviour
    {
        private void Update()
        {
            var loop = GameBootstrap.Loop;
            if (loop == null) return;
            // 调试面板（C13 §5）：热键取自键位表第 ActionDebugPanel 条（默认 "F3"），不再硬编码；
            // 面板默认关着，不要每帧刷屏。
            var presentation = GameBootstrap.Presentation;
            if (presentation != null)
            {
                if (Input.GetKeyDown(GameBootstrap.DebugPanelKey)) presentation.ToggleDebugPanel();
                // 大厅昵称（C12 §4）：唯一的输入源是 Input.inputString。只在大厅相位读它 ——
                // 对局里的按键属于 InputSampler，且这样非大厅相位连这个属性都不碰。
                if (presentation.Flow.LobbyVisible) presentation.Flow.CaptureName(Input.inputString);
            }
            // C05 §5.6：点画面锁定指针（锁定期间才计鼠标增量），Escape 解锁；焦点变化只在**跳变**那一帧
            // 清理意图（每帧都调会在未聚焦时反复塞零意图命令，把 30Hz 上行塞满噪声）。
            var sampler = loop.Sampler;
            if (sampler != null)
            {
                if (Input.GetKeyDown(KeyCode.Escape))
                {
                    sampler.SetPointerLocked(false);
                    Cursor.lockState = CursorLockMode.None;
                    Cursor.visible = true;
                }
                else if (Input.GetMouseButtonDown(0) && !sampler.PointerLocked)
                {
                    sampler.SetPointerLocked(true);
                    Cursor.lockState = CursorLockMode.Locked;
                    Cursor.visible = false;
                }
                if (Application.isFocused != sampler.Focused) sampler.OnFocusChanged(Application.isFocused);
            }
            loop.Frame(Time.unscaledDeltaTime * 1000.0);
        }
    }
}
