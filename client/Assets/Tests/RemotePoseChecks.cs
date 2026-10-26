using System;
using System.Collections.Generic;
using Ac.Sim;
using Ac.View;

namespace Ac.Tests
{
    public static class RemotePoseChecks
    {
        public static void Register()
        {
            Ac.Core.SelfTest.Add("view.sparse_stop", SparseDeltaStopsAtLatestPose);
            Ac.Core.SelfTest.Add("view.clock_monotonic", SnapshotArrivalDoesNotRewindClock);
            Ac.Core.SelfTest.Add("view.missing_baseline", MissingBaselineIsRejected);
            Ac.Core.SelfTest.Add("view.historical_baseline", DeltaUsesDeclaredHistoricalBaseline);
            Ac.Core.SelfTest.Add("view.identity_lifetime", RemovalStartsNewLifetime);
            Ac.Core.SelfTest.Add("view.remote_cadence_matrix", PlaybackRemainsContinuousAcrossCadences);
            Ac.Core.SelfTest.Add("view.remote_seeded_arrivals", SeededArrivalsAcceptOnlyRetainedBaselines);
            Ac.Core.SelfTest.Add("view.remote_full_recovery", FullSnapshotRecoversMissingBaselineChain);
            Ac.Core.SelfTest.Add("view.remote_sparse_reference", SparseSheepMatchCompleteHistory);
            Ac.Core.SelfTest.Add("view.remote_history_rollover", HistoryRolloverRetainsCompleteState);
            Ac.Core.SelfTest.Add("view.remote_state_id_reuse", StateAndReusedIdsDoNotBlendLifetimes);
            Ac.Core.SelfTest.Add("view.full_preserves_local_prediction", FullRefreshPreservesLocalPrediction);
            Ac.Core.SelfTest.Add("view.empty_session_releases_models", EmptySessionReleasesViews);
        }

        private static SnapshotFrame CreateFrame(uint tick, short x, bool include, uint baseline)
        {
            return CreateFrame(tick, baseline, include ? new[] { CreateSheep(17, x) } : new FrameEntity[0]);
        }

        private static SnapshotFrame CreateFrame(uint tick, uint baseline, params FrameEntity[] entities)
        {
            return new SnapshotFrame
            {
                Tick = tick,
                ServerTimeMs = 1000 + (tick - 1) * 50,
                BaselineTick = baseline,
                Entities = entities,
                EntityCount = entities.Length,
                RemovedIds = new ushort[0]
            };
        }

        private static FrameEntity CreateSheep(ushort id, short x, byte state = 0)
        {
            return new FrameEntity { Id = id, KindFlags = 1, XCm = x, HpRatioUnits = 255, State = state };
        }

        private static void Require(bool condition, string message)
        {
            if (!condition) throw new Exception(message);
        }

        public static void SparseDeltaStopsAtLatestPose()
        {
            var snapshots = new SnapshotView();
            var views = new EntityViews();
            var clock = new Interpolation.RenderClock();
            snapshots.ApplyFrame(CreateFrame(1, 0, true, 0));
            clock.OnSnapshot(1000, 0);
            views.SyncFrame(snapshots, clock, 0);
            clock.Advance(50);
            snapshots.ApplyFrame(CreateFrame(2, 30, true, 1));
            clock.OnSnapshot(1050, 50);
            clock.Advance(50);
            snapshots.ApplyFrame(CreateFrame(3, 30, false, 2));
            clock.OnSnapshot(1100, 100);
            clock.Advance(50);
            snapshots.ApplyFrame(CreateFrame(4, 30, false, 3));
            clock.OnSnapshot(1150, 150);
            clock.Advance(25);
            views.SyncFrame(snapshots, clock, 25);
            EntityView sheep;
            Require(views.TryGet(17, out sheep), "missing sheep");
            Require(Math.Abs(sheep.X - 0.3) < 1e-9, "sparse stop expected x=0.3 actual=" + sheep.X);
        }

