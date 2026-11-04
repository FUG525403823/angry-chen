using Ac.Core;
using Ac.Boot;
namespace Ac.Tests
{
    internal static class ExpeditionPerformanceSuite
    {
        public static void Register() { SelfTest.Add("p1.performance_missing_is_unverified", Missing); }
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
