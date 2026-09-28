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

            // 键位表 → KeyCode：14 条默认项每条都要认得（表改了却忘了改映射，这里就红）。
            // 表就是这套键名的全集（InputSampler 的 14 个动作用的是同一套名字），没有第二个候选表。
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
            var before = GC.GetAllocatedBytesForCurrentThread();
            for (uint tick = 61; tick <= 180; tick++)
            {
                Feed(loop, ref frame, 64, tick, tick % 40 == 0);
                loop.Frame(1000.0 / 60.0);
            }
            var delta = GC.GetAllocatedBytesForCurrentThread() - before;
            // C14 §5：稳态每帧 0 B 分配。这条闸挂了就说明帧回路里有隐藏分配。
            SelfTest.Equal(0, delta);
        }
    }
}