        public static void SnapshotArrivalDoesNotRewindClock()
        {
            var clock = new Interpolation.RenderClock();
            clock.OnSnapshot(1000, 0);
            clock.Advance(75);
            double previous = clock.RenderTimeMs;
            clock.OnSnapshot(1050, 75);
            Require(clock.RenderTimeMs >= previous, "arrival moved render clock backwards");
        }

        public static void MissingBaselineIsRejected()
        {
            var snapshots = new SnapshotView();
            Require(snapshots.ApplyFrame(CreateFrame(1, 0, true, 0)), "seed rejected");
            Require(snapshots.ApplyFrame(CreateFrame(3, 30, true, 1)), "valid base rejected");
            Require(!snapshots.ApplyFrame(CreateFrame(4, 60, true, 2)), "missing baseline accepted");
        }

        public static void DeltaUsesDeclaredHistoricalBaseline()
        {
            var snapshots = new SnapshotView();
            snapshots.ApplyFrame(CreateFrame(1, 10, true, 0));
            snapshots.ApplyFrame(CreateFrame(2, 20, true, 1));
            Require(snapshots.ApplyFrame(CreateFrame(3, 0, false, 1)), "historical baseline rejected");
            FrameEntity entity;
            Require(snapshots.TryGetEntity(17, out entity) && entity.XCm == 10,
                "delta merged into latest instead of declared base");
            Require(!snapshots.TryGetEntityInFrame(0, 17, out entity), "raw delta contract changed");
            uint lifetime;
            Require(snapshots.TryGetRenderEntity(0, 17, out entity, out lifetime) && entity.XCm == 10,
                "complete render state missing");
        }

        public static void RemovalStartsNewLifetime()
        {
            var snapshots = new SnapshotView();
            snapshots.ApplyFrame(CreateFrame(1, 10, true, 0));
            FrameEntity entity;
            uint first, second;
            snapshots.TryGetRenderEntity(0, 17, out entity, out first);
            var removed = CreateFrame(2, 0, false, 1);
            removed.RemovedIds = new ushort[] { 17 };
            removed.RemovedCount = 1;
            snapshots.ApplyFrame(removed);
            snapshots.ApplyFrame(CreateFrame(3, 300, true, 2));
            snapshots.TryGetRenderEntity(0, 17, out entity, out second);
            Require(first != second, "reentry reused lifetime");
            snapshots.ResetForNewSession();
            Require(!snapshots.TryGetRenderEntity(0, 17, out entity, out second), "old session history retained");
        }

        public static void PlaybackRemainsContinuousAcrossCadences()
        {
            foreach (int fps in new[] { 30, 60, 144 })
            foreach (int hz in new[] { 10, 15, 20 })
            {
                var snapshots = new SnapshotView();
                var views = new EntityViews();
                var clock = new Interpolation.RenderClock();
                double dt = 1000.0 / fps, previous = 0;
                int next = 0;
                uint tick = 0;
                for (int frame = 0; frame < fps * 3; frame++)
                {
                    double now = frame * dt;
                    while (next <= now + 1e-7)
                    {
                        tick++;
                        var snapshot = CreateFrame(tick, (short)(next * 0.3), true, tick == 1 ? 0 : tick - 1);
                        snapshot.ServerTimeMs = (uint)(1000 + next);
                        Require(snapshots.ApplyFrame(snapshot), "cadence frame rejected");
                        clock.OnSnapshot(snapshot.ServerTimeMs, now);
                        next = (int)Math.Round(tick * 1000.0 / hz);
                    }
                    clock.Advance(dt);
                    views.SyncFrame(snapshots, clock, dt);
                    EntityView sheep;
                    Require(views.TryGet(17, out sheep), "cadence sheep missing");
                    if (frame > fps)
                        Require(sheep.X >= previous - 1e-8 && sheep.X - previous <= 3.0 / fps * 1.12 + 0.02,
                            "cadence discontinuity fps=" + fps + " hz=" + hz);
                    previous = sheep.X;
                }
            }
        }

