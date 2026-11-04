using Ac.Core;
using Ac.Boot;
using Ac.Sim;
using Ac.UI;
using Ac.View;
namespace Ac.Tests
{
    internal static class ExpeditionPerformanceSuite
    {
        public static void Register()
        {
            SelfTest.Add("p1.performance_missing_is_unverified", Missing);
            SelfTest.Add("p1.performance_loop_allocation_exact_zero_only", LoopAllocationExactZeroOnly);
            SelfTest.Add("p1.performance_loop_diagnostics_bracket_frame", LoopDiagnosticsBracketFrame);
        }

        private static void LoopAllocationExactZeroOnly()
        {
            var unsampled = new ExpeditionLoopAllocationSamples();
            SelfTest.True(unsampled.Status == "UNVERIFIED", "no exact loop allocation sample is unverified", unsampled.Status);
            var missing = new ExpeditionLoopAllocationSamples();
            for (var i = 0; i < FrameBudget.WarmupFrames + FrameBudget.SampleFrames; i++)
                missing.Add(-1, "GC Allocated In Frame unavailable in release");
            SelfTest.True(missing.Status == "UNMEASURED" && missing.Reason == "GC Allocated In Frame unavailable in release",
                "missing exact counter is unmeasured instead of zero bytes", missing.Status);
            var pass = new ExpeditionLoopAllocationSamples();
            for (var i = 0; i < FrameBudget.WarmupFrames + FrameBudget.SampleFrames; i++) pass.Add(0, null);
            SelfTest.True(pass.Status == "PASS" && pass.Measured == FrameBudget.SampleFrames
                    && pass.BytesPerFrame == 0 && pass.MaxBytes == 0 && pass.NonZero == 0,
                "600 exact zero frames pass the original zero budget", pass.Status);
            pass.Add(1, null);
            SelfTest.True(pass.Status == "FAIL" && pass.NonZero == 1 && pass.MaxBytes == 1 && pass.TotalBytes == 1,
                "one allocated byte fails the zero budget", pass.Status);
            var shortRun = new ExpeditionLoopAllocationSamples();
            for (var i = 0; i < FrameBudget.WarmupFrames + FrameBudget.SampleFrames - 1; i++) shortRun.Add(0, null);
            SelfTest.True(shortRun.Status == "UNVERIFIED" && shortRun.Measured == FrameBudget.SampleFrames - 1,
                "insufficient exact frames cannot pass", shortRun.Status);
        }

        private static void LoopDiagnosticsBracketFrame()
        {
            var loop = new GameLoop(new SnapshotView(), new EntityViews(), new Hud(), new FrameProfiler());
            var begin = 0;
            var end = 0;
            loop.DiagnosticFrameBegin = () => begin++;
            loop.DiagnosticFrameEnd = () => end++;
            loop.Frame(16);
            SelfTest.True(begin == 1 && end == 1,
                "loop diagnostics bracket exactly one production frame", begin + "/" + end);
            loop.Frame(16);
            SelfTest.True(begin == 2 && end == 2, "loop diagnostics keep firing per frame", begin + "/" + end);
        }

