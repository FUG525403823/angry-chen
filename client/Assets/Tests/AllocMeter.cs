using System;
using Ac.Core;
using Unity.Profiling;
using UnityEngine;

namespace Ac.Tests
{
    // C14 §5「稳态每帧 0 B 分配」的**唯一**度量实现：所有零分配用例与 FrameBench 都走这里，不许再有第二套读数。
    //
    // 为什么不用 System.GC.GetAllocatedBytesForCurrentThread()：本机（Tuanjie 2022.3.62t16 / Mono）上这个
    // per-thread 计数器恒返回 0。探针实测：测量区间里做 1000 次字符串拼接，delta 仍是 0；同期
    // GC.GetTotalMemory(false) 只按 4 KB 页粒度动（累积 100 × new byte[256] = 25600 B 时它给的还是 0），
    // 也就是说两者都看不见"每帧几百字节"这类分配——拿它们的 0 当判据的用例全都在假绿。
    //
    // 有判别力的是引擎自带的 Memory/GC Allocated In Frame 计数器（ProfilerRecorder）。同机 batchmode
    // -nographics 实测：静默窗口 8/8 读数 0；new byte[64] → 96 B、new byte[1024] → 1056 B、
    // new byte[1 << 20] → 1048608 B（含对象头，逐字节精确）；160 × new byte[64] → 15360 B、
    // 1000 次字符串拼接 → 105560 B，无采样滞后。
    //
    // 语义：该计数器按引擎帧累加、跨帧归零。所以窗口必须落在一帧之内（本仓的用例都是同步循环，实测不跨帧）；
    // 一旦发现跨帧或计数倒退，就报"不可测"，由调用方判 UNVERIFIED——绝不允许回退成 PASS。
    public static class AllocMeter
    {
        public const string MarkerName = "GC Allocated In Frame";
        public const string MarkerCategory = "Memory";

        private static ProfilerRecorder _recorder;
        private static bool _ready;
        private static string _unavailable;
        private static int _windowFrame = -1;

        public static bool Available { get { Ensure(); return _recorder.Valid; } }

        public static string UnavailableReason { get { Ensure(); return _unavailable; } }

        private static void Ensure()
        {
            if (_ready) return;
            _ready = true;
            try
            {
                _recorder = ProfilerRecorder.StartNew(ProfilerCategory.Memory, MarkerName, 1024, ProfilerRecorderOptions.SumAllSamplesInFrame);
            }
            catch (Exception error)
            {
                _unavailable = error.GetType().Name + ": " + error.Message;
                return;
            }
            if (!_recorder.Valid) { _unavailable = "no '" + MarkerCategory + "/" + MarkerName + "' counter in this process"; return; }
            WarmUp();
        }

        // 首次调用 AllocMeter 自己（JIT 编译 BytesSince/AssertZero、第一次读 Time.frameCount）本身会分配
        // 托管内存——实测 390 B，而且它记在**调用者当时的那个窗口**里（探针实测：进程里第一个空窗口
        // 读到的正是 390 B，随后的窗口都是 0）。在这里先付掉，否则第一个用它的用例会永远假红。
        // 空窗口的 delta 必然是 0；真出问题（计数器缺失/跨帧）由用例自己的窗口报，不在这里吞。
        // 预热发生在**任何测量窗口打开之前**（Ensure 在 Begin/BytesSince 的第一行），所以不会掩盖真分配。
        private static void WarmUp()
        {
            try
            {
                _windowFrame = Time.frameCount;
                string reason;
                BytesSince(_recorder.CurrentValue, out reason);
                AssertZero(_recorder.CurrentValue);
            }
            catch (Exception) { }
        }

        // 打开测量窗口。返回 -1 表示本机没有可用的分配计数器。
        public static long Begin()
        {
            Ensure();
            _windowFrame = Time.frameCount;
            return _recorder.Valid ? _recorder.CurrentValue : -1L;
        }

        // 窗口内分配的字节数。-1 = 不可测（计数器缺失、窗口跨了引擎帧、计数倒退）。
        public static long BytesSince(long start, out string reason)
        {
            Ensure();
            if (!_recorder.Valid || start < 0) { reason = _unavailable; return -1L; }
            if (Time.frameCount != _windowFrame)
            {
                reason = "engine frame boundary inside the window (" + _windowFrame + " -> " + Time.frameCount
                    + "): the per-frame counter resets there";
                return -1L;
            }
            var now = _recorder.CurrentValue;
            if (now < start) { reason = "counter went backwards inside the window (" + start + " -> " + now + ")"; return -1L; }
            reason = null;
            return now - start;
        }

        // 零分配用例的断言：窗口内 0 B 才 PASS；不可测一律 UNVERIFIED（fail-closed，绝不当"预算内"）。
        public static void AssertZero(long start)
        {
            string reason;
            var bytes = BytesSince(start, out reason);
            if (bytes < 0) throw new SelfTestFailure("expected=0B actual=UNVERIFIED(" + reason + ")");
            if (bytes != 0) throw new SelfTestFailure("expected=0B actual=" + bytes + "B in the measured window");
        }
    }
}
