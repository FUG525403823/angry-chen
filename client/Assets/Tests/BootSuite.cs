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