        private static void Missing()
        {
            var reward = new ExpeditionPerformanceSamples(0, 1);
            for (var i = 1; i <= 2161; i++) reward.Add(i, 16, 20, 100, 1, 1, 0, 10);
            SelfTest.True(reward.Status == "PASS", "reward node0 completes its own windows", reward.Status);
            reward = new ExpeditionPerformanceSamples(0, 1);
            for (var i = 1; i <= 4000; i++) reward.Add(i, 16, 20, 100, 1, 1, 1, (uint)(i / 500));
            SelfTest.True(reward.Status == "UNVERIFIED", "partial windows cannot cross offers", reward.Status);
            reward = new ExpeditionPerformanceSamples(0, 1);
            for (var i = 1; i <= 2163; i++) reward.Add(i, 16, 20, 100, 1, 1, (byte)((i - 1) / 721), (uint)((i - 1) / 721));
            SelfTest.True(reward.Status == "PASS", "completed rounds can span reward offers and nodes", reward.Status);
            var invalidStage = new ExpeditionPerformanceSamples(1, 0);
            for (var i = 1; i <= 2161; i++) invalidStage.Add(i, 16, 20, 100, 1, 2, 1, 1);
            SelfTest.True(invalidStage.Status == "UNVERIFIED", "invalid requested stage cannot pass", invalidStage.Status);
            reward = new ExpeditionPerformanceSamples(3, 1);
            for (var i = 1; i <= 200; i++) reward.Add(i, 16, 999, 999999, 1, 1, 0, 1);
            for (var i = 201; i <= 2360; i++) reward.Add(i, 16, 20, 100, 1, 1, 2, 2);
            SelfTest.True(reward.Status == "PASS" && reward.RequestedNode == 0, "new offer clears partial maxima and reward ignores node", reward.Status);
            var node3 = new ExpeditionPerformanceSamples(3);
            for (var i = 1; i <= 2161; i++) node3.Add(i, 16, 20, 100, 1, 2, 1);
            SelfTest.True(node3.Status == "UNVERIFIED", "node1 cannot populate node3 windows", node3.Status);
            for (var i = 2162; i <= 4321; i++) node3.Add(i, 16, 20, 100, 1, 2, 3);
            SelfTest.True(node3.Status == "PASS", "node3 completes three own windows", node3.Status);
            var invalidNode = new ExpeditionPerformanceSamples(2);
            for (var i = 1; i <= 2161; i++) invalidNode.Add(i, 16, 20, 100, 1, 2, 2);
            SelfTest.True(invalidNode.Status == "UNVERIFIED", "unsupported target node cannot pass", invalidNode.Status);
            var a = new ExpeditionPerformanceSamples();
            SelfTest.True(a.Status == "UNVERIFIED", "no sample is unverified", a.Status);
            for (var i = 1; i <= 2161; i++) a.Add(i, 16, -1, 20, 1, 2);
            SelfTest.True(a.Status == "UNVERIFIED", "missing graphics cannot pass", a.Status);
            a = new ExpeditionPerformanceSamples();
            for (var i = 1; i <= 4322; i += 2) a.Add(i, 16, 20, 100, 1, 2);
            SelfTest.True(a.Status == "UNVERIFIED", "missing engine frames cannot pass", a.Status);
            a = new ExpeditionPerformanceSamples();
            for (var i = 1; i <= 2161; i++) a.Add(i, 16, 20, 100, 1, 2);
            SelfTest.True(a.Status == "PASS", "three valid combat windows pass", a.Status);
            a = new ExpeditionPerformanceSamples();
            for (var i = 1; i <= 2161; i++) a.Add(i, 34, 121, 180001, 1, 2);
            SelfTest.True(a.Status == "FAIL", "original limits enforced", a.Status);
            a = new ExpeditionPerformanceSamples();
            for (var i = 1; i <= 2161; i++) a.Add(i, 16, 20, 100, 1, i < 1500 ? (byte)2 : (byte)1);
            SelfTest.True(a.Status == "UNVERIFIED", "reward cannot complete combat samples", a.Status);
            foreach (var invalid in new[] { double.NaN, double.PositiveInfinity, 0.0 })
            {
                a = new ExpeditionPerformanceSamples();
                for (var i = 1; i <= 2161; i++) a.Add(i, invalid, 20, 100, 1, 2);
                SelfTest.True(a.Status == "UNVERIFIED", "invalid wall samples cannot pass", a.Status);
            }
            a = new ExpeditionPerformanceSamples();
            for (var i = 1; i <= 200; i++) a.Add(i, 16, 999, 999999, 1, 2);
            a.Add(201, 16, 20, 100, 1, 1);
            for (var i = 202; i <= 2361; i++) a.Add(i, 16, 20, 100, 1, 2);
            SelfTest.True(a.Status == "PASS", "interrupted maxima do not leak into new combat window", a.Status);
        }
    }
}