        private sealed class Arrival
        {
            public int AtMs;
            public SnapshotFrame Frame;
        }

        // Deterministic in-memory arrival scheduling, not a socket or real-network test.
        public static void SeededArrivalsAcceptOnlyRetainedBaselines()
        {
            const int seed = 20261026;
            var random = new Random(seed);
            var arrivals = new List<Arrival>();
            int dropped = 0;
            for (uint tick = 1; tick <= 120; tick++)
            {
                bool full = tick == 1 || tick % 12 == 0;
                if (!full && (tick == 14 || random.Next(100) < 18))
                {
                    dropped++;
                    continue;
                }
                int at = (int)(tick - 1) * 50 + (tick == 1 ? 0 : random.Next(0, 121));
                if (tick >= 30 && tick <= 33) at = 1700;
                if (tick == 20) at = 1150;
                if (tick == 21) at = 1000;
                arrivals.Add(new Arrival
                {
                    AtMs = at,
                    Frame = CreateFrame(tick, (short)(tick * 10), true, full ? 0 : tick - 1)
                });
            }
            arrivals.Sort((left, right) => left.AtMs != right.AtMs
                ? left.AtMs.CompareTo(right.AtMs) : left.Frame.Tick.CompareTo(right.Frame.Tick));

            var snapshots = new SnapshotView();
            var views = new EntityViews();
            var clock = new Interpolation.RenderClock();
            var retained = new List<uint>();
            uint applied = 0;
            int next = 0, accepted = 0, rejected = 0, reordered = 0, burst = 0;
            int mismatches = 0;
            double previousTime = 0, previousX = 0;
            for (int now = 0; now <= 6400; now += 10)
            {
                bool hadEstimate = clock.HasEstimate;
                clock.Advance(10);
                Require(clock.RenderTimeMs >= previousTime, "seeded advance rewound seed=" + seed);
                if (hadEstimate)
                    Require(clock.RenderTimeMs - previousTime >= 9 - 1e-9
                        && clock.RenderTimeMs - previousTime <= 11 + 1e-9,
                        "seeded playback exceeded rate limits seed=" + seed);
                while (next < arrivals.Count && arrivals[next].AtMs <= now)
                {
                    var arrival = arrivals[next++];
                    var frame = arrival.Frame;
                    bool newer = frame.Tick > applied;
                    bool validBase = frame.BaselineTick == 0 || retained.Contains(frame.BaselineTick);
                    bool expected = newer && validBase;
                    int beforeCount = snapshots.FrameCount;
                    int beforeApplied = snapshots.AppliedFrames;
                    uint beforeTime = snapshots.GetServerTimeMs();
                    double beforeClock = clock.RenderTimeMs;
                    Require(snapshots.ApplyFrame(frame) == expected,
                        "baseline acceptance differs tick=" + frame.Tick + " seed=" + seed);
                    if (expected)
                    {
                        applied = frame.Tick;
                        retained.Add(applied);
                        if (retained.Count > SnapshotView.HistoryFrames) retained.RemoveAt(0);
                        accepted++;
                        clock.OnSnapshot(frame.ServerTimeMs, now);
                        Require(clock.RenderTimeMs >= beforeClock, "seeded arrival rewound clock tick=" + frame.Tick);
                    }
                    else
                    {
                        rejected++;
                        if (!newer) reordered++;
                        else mismatches++;
                        Require(snapshots.FrameCount == beforeCount && snapshots.AppliedFrames == beforeApplied
                            && snapshots.GetServerTimeMs() == beforeTime, "rejection mutated history or time");
                    }
                    Require(snapshots.AppliedTick == applied && snapshots.BaselineMismatch == mismatches,
                        "acceptance bookkeeping differs");
                    if (next < arrivals.Count && arrivals[next].AtMs == arrival.AtMs) burst++;
                }
                views.SyncFrame(snapshots, clock, 10);
                EntityView sheep;
                if (views.TryGet(17, out sheep))
                {
                    Require(sheep.X >= previousX - 1e-9, "seeded pose moved backwards seed=" + seed);
                    previousX = sheep.X;
                }
                previousTime = clock.RenderTimeMs;
            }
            Require(dropped > 0 && reordered > 0 && burst > 0 && rejected > 0 && accepted > 10,
                "seed did not exercise drop/reorder/burst/accept/reject");
            Require(applied == 120, "final full snapshot did not recover");
        }

