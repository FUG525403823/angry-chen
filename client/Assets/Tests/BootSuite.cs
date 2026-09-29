using System;
using Ac.Boot;
using Ac.Core;
using Ac.Net;
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
            SelfTest.Add("boot.client_log_wiring", ChecksClientLogWiring);
            SelfTest.Add("boot.join_reported_once_per_name", ChecksJoinReport);
            SelfTest.Add("boot.command_uplink", ChecksCommandUplink);
            SelfTest.Add("boot.snapshot_reaches_mirror", ChecksSnapshotReachesMirror);
            SelfTest.Add("boot.command_tick_from_snapshot", ChecksCommandTickFromSnapshot);
            SelfTest.Add("boot.hud_ammo_rage_from_match_state", ChecksHudAmmoRage);
        }

        // 弹药/怒气 HUD 的权威来源：**本地玩家那行 MatchState**。此前客户端没有任何地方把 MatchState 的
        // mag/reserve/rage 填进 HudSample（AmmoLedger 只有用例在用），出包 HUD 恒显示 "0 / 0"、怒气恒 0。
        private static void ChecksHudAmmoRage()
        {
            var loop = new GameLoop(new SnapshotView(), new EntityViews(), new Hud(), new FrameProfiler());
            loop.LocalPlayerId = 3;      // 身份已认领（由 MatchState 行按昵称认领，见 IdentitySuite）
            SelfTest.Equal(0, loop.Hud.Ammo.Mag);

            loop.OnPacket(MatchStateHeader(), MatchStateBytes(2, 3, 0,
                new ushort[] { 3 }, new[] { "牧羊人" }, new[] { true },
                new byte[] { 1 }, new byte[] { 7 }, new ushort[] { 90 }, new byte[] { 42 }));
            loop.Frame(1000.0 / 60.0);

            SelfTest.Equal(7, loop.Hud.Ammo.Mag);
            SelfTest.Equal(90, loop.Hud.Ammo.Reserve);
            SelfTest.Equal(30, loop.Hud.Ammo.MagSize);   // 手枪弹匣 12（WeaponTable 与服务端 kWeapons 同值）
            SelfTest.Equal(42, loop.Hud.Rage.Rage);

            // 不是本地玩家那一行就不许串到 HUD 上
            loop.OnPacket(MatchStateHeader(), MatchStateBytes(2, 3, 0,
                new ushort[] { 9 }, new[] { "b" }, new[] { true },
                new byte[] { 1 }, new byte[] { 30 }, new ushort[] { 1 }, new byte[] { 99 }));
            loop.Frame(1000.0 / 60.0);
            SelfTest.Equal(7, loop.Hud.Ammo.Mag);
            SelfTest.Equal(42, loop.Hud.Rage.Rage);
        }

        // 大厅 ready 这条产品路径的死因（ADR-013）：命令的 clientTick 必须**等于**服务端 tick
        // （`security::validateClientTick` 严格相等，S06/S11 冻结），而客户端取自最近一条**已应用**的
        // 权威快照。修复前快照全被丢弃（见 boot.snapshot_reaches_mirror）⇒ AppliedTick 恒 0，
        // 于是任何已经打过一局的服务（world tick ≠ 0）都把每一条大厅命令**整条丢掉**（Ready 位一起丢）
        // ⇒ 按回车永远 `准备 0/1`、永远开不了局。实测对照：生产实例（tick≈48590）按回车无反应；
        // 新起实例（tick=0）按回车立刻进对局。这条用例把"大厅命令带活 tick + Ready 位"钉住。
        private static void ChecksCommandTickFromSnapshot()
        {
            var now = 0.0;
            var socket = new ScriptedSocket();
            var transport = new Ac.Net.UdpTransport(socket, delegate { return now; });
            var loop = new GameLoop(new SnapshotView(), new EntityViews(), new Hud(), new FrameProfiler());
            loop.Transport = transport;
            var sampler = new InputSampler();
            sampler.SetReadyHeld(true);          // 玩家在大厅按下了准备
            loop.Sampler = sampler;

            SelfTest.True(transport.Connect("mem", 0), "Connect 成功", "Connect 返回 false");
            for (var i = 0; i < 40 && transport.State != Ac.Net.ConnectionState.Connected; i++)
            {
                now += 16.6667;
                loop.Frame(16.6667);
            }
            SelfTest.Equal((long)Ac.Net.ConnectionState.Connected, (long)transport.State);

            // 大厅的空快照：世界 tick 冻结在生产实例的量级（不是 0）
            loop.OnPacket(SnapshotHeader(), SnapshotPayload(48590u, 0u, new byte[0][], new ushort[0]));
            SelfTest.Equal(48590, loop.View.AppliedTick);

            for (var i = 0; i < 30; i++)         // 0.5s @60fps：30Hz 上行至少发几条
            {
                now += 16.6667;
                loop.Frame(16.6667);
            }
            var commands = socket.CommandPayloads();
            SelfTest.True(commands.Count >= 10, "大厅也要有 30Hz 上行", commands.Count.ToString());
            var last = commands[commands.Count - 1];
            SelfTest.Equal(48590, last.ClientTick);   // ← 不是 0（0 会被服务端判 kStaleTick 整条丢掉）
            SelfTest.True((last.Buttons & (byte)CommandButtons.Ready) != 0, "Ready 位要随命令上行",
                last.Buttons.ToString());
        }

        // 真收包缝（type=5）：快照必须**经 OnPacket** 进镜像。这条缝此前零覆盖 —— 帧基准自己造帧调
        // `ApplySnapshot`、六步联调自带解包器（JointSuite.cs:408），于是 OnPacket 里
        // `_scratchFrame = default(SnapshotFrame)` 把复用帧的数组置空、`TryToFrame` 因 null 每帧拒收
        // （只进 DecodeFailures），一路绿灯到出包：实跑 inboundBytesPerSec 19240 而 entityCount=0、
        // serverTick=0，世界里一个实体都没有（相机因此没有本地实体可跟，画面是几何体内壁）。
        private static void ChecksSnapshotReachesMirror()
        {
            var loop = new GameLoop(new SnapshotView(), new EntityViews(), new Hud(), new FrameProfiler());

            // ① 全量帧（baselineTick=0）：两条记录 —— 玩家 1 + 羊 8
            loop.OnPacket(SnapshotHeader(), SnapshotPayload(100u, 0u,
                new[]
                {
                    SnapshotRecord(1, 0, 150, 0, -300, 16384, 0, 255, 0),
                    SnapshotRecord(8, 1, 900, 0, -1200, 32768, 0, 200, 0),
                },
                new ushort[0]));
            SelfTest.Equal(0, loop.DecodeFailures);
            SelfTest.Equal(100, loop.View.AppliedTick);
            SelfTest.Equal(1, loop.View.AppliedFrames);
            FrameEntity entity;
            SelfTest.True(loop.View.TryGetEntity(1, out entity), "本地玩家实体要进镜像", "没进");
            SelfTest.Equal(150, entity.XCm);
            SelfTest.Equal(-300, entity.ZCm);
            SelfTest.Equal(16384, entity.YawUnits);
            SelfTest.Equal(255, entity.HpRatioUnits);
            SelfTest.True(loop.View.TryGetEntity(8, out entity), "羊实体要进镜像", "没进");
            SelfTest.Equal(900, entity.XCm);
            SelfTest.Equal(1, entity.KindFlags);

            // ② 差分帧（baselineTick=上一帧 tick）：更新一条 + 移除一条
            loop.OnPacket(SnapshotHeader(), SnapshotPayload(101u, 100u,
                new[] { SnapshotRecord(1, 0, 250, 0, -300, 8192, 0, 255, 0) },
                new ushort[] { 8 }));
            SelfTest.Equal(0, loop.DecodeFailures);
            SelfTest.Equal(101, loop.View.AppliedTick);
            SelfTest.Equal(2, loop.View.AppliedFrames);
            SelfTest.True(loop.View.TryGetEntity(1, out entity) && entity.XCm == 250, "差分要改到镜像上", "没改");
            SelfTest.True(!loop.View.TryGetEntity(8, out entity), "移除列表要真的删掉", "还在");

            // ③ 坏载荷：只计数、镜像一动不动、也不卡死（下一帧照收）
            loop.OnPacket(SnapshotHeader(), new byte[] { 1, 2, 3 });
            SelfTest.True(loop.DecodeFailures >= 1, "坏载荷要计进 DecodeFailures", loop.DecodeFailures.ToString());
            SelfTest.Equal(101, loop.View.AppliedTick);
            loop.OnPacket(SnapshotHeader(), SnapshotPayload(102u, 101u,
                new[] { SnapshotRecord(1, 0, 300, 0, -300, 8192, 0, 255, 0) }, new ushort[0]));
            SelfTest.Equal(102, loop.View.AppliedTick);
        }

        // type=5 的包头（unreliable，flags 由 RequiredFlags 给）。载荷字节由下面的helper按**线上顺序**拼。
        // type=10 的包头（reliable）
        private static PacketHeader MatchStateHeader()
        {
            var header = new PacketHeader();
            header.Version = PacketHeader.ProtocolVersion;
            header.Type = PacketType.MatchState;
            header.Flags = (PacketFlags)PacketHeader.RequiredFlags(PacketType.MatchState);
            return header;
        }

        private static PacketHeader SnapshotHeader()        {
            var header = new PacketHeader();
            header.Version = PacketHeader.ProtocolVersion;
            header.Type = PacketType.Snapshot;
            header.Flags = (PacketFlags)PacketHeader.RequiredFlags(PacketType.Snapshot);
            return header;
        }

        // 快照载荷（S03 §5.4 的服务端写入顺序）：
        //   tick u32 | serverTimeMs u32 | lastAckedSeq u16 | baselineTick u32 | changedCount u8 |
        //   records(changedCount × 15B) | removedCount u8 | removedIds u16 × n | eventCount u8
        // 记录 15B 的线上顺序：id u16 | kindFlags u8 | xCm i16 | yCm i16 | zCm i16 | yawUnits u16 |
        //   pitchUnits u16 | hpRatioUnits u8 | state u8。
        private static byte[] SnapshotPayload(uint tick, uint baselineTick, byte[][] records, ushort[] removed)
        {
            var size = 4 + 4 + 2 + 4 + 1 + records.Length * 15 + 1 + removed.Length * 2 + 1;
            var bytes = new byte[size];
            var offset = 0;
            WriteU32(bytes, ref offset, tick);
            WriteU32(bytes, ref offset, tick * 20u);
            WriteU16(bytes, ref offset, (ushort)(tick & 0xFFFFu));
            WriteU32(bytes, ref offset, baselineTick);
            bytes[offset++] = (byte)records.Length;
            for (var i = 0; i < records.Length; i++)
            {
                System.Array.Copy(records[i], 0, bytes, offset, 15);
                offset += 15;
            }
            bytes[offset++] = (byte)removed.Length;
            for (var i = 0; i < removed.Length; i++) WriteU16(bytes, ref offset, removed[i]);
            bytes[offset] = 0;   // eventCount
            SelfTest.Equal(size, offset + 1);
            return bytes;
        }

        private static byte[] SnapshotRecord(ushort id, byte kindFlags, short xCm, short yCm, short zCm,
            ushort yawUnits, ushort pitchUnits, byte hpRatioUnits, byte state)
        {
            var bytes = new byte[15];
            var offset = 0;
            WriteU16(bytes, ref offset, id);
            bytes[offset++] = kindFlags;
            WriteI16(bytes, ref offset, xCm);
            WriteI16(bytes, ref offset, yCm);
            WriteI16(bytes, ref offset, zCm);
            WriteU16(bytes, ref offset, yawUnits);
            WriteU16(bytes, ref offset, pitchUnits);
            bytes[offset++] = hpRatioUnits;
            bytes[offset] = state;
            return bytes;
        }

        private static void WriteU16(byte[] bytes, ref int offset, ushort value)
        {
            bytes[offset++] = (byte)(value & 0xFF);
            bytes[offset++] = (byte)((value >> 8) & 0xFF);
        }

        private static void WriteI16(byte[] bytes, ref int offset, short value)
        {
            WriteU16(bytes, ref offset, unchecked((ushort)value));
        }

        private static void WriteU32(byte[] bytes, ref int offset, uint value)
        {
            bytes[offset++] = (byte)(value & 0xFF);
            bytes[offset++] = (byte)((value >> 8) & 0xFF);
            bytes[offset++] = (byte)((value >> 16) & 0xFF);
            bytes[offset++] = (byte)((value >> 24) & 0xFF);
        }

        // MatchState 载荷（S03 §5.5 的服务端写入顺序）：phase u8 | wave u8 | intermissionMs u16 |
        // count u8 | 逐行（pid u16 | nameLen u8 | name | ready u8 | weapon u8 | hp u8 | kills u16 |
        // mag u8 | reserve u16 | reloadLeft u8 | rage u8 | rageLeft u8 | downed u8 | revive u8）。
        private static byte[] MatchStateBytes(byte phase, byte wave, ushort intermissionMs, ushort[] pids,
            string[] names, bool[] ready, byte[] weapons, byte[] mags, ushort[] reserves, byte[] rages)
        {
            var size = 5;
            for (var i = 0; i < pids.Length; i++) size += 16 + System.Text.Encoding.UTF8.GetByteCount(names[i]);
            var bytes = new byte[size];
            var offset = 0;
            bytes[offset++] = phase;
            bytes[offset++] = wave;
            WriteU16(bytes, ref offset, intermissionMs);
            bytes[offset++] = (byte)pids.Length;
            for (var i = 0; i < pids.Length; i++)
            {
                WriteU16(bytes, ref offset, pids[i]);
                var nameBytes = System.Text.Encoding.UTF8.GetBytes(names[i]);
                bytes[offset++] = (byte)nameBytes.Length;
                System.Array.Copy(nameBytes, 0, bytes, offset, nameBytes.Length);
                offset += nameBytes.Length;
                bytes[offset++] = (byte)(ready[i] ? 1 : 0);
                bytes[offset++] = weapons[i];
                bytes[offset++] = 200;                 // hpRatio
                WriteU16(bytes, ref offset, 0);        // kills
                bytes[offset++] = mags[i];
                WriteU16(bytes, ref offset, reserves[i]);
                bytes[offset++] = 0;                   // reloadLeft10Ms
                bytes[offset++] = rages[i];
                bytes[offset++] = 0;                   // rageLeft100Ms
                bytes[offset++] = 0;                   // downed
                bytes[offset++] = 0;                   // reviveRatio255
            }
            SelfTest.Equal(size, offset);
            return bytes;
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
        // C15 §5：日志落盘此前**只有用例在用**（`LogSink` 全仓没有生产调用方）—— 出包版一个文件都不落，
        // 而运维手册承诺 `logs/client-<yyyyMMdd>.log` 与 `crash-<yyyyMMdd-HHmmss>.log`。这条钉住运行期接线。
        private static void ChecksClientLogWiring()
        {
            var root = System.IO.Path.Combine(System.IO.Path.GetTempPath(),
                "ac-clientlog-" + Guid.NewGuid().ToString("N").Substring(0, 8));
            ClientLog.Detach();                       // 回到"未装配"，避免踩运行期那份 sink
            SelfTest.True(ClientLog.AttachRoot(root), "AttachRoot 要成功", "失败");
            SelfTest.True(ClientLog.Sink != null, "装配后 Sink 不许为空", "为空");

            ClientLog.Handle("hello-log", "", UnityEngine.LogType.Log);
            var path = ClientLog.Sink.CurrentPath;
            SelfTest.True(System.IO.File.Exists(path), "普通行要落到 client-<yyyyMMdd>.log", path);
            var text = System.IO.File.ReadAllText(path);
            SelfTest.True(text.Contains("\"msg\":\"hello-log\""), "行里要有 msg", text);
            SelfTest.True(text.Contains("\"level\":\"log\""), "Log 级要折成小写 log", text);
            SelfTest.True(text.Contains("\"version\":\"" + VersionInfo.VersionLine + "\""), "每行都要带版本行", text);

            ClientLog.Handle("boom-exception", "stack-line-1\nstack-line-2", UnityEngine.LogType.Exception);
            var crashes = System.IO.Directory.GetFiles(root, "crash-*.log");
            SelfTest.Equal(1, crashes.Length);
            var crash = System.IO.File.ReadAllText(crashes[0]);
            SelfTest.True(crash.StartsWith(VersionInfo.VersionLine), "crash 首行是版本行", crash.Substring(0, 12));
            SelfTest.True(crash.Contains("boom-exception") && crash.Contains("stack-line-2"), "crash 要带消息与栈", crash);
            ClientLog.Handle("boom-exception", "stack-line-1\nstack-line-2", UnityEngine.LogType.Exception);
            SelfTest.Equal(1, System.IO.Directory.GetFiles(root, "crash-*.log").Length);  // 同一段异常只写一份

            ClientLog.Detach();
            ClientLog.Handle("after-detach", "", UnityEngine.LogType.Log);
            SelfTest.True(!System.IO.File.ReadAllText(path).Contains("after-detach"), "Detach 之后不许再落盘", "还在写");
            ClientLog.Attach(UnityEngine.Application.persistentDataPath);   // 把运行期那份接回去
            try { System.IO.Directory.Delete(root, true); } catch (Exception) { }
        }

        private static void ChecksServerConfig()        {
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

            // 产品侧连接入口（ADR-016）：出包版既没有环境变量、也拿不到被播放器吃掉的 `-server`，
            // 所以①默认值必须指向**游戏面 UDP 端口**，②四条候选的优先级要钉死。
            SelfTest.True(GameBootstrap.DefaultServerPort == 8788,
                "默认端口 = 游戏面 UDP 8788（server/README §18.2 的 AC_UDP_PORT）",
                GameBootstrap.DefaultServerPort.ToString());
            SelfTest.True(GameBootstrap.DefaultServerPort != 8787,
                "默认端口不许是 HTTP 面 8787（把 UDP Hello 发到 TCP 端口永远等不到 HelloAck）",
                GameBootstrap.DefaultServerPort.ToString());
            SelfTest.True(GameBootstrap.ProductServerArgument == "-acserver" &&
                GameBootstrap.ProductServerArgument != GameBootstrap.ServerArgument,
                "产品侧开关与 Unity 播放器自用的 -server 不是同一个", GameBootstrap.ProductServerArgument);

            var choice = GameBootstrap.ChooseServer("10.0.0.5:9999", "1.1.1.1:1111", "2.2.2.2:2222\n", "3.3.3.3:3333");
            SelfTest.True(choice.IsValid && choice.Host == "10.0.0.5" && choice.Port == 9999 &&
                choice.Source == GameBootstrap.ProductServerArgument, "-acserver 压过另外三条", choice.Source);
            choice = GameBootstrap.ChooseServer(null, "1.1.1.1:1111", "2.2.2.2:2222\n", "3.3.3.3:3333");
            SelfTest.True(choice.IsValid && choice.Host == "1.1.1.1" && choice.Source == GameBootstrap.ServerEnvironment,
                "AC_SERVER 压过 server.txt 与 -server", choice.Source);
            choice = GameBootstrap.ChooseServer(null, null, "# 注释\n\n2.2.2.2:2222\n", "3.3.3.3:3333");
            SelfTest.True(choice.IsValid && choice.Host == "2.2.2.2" && choice.Source == GameBootstrap.ServerConfigFileName,
                "server.txt 压过 -server（注释与空行跳过）", choice.Source);
            choice = GameBootstrap.ChooseServer(null, null, null, "3.3.3.3:3333");
            SelfTest.True(choice.IsValid && choice.Host == "3.3.3.3" && choice.Source == GameBootstrap.ServerArgument,
                "-server 仍然认（编辑器与历史写法）", choice.Source);
            choice = GameBootstrap.ChooseServer(null, null, "  \n# 只有注释\n", null);
            SelfTest.True(choice.IsValid && choice.Host == GameBootstrap.DefaultServerHost &&
                choice.Port == GameBootstrap.DefaultServerPort && choice.Source == "default",
                "四条候选都空 → 默认值", choice.Source + ":" + choice.Port);
            choice = GameBootstrap.ChooseServer("没有端口", "1.1.1.1:1111", "2.2.2.2:2222", null);
            SelfTest.True(!choice.IsValid && choice.Host == null,
                "第一个非空候选解析失败 → 离线（不许退到环境变量或默认值）", choice.Source + "/" + (choice.Host ?? "null"));
            SelfTest.True(GameBootstrap.FirstConfigLine("\uFEFF10.0.0.5:9999\n") == "10.0.0.5:9999",
                "server.txt 的 UTF-8 BOM 要被吃掉", GameBootstrap.FirstConfigLine("\uFEFF10.0.0.5:9999\n"));
            SelfTest.True(GameBootstrap.FirstConfigLine("  # 注释\r\n\r\n 7.7.7.7:8788 \r\n") == "7.7.7.7:8788",
                "CRLF + 注释 + 两侧空白", GameBootstrap.FirstConfigLine("  # 注释\r\n\r\n 7.7.7.7:8788 \r\n"));
            SelfTest.True(GameBootstrap.FirstConfigLine("# 只有注释\n\n") == null, "只有注释与空行 = 没配",
                GameBootstrap.FirstConfigLine("# 只有注释\n\n") ?? "null");
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
