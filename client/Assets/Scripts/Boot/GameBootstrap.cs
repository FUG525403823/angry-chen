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

        // 服务器地址的**产品侧解析顺序**（ADR-016）：`-acserver host:port` → 环境变量 `AC_SERVER` →
        // **exe 同级的 `server.txt`** → `-server host:port`（编辑器/历史写法）→ 默认值。
        // 为什么另起一个开关而不是修 `-server`：Unity 播放器自己解析 `-server`（headless server 开关）
        // 并在参数不合法时中止进程，出包版里 GameBootstrap 根本收不到那个值（ADR-014 §后果-2），
        // 而 `-acserver` 不是任何播放器开关，原样落到 `Environment.GetCommandLineArgs()`。
        public const string DefaultServerHost = "127.0.0.1";
        // 8788 = **游戏面 UDP 端口**（server/README.md §18.2 的 `AC_UDP_PORT`）。这里曾经写 8787 —— 那是
        // HTTP 诊断面（TCP）：客户端拿着默认值去发 UDP `Hello`，服务端根本没有那个 UDP 端口，永远收不到
        // `HelloAck`（出包版"连不上"的一个默认值级成因）；联调之所以没暴露，是因为脚本每次都显式传
        // `AC_JOINT_UDP=127.0.0.1:8788`。
        public const int DefaultServerPort = 8788;
        public const string ServerArgument = "-server";
        // 产品侧开关（出包版唯一可用的命令行入口）。
        public const string ProductServerArgument = "-acserver";
        public const string ServerEnvironment = "AC_SERVER";
        // 产品侧配置文件：与可执行文件同级，内容是第一行有效的 `host:port`（空行与 `#` 注释跳过）。
        // 分发形态就是"解压 → 改这一行 → 双击 exe"，不需要环境变量、也不需要会写命令行。
        public const string ServerConfigFileName = "server.txt";

        // 默认昵称：**不输名字也必须能玩**。本地身份（过渡方案，见 Ac.Net.LocalIdentity）是按昵称严格相等
        // 去 MatchState 玩家表里认领一行的，而 Lobby.Name 的初值是空串 ⇒ 空名字永远认领不到 ⇒
        // LocalPlayerId 恒为 0，于是 ①相机停在装配原点（不是人头，画面诡异）②键鼠采到的意图不驱动任何实体
        // ③大厅昵称非法（准备位那条开局路径也堵死）。玩家在大厅里键入的名字会覆盖它（SetName 走 OnNameChanged）。
        public const string DefaultLocalName = "牧羊人";

        public static GameLoop Loop { get; private set; }
        // B1：呈现层（相机/场地/羊群/特效/屏幕流/调试面板）。此前它整个不存在，所以画面是空的。
        public static PresentationLayer Presentation { get; private set; }
        public static SettingsStore Settings { get; private set; }
        // 解析后的连接目标（ResolveServer 的结果）。null host = 本进程不连接。
        public static string ServerHost { get; private set; }
        public static int ServerPort { get; private set; }
        // 这个目标是从哪条候选读出来的（`-acserver` / `AC_SERVER` / `server.txt` / `-server` / `default`）。
        // 排障时"我改了 server.txt 怎么没生效"这类问题只看这一条。
        public static string ServerSource { get; private set; }
        // 调试面板热键：从 SettingsDefaults.KeyBindings[ActionDebugPanel] 的表里解析，不在代码里写 F3。
        public static KeyCode DebugPanelKey { get; private set; }
        // 大厅准备键（ADR-013）：同样从键位表（[ActionReady]，默认表那条是 Return）解析，
        // 采样器里不写死键 —— 设置面板改了它就跟着改。
        public static KeyCode ReadyKey { get; private set; }
        // 局内聊天键（键位表 [ActionChat]，默认表那条是 Return；与 ReadyKey 同键是刻意的，相位互斥）。
        // 此前这条绑定没有任何消费方（Ac.UI.Chat 只有规则、没有输入通路）——v2 收尾把它接上。
        public static KeyCode ChatKey { get; private set; }
        public static KeyCode SettingsKeyCode { get; private set; }

        private static bool _settingsSubscribed;

        // 角度表（C02 §5.6 / C15）：**出包 player 里没有仓库根**，`TrigTable.Shared` 的仓库路径必然抛
        // `TrigTableException`（实测：player 里每帧一条，预测步进整条废掉）。所以装配第一件事就是把
        // `Assets/Resources/trig-table.json`（资产名 `trig-table`，与 `docs/evidence/fixtures/` 逐字节相同）
        // 装进 `Ac.Sim.TrigTable`。装不上只记一条 error，让 `Shared` 的仓库路径兜底（编辑器/自检）。
        public static void InstallTrigTable()
        {
            if (TrigTable.IsInstalled) return;
            var asset = Resources.Load<TextAsset>(TrigTable.ResourceName);
            if (asset == null)
            {
                Debug.LogError("Ac.Boot: 角度表资产缺失（Resources/" + TrigTable.ResourceName +
                    "），预测步进会退化到仓库路径");
                return;
            }
            TrigTable.Install(asset.text);
        }

        [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.BeforeSceneLoad)]
        public static void Start()
        {
            // 日志落盘（C15 §5）：放在最前面 —— 后面每一步的失败都要留下结构化证据。
            ClientLog.Attach(Application.persistentDataPath);
            InstallTrigTable();
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
            ChatKey = ChatKeyFor(settings.KeyBindings);
            SettingsKeyCode = KeyBindingFor(settings.KeyBindings, 12);
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
            Presentation.BindSettings(Settings);
            // 昵称兜底（见 DefaultLocalName）：必须在 Attach 之后 —— Attach 会把大厅昵称灌进帧回路，
            // 这里把"空名字"补成一个合法默认值；玩家一在大厅键入，SetName 就把它换掉并重发 kJoin。
            if (string.IsNullOrEmpty(Presentation.Flow.Lobby.Name)) Presentation.Flow.Lobby.SetName(DefaultLocalName);
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
            var settings = Settings.Get();
            DebugPanelKey = DebugPanelKeyFor(settings.KeyBindings);
            ReadyKey = ReadyKeyFor(settings.KeyBindings);
            ChatKey = ChatKeyFor(settings.KeyBindings);
            SettingsKeyCode = KeyBindingFor(settings.KeyBindings, 12);
            if (Loop != null && Loop.Sampler != null)
            {
                Loop.Sampler.ConfirmKey = ReadyKey;
                Loop.Sampler.SetSensitivity(settings.Sensitivity);
            }
            if (Presentation != null) Presentation.ApplySettings(settings);
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

        public static KeyCode ChatKeyFor(string[] keyBindings)
        {
            return KeyBindingFor(keyBindings, SettingsDefaults.ActionChat);
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

        // 产品侧的连接来源（ADR-016）。纯函数：把四个候选值折成"连哪儿、凭哪一条"，好让无头用例
        // 逐条钉住优先级 —— 命令行/环境变量/文件这三条读法本身在 ResolveServer 里做（依赖 Application）。
        // 语义两条：① 第一个**非空**的候选赢；② 它若解析不了就**离线**（fail-closed），不许悄悄退到
        // 默认值去连一个没人写过的地址（`boot.server_config` 早就钉住了"不许悄悄连默认端口"）。
        public static ServerChoice ChooseServer(string productArg, string environment, string configText, string legacyArg)
        {
            var candidates = new[]
            {
                new ServerCandidate(ProductServerArgument, productArg),
                new ServerCandidate(ServerEnvironment, environment),
                new ServerCandidate(ServerConfigFileName, FirstConfigLine(configText)),
                new ServerCandidate(ServerArgument, legacyArg),
            };
            for (var i = 0; i < candidates.Length; i++)
            {
                if (string.IsNullOrEmpty(candidates[i].Value)) continue;
                string host;
                int port;
                if (!TryParseServer(candidates[i].Value, out host, out port))
                {
                    return new ServerChoice { Source = candidates[i].Source, IsValid = false };
                }
                return new ServerChoice { Host = host, Port = port, Source = candidates[i].Source, IsValid = true };
            }
            return new ServerChoice
            {
                Host = DefaultServerHost,
                Port = DefaultServerPort,
                Source = "default",
                IsValid = true,
            };
        }

        // `server.txt` 的第一行有效内容：空行与 `#` 注释跳过；开头的 UTF-8 BOM 也吃掉
        //（Windows 记事本"另存为 UTF-8"会写 BOM，不剥的话第一行永远解析失败）。
        public static string FirstConfigLine(string text)
        {
            if (string.IsNullOrEmpty(text)) return null;
            if (text[0] == '\uFEFF') text = text.Substring(1);
            var lines = text.Replace("\r\n", "\n").Replace('\r', '\n').Split('\n');
            for (var i = 0; i < lines.Length; i++)
            {
                var line = lines[i].Trim();
                if (line.Length == 0 || line[0] == '#') continue;
                return line;
            }
            return null;
        }

        // 产品侧连接来源的解析结果。`Source` 只用于日志与排障（"到底哪条配置生效了"）。
        public struct ServerChoice
        {
            public string Host;
            public int Port;
            public string Source;
            public bool IsValid;
        }

        private struct ServerCandidate
        {
            public readonly string Source;
            public readonly string Value;

            public ServerCandidate(string source, string value)
            {
                Source = source;
                Value = value;
            }
        }

        // 编辑器与批处理（自检 / 帧基准）**一律不连**：测试路径不许真的开套接字去连服务器。
        public static bool ShouldConnect(string host, int port, bool isEditor, bool isBatchMode)
        {
            if (isEditor || isBatchMode) return false;
            return !string.IsNullOrEmpty(host) && port > 0;
        }

        private static void ResolveServer()
        {
            var choice = ChooseServer(
                ArgumentValue(ProductServerArgument),
                Environment.GetEnvironmentVariable(ServerEnvironment),
                ReadServerConfigFile(),
                ArgumentValue(ServerArgument));
            if (!choice.IsValid)
            {
                Debug.LogWarning("Ac.Boot: 服务器地址来自 " + choice.Source + " 但无法解析（期望 host:port），本进程保持离线");
                ServerHost = null;
                ServerPort = 0;
                ServerSource = choice.Source;
                return;
            }
            ServerHost = choice.Host;
            ServerPort = choice.Port;
            ServerSource = choice.Source;
        }

        private static string ReadServerConfigFile()
        {
            try
            {
                var root = ServerConfigRoot();
                if (root == null) return null;
                var path = System.IO.Path.Combine(root, ServerConfigFileName);
                return System.IO.File.Exists(path) ? System.IO.File.ReadAllText(path) : null;
            }
            catch (Exception)
            {
                // 读不到/读坏了就是"没配"：产品侧不许因为一个坏配置文件起不来（退回下一条候选/默认值）。
                return null;
            }
        }

        // "exe 同级"：出包版的 `Application.dataPath` 是 `<app>/angry-chen_Data`，父目录就是解压目录；
        // 编辑器里是 `<project>/Assets`，父目录是工程根（编辑器不连服务器，这里只是同一套解析）。
        private static string ServerConfigRoot()
        {
            var data = Application.dataPath;
            if (string.IsNullOrEmpty(data)) return null;
            var dir = new System.IO.DirectoryInfo(data).Parent;
            return dir == null ? null : dir.FullName;
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
                Debug.Log("Ac.Boot: 连接 " + ServerHost + ":" + ServerPort + "（来源 " + ServerSource + "）");
                var transport = new UdpTransport(new UdpTransport.RealUdpSocket());
                if (!transport.Connect(ServerHost, ServerPort))
                {
                    Debug.LogWarning("Ac.Boot: 传输层未绑定本地端口，保持离线");
                    return;
                }
                // 收包路径只有这一条：会话层（HelloAck/KeepAlive/Disconnect）在 UdpTransport 内部消化，
                // 应用包（快照/事件/match state）交给帧回路的既有入口 OnPacket。
                transport.ApplicationPacket += loop.OnPacket;
                // 断线自动重连（产品路径）：没有它，任何一次会话释放都会把客户端永久钉在僵死大厅。
                transport.AutoReconnect = true;
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
        // 玩家按 ESC 主动解锁指针后，不要在同一场对局里自动锁回去（进对局/点击画面会复位它）。
        private bool _pointerUnlockRequested;
        private float _nextSettingsSaveTime;

        // 本帧是否按下了"聊天"键（键位表 [ActionChat] 解析出来的 KeyCode，设置面板改了它跟着改）。
        // 表的默认值是 Return —— 与大厅准备键同键，两者相位互斥（ADR-013 / ActionChat 的注释）。
        private static bool ChatKeyDown()
        {
            var key = GameBootstrap.ChatKey;
            return key != KeyCode.None && Input.GetKeyDown(key);
        }

        private void OnApplicationQuit()
        {
            var settings = GameBootstrap.Settings;
            if (settings != null && !settings.ReadOnlyFile) settings.FlushIfDirty(Application.persistentDataPath, true);
        }

        private void Update()
        {
            var loop = GameBootstrap.Loop;
            if (loop == null) return;
            // 调试面板（C13 §5）：热键取自键位表第 ActionDebugPanel 条（默认 "F3"），不再硬编码；
            // 面板默认关着，不要每帧刷屏。
            var presentation = GameBootstrap.Presentation;
            var chat = presentation == null ? null : presentation.Flow.Chat;
            var settings = GameBootstrap.Settings;
            if (settings != null) settings.BeginFrame();
            if (presentation != null)
            {
                presentation.UpdateSettingsRelease(Input.GetMouseButton(0));
                var settingsKey = GameBootstrap.SettingsKeyCode;
                if (presentation.SettingsPanel != null && !presentation.SettingsPanel.Visible && Cursor.lockState != CursorLockMode.Locked
                    && Input.GetMouseButtonDown(0) && OverlayRenderer.SettingsButtonContains(Input.mousePosition, Screen.width, Screen.height))
                    presentation.ToggleSettings();
                if (Input.GetKeyDown(settingsKey) && (chat == null || !chat.Focused)) presentation.ToggleSettings();
                else if (presentation.SettingsPanel != null && presentation.SettingsPanel.Visible && Input.GetKeyDown(KeyCode.Escape)) presentation.ToggleSettings();
                if (!presentation.SettingsInputBlocked)
                {
                    if (Input.GetKeyDown(GameBootstrap.DebugPanelKey)) presentation.ToggleDebugPanel();
                    if (presentation.Flow.LobbyVisible) presentation.Flow.CaptureName(Input.inputString);
                }
                chat.SetVisible(loop.ChatVisible);
                chat.ApplyInputFrame(presentation.SettingsInputBlocked ? string.Empty : Input.inputString,
                    !presentation.SettingsInputBlocked && Input.GetKeyDown(KeyCode.Escape),
                    !presentation.SettingsInputBlocked && ChatKeyDown(), Time.unscaledDeltaTime * 1000.0);
            }
            // C05 §5.6：点画面锁定指针（锁定期间才计鼠标增量），Escape 解锁；焦点变化只在**跳变**那一帧
            // 清理意图（每帧都调会在未聚焦时反复塞零意图命令，把 30Hz 上行塞满噪声）。
            var sampler = loop.Sampler;
            if (sampler != null)
            {
                var typing = (chat != null && chat.Focused) || (presentation != null && presentation.SettingsInputBlocked);
                if (typing)
                {
                    // 打字期间不抢指针：既解锁（锁定下鼠标增量会继续转视角），也不让"点一下画面"再锁上
                    sampler.SetPointerLocked(false);
                    Cursor.lockState = CursorLockMode.None;
                    Cursor.visible = true;
                }
                else if (Input.GetKeyDown(KeyCode.Escape))
                {
                    sampler.SetPointerLocked(false);
                    _pointerUnlockRequested = true;      // 玩家自己解锁：别下一帧又自动锁回去
                    Cursor.lockState = CursorLockMode.None;
                    Cursor.visible = true;
                }
                else if (loop.CombatVisible && Input.GetMouseButtonDown(0) && !sampler.PointerLocked && !OverlayRenderer.SettingsButtonContains(Input.mousePosition, Screen.width, Screen.height))
                {
                    sampler.SetPointerLocked(true);
                    _pointerUnlockRequested = false;
                    Cursor.lockState = CursorLockMode.Locked;
                    Cursor.visible = false;
                }
                else if (loop.CombatVisible && !sampler.PointerLocked && !_pointerUnlockRequested)
                {
                    // 进对局自动锁定指针：以前必须"先点一下画面"才锁，玩家第一反应是"视角转了不了/无法调整"
                    // （实跑反馈）。ESC 仍然能解锁，而且解锁后不会在下一帧被自动锁回去。
                    sampler.SetPointerLocked(true);
                    Cursor.lockState = CursorLockMode.Locked;
                    Cursor.visible = false;
                }
                if (!loop.CombatVisible) _pointerUnlockRequested = false;   // 出对局就复位，下次进对局重新自动锁
                if (Application.isFocused != sampler.Focused) sampler.OnFocusChanged(Application.isFocused);
                // 打字期间不能一边聊天一边开枪：意图按 0 处理，但 30Hz 上行照发零意图（服务端语义明确）
                if (typing) sampler.Suspend();
                else sampler.Resume();
            }
            loop.Frame(Time.unscaledDeltaTime * 1000.0);
            if (settings != null && !settings.ReadOnlyFile && !Input.GetMouseButton(0) && Time.unscaledTime >= _nextSettingsSaveTime)
            {
                settings.FlushIfDirty(Application.persistentDataPath);
                _nextSettingsSaveTime = Time.unscaledTime + 1f;
            }
        }
    }
}