        public static void FullSnapshotRecoversMissingBaselineChain()
        {
            var snapshots = new SnapshotView();
            Require(snapshots.ApplyFrame(CreateFrame(1, 10, true, 0)), "recovery seed rejected");
            Require(!snapshots.ApplyFrame(CreateFrame(3, 30, true, 2)), "lost baseline accepted");
            Require(!snapshots.ApplyFrame(CreateFrame(4, 40, true, 3)), "rejected frame became a baseline");
            FrameEntity entity;
            Require(snapshots.AppliedTick == 1 && snapshots.TryGetEntity(17, out entity) && entity.XCm == 10,
                "missing chain changed mirror");
            Require(snapshots.ApplyFrame(CreateFrame(5, 0, CreateSheep(18, 500, 2))), "full recovery rejected");
            Require(!snapshots.TryGetEntity(17, out entity), "full recovery retained omitted entity");
            Require(snapshots.TryGetEntity(18, out entity) && entity.XCm == 500 && entity.State == 2,
                "full recovery did not replace state");
            Require(snapshots.ApplyFrame(CreateFrame(6, 5, CreateSheep(18, 600, 3))), "recovered delta rejected");
            Require(snapshots.TryGetEntity(18, out entity) && entity.XCm == 600 && entity.State == 3,
                "post-recovery delta missing");
            Require(!snapshots.ApplyFrame(CreateFrame(4, 400, true, 0)), "late full snapshot accepted");
            Require(snapshots.AppliedTick == 6 && snapshots.BaselineMismatch == 2, "recovery counters changed");
        }

        public static void SparseSheepMatchCompleteHistory()
        {
            var sparse = new SnapshotView();
            var complete = new SnapshotView();
            var sparseViews = new EntityViews();
            var completeViews = new EntityViews();
            var clock = new Interpolation.RenderClock();
            short firstX = 0, secondX = 1000;
            byte firstState = 0, secondState = 0;
            var random = new Random(7319);
            for (uint tick = 1; tick <= 40; tick++)
            {
                bool firstChanges = tick == 1 || tick % 2 == 0;
                bool secondChanges = tick == 1 || tick % 3 == 0;
                if (firstChanges) { firstX += (short)random.Next(1, 30); firstState = (byte)(tick % 4); }
                if (secondChanges) { secondX += (short)random.Next(1, 15); secondState = (byte)(tick % 3); }
                var first = CreateSheep(17, firstX, firstState);
                var second = CreateSheep(18, secondX, secondState);
                var changed = new List<FrameEntity>();
                if (firstChanges) changed.Add(first);
                if (secondChanges) changed.Add(second);
                Require(sparse.ApplyFrame(CreateFrame(tick, tick == 1 ? 0 : tick - 1, changed.ToArray())),
                    "sparse reference delta rejected");
                Require(complete.ApplyFrame(CreateFrame(tick, 0, first, second)), "complete reference rejected");
                clock.OnSnapshot(1000 + (tick - 1) * 50, (tick - 1) * 50);
                for (int sample = 0; sample < 5; sample++)
                {
                    sparseViews.SyncFrame(sparse, clock, 10);
                    completeViews.SyncFrame(complete, clock, 10);
                    foreach (ushort id in new ushort[] { 17, 18 })
                    {
                        EntityView actual, expected;
                        Require(sparseViews.TryGet(id, out actual), "sparse sheep missing");
                        Require(completeViews.TryGet(id, out expected), "reference sheep missing");
                        // Both paths use production interpolation; the oracle receives authored full states, never merged deltas.
                        Require(Math.Abs(actual.X - expected.X) < 1e-9 && actual.State == expected.State,
                            "sparse/full playback differs tick=" + tick + " id=" + id);
                        for (int age = 0; age < sparse.FrameCount; age++)
                        {
                            FrameEntity actualRecord, expectedRecord;
                            uint actualLife, expectedLife;
                            Require(sparse.TryGetRenderEntity(age, id, out actualRecord, out actualLife),
                                "sparse history missing sheep");
                            Require(complete.TryGetRenderEntity(age, id, out expectedRecord, out expectedLife),
                                "complete history missing sheep");
                            Require(actualRecord.XCm == expectedRecord.XCm && actualRecord.State == expectedRecord.State,
                                "sparse/full history differs age=" + age + " id=" + id);
                        }
                    }
                    clock.Advance(10);
                }
            }
        }

        public static void HistoryRolloverRetainsCompleteState()
        {
            var snapshots = new SnapshotView();
            Require(snapshots.ApplyFrame(CreateFrame(1, 0, CreateSheep(17, 100), CreateSheep(18, 200))),
                "rollover seed rejected");
            for (uint tick = 2; tick <= 24; tick++)
            {
                Require(snapshots.ApplyFrame(CreateFrame(tick, tick - 1)), "rollover empty delta rejected");
                Require(snapshots.FrameCount == Math.Min((int)tick, SnapshotView.HistoryFrames), "ring size wrong");
                for (int age = 0; age < snapshots.FrameCount; age++)
                {
                    SnapshotFrame frame;
                    Require(snapshots.TryGetFrame(age, out frame) && frame.Tick == tick - (uint)age,
                        "ring age/tick order wrong");
                    foreach (ushort id in new ushort[] { 17, 18 })
                    {
                        FrameEntity entity;
                        uint lifetime;
                        Require(snapshots.TryGetRenderEntity(age, id, out entity, out lifetime)
                            && entity.XCm == (id == 17 ? 100 : 200), "rollover lost inherited entity");
                    }
                }
            }
            Require(!snapshots.ApplyFrame(CreateFrame(25, 18)), "evicted baseline accepted");
            Require(snapshots.ApplyFrame(CreateFrame(25, 19)), "oldest retained baseline rejected");
            Require(!snapshots.ApplyFrame(CreateFrame(26, 19)), "overwritten baseline accepted");
            Require(!snapshots.ApplyFrame(CreateFrame(26, 27)), "future baseline accepted");
            Require(!snapshots.ApplyFrame(CreateFrame(26, 26)), "self baseline accepted");
            Require(snapshots.ApplyFrame(CreateFrame(26, 0, CreateSheep(17, 260))), "rollover full recovery rejected");
        }

        public static void StateAndReusedIdsDoNotBlendLifetimes()
        {
            var snapshots = new SnapshotView();
            var views = new EntityViews();
            var clock = new Interpolation.RenderClock();
            Require(snapshots.ApplyFrame(CreateFrame(1, 0, CreateSheep(17, 10, 1))), "lifetime seed rejected");
            clock.OnSnapshot(1000, 0);
            views.SyncFrame(snapshots, clock, 0);
            EntityView sheep;
            Require(views.TryGet(17, out sheep), "lifetime sheep missing");
            uint firstLife = sheep.Lifetime;
            Require(snapshots.ApplyFrame(CreateFrame(2, 1, CreateSheep(17, 20, 2))), "state delta rejected");
            views.SyncFrame(snapshots, clock, 0);
            Require(sheep.State == 2 && sheep.Lifetime == firstLife, "state update changed identity or was interpolated");
            var removed = CreateFrame(3, 2);
            removed.RemovedIds = new ushort[] { 17 };
            removed.RemovedCount = 1;
            Require(snapshots.ApplyFrame(removed), "lifetime removal rejected");
            views.SyncFrame(snapshots, clock, 0);
            Require(!views.TryGet(17, out sheep), "removed sheep stayed active");
            Require(snapshots.ApplyFrame(CreateFrame(4, 3, CreateSheep(17, 3000, 3))), "ID reuse rejected");
            views.SyncFrame(snapshots, clock, 0);
            Require(views.TryGet(17, out sheep) && sheep.Lifetime != firstLife && sheep.State == 3,
                "reused ID retained old identity/state");
            Require(Math.Abs(sheep.X - 30) < 1e-9, "reused ID interpolated into old lifetime");
            clock.Advance(225);
            views.SyncFrame(snapshots, clock, 0);
            Require(Math.Abs(sheep.X - 30) < 1e-9, "removal/reentry bracket blended old lifetime");
            uint secondLife = sheep.Lifetime;
            var replacement = CreateSheep(17, 6000, 4);
            replacement.KindFlags = 2;
            Require(snapshots.ApplyFrame(CreateFrame(5, 4, replacement)), "kind replacement rejected");
            views.SyncFrame(snapshots, clock, 0);
            Require(sheep.Lifetime != secondLife && sheep.Kind == 2 && sheep.State == 4,
                "kind replacement retained lifetime/state");
            Require(Math.Abs(sheep.X - 60) < 1e-9, "kind replacement interpolated across lifetimes");
        }

        public static void FullRefreshPreservesLocalPrediction()
        {
            var snapshots = new SnapshotView();
            var views = new EntityViews();
            snapshots.SetLocalPlayer(17);
            views.SetLocalPlayer(17);
            var full = CreateFrame(1, 10, true, 0);
            full.Entities[0].KindFlags = 0;
            snapshots.ApplyFrame(full);
            views.SyncFrame(snapshots, null, 0);
            EntityView local;
            Require(views.TryGet(17, out local), "local view missing");
            var lifetime = local.Lifetime;
            local.HasPrediction = true;
            local.PredictedX = 9;
            local.Smoother.Apply(0.2, 0, 0);
            full = CreateFrame(2, 30, true, 0);
            full.Entities[0].KindFlags = 0;
            snapshots.ApplyFrame(full);
            views.SyncFrame(snapshots, null, 0);
            Require(local.Lifetime == lifetime && local.HasPrediction, "full refresh cleared local prediction");
            Require(Math.Abs(local.X - 9) < 1e-9 && Math.Abs(local.Smoother.OffsetX - 0.2) < 1e-9,
                "full refresh replaced predicted pose or smoother");
        }

        public static void EmptySessionReleasesViews()
        {
            var snapshots = new SnapshotView();
            var views = new EntityViews();
            snapshots.ApplyFrame(CreateFrame(1, 10, true, 0));
            views.SyncFrame(snapshots, null, 0);
            Require(views.ActiveCount == 1, "seed view missing");
            snapshots.ResetForNewSession();
            Require(views.SyncFrame(snapshots, null, 0) == 0 && views.ActiveCount == 0,
                "empty session retained old visible entity");
        }

        public static void Run(string name)
        {
            switch (name)
            {
                case "sparse-stop": SparseDeltaStopsAtLatestPose(); break;
                case "clock-monotonic": SnapshotArrivalDoesNotRewindClock(); break;
                case "missing-baseline": MissingBaselineIsRejected(); break;
                case "historical-baseline": DeltaUsesDeclaredHistoricalBaseline(); break;
                case "lifetime": RemovalStartsNewLifetime(); break;
                case "cadence": PlaybackRemainsContinuousAcrossCadences(); break;
                case "seeded-arrivals": SeededArrivalsAcceptOnlyRetainedBaselines(); break;
                case "full-recovery": FullSnapshotRecoversMissingBaselineChain(); break;
                case "sparse-reference": SparseSheepMatchCompleteHistory(); break;
                case "history-rollover": HistoryRolloverRetainsCompleteState(); break;
                case "state-id-reuse": StateAndReusedIdsDoNotBlendLifetimes(); break;
                default: throw new ArgumentException("Unknown remote pose check: " + name, "name");
            }
        }
    }
}
