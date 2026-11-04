using Ac.Boot;
using Ac.Core;
using Ac.Net;
using Ac.Sim;
using Ac.UI;
using Ac.View;
using UnityEngine;

namespace Ac.Tests
{
    internal static class ExpeditionUiSuite
    {
        public static void Register()
        {
            SelfTest.Add("expedition.ui_pending_matches_exact_receipt", PendingMatchesExactReceipt);
            SelfTest.Add("expedition.loop_wrongrun_max_does_not_poison_request", WrongRunMaxDoesNotPoisonRequest);
            SelfTest.Add("expedition.ui_rejects_previous_run_until_reset", RejectsPreviousRunUntilReset);
            SelfTest.Add("expedition.ui_authority_pending_deadline", AuthorityPendingDeadline);
            SelfTest.Add("expedition.ui_modal_zero_and_release", ModalZeroAndRelease);
            SelfTest.Add("expedition.sampler_interact_reload_fire_remains_held", InteractReloadFireRemainsHeld);
            SelfTest.Add("expedition.loop_state14_restores_authority", LoopStateRestoresAuthority);
            SelfTest.Add("expedition.acceptance_tool_phase_race_is_narrow", ToolPhaseRaceIsNarrow);
            SelfTest.Add("expedition.sampler_number_keys_select_rifle_shotgun", NumberKeysSelectRifleShotgun);
            SelfTest.Add("expedition.acceptance_refill_tracks_visible_pickup", RefillTargetTracksVisiblePickup);
            SelfTest.Add("expedition.acceptance_combat_point_stays_in_purifier", CombatPointAvoidsVisibleThreatInsidePurifier);
        }

        private static void CombatPointAvoidsVisibleThreatInsidePurifier()
        {
            var view = new SnapshotView();
            var local = new FrameEntity { Id = 1, XCm = 1200 };
            view.ApplyFrame(new SnapshotFrame { Tick = 1, Entities = new[] { local,
                new FrameEntity { Id = 2, KindFlags = 1, XCm = 1400, HpRatioUnits = 255 },
                new FrameEntity { Id = 3, KindFlags = 1, XCm = 700, HpRatioUnits = 0 },
                new FrameEntity { Id = 4, KindFlags = 3, XCm = 700 } },
                EntityCount = 4, RemovedIds = new ushort[0] });
            double x, z;
            ExpeditionAcceptance.SelectCombatPoint(view, local, out x, out z);
            var radiusSquared = (x - 12) * (x - 12) + z * z;
            var threatSquared = (x - 14) * (x - 14) + z * z;
            SelfTest.True(radiusSquared <= 25.00001 && threatSquared > 4,
                "visible living threat leads to a safer point within purifier", x + "," + z);
            ExpeditionAcceptance.SelectCombatPoint(new SnapshotView(), local, out x, out z);
            SelfTest.True(x == 12 && z == 0, "no visible threats retains nominal objective point", x + "," + z);
        }

        private static void RefillTargetTracksVisiblePickup()
        {
            var view = new SnapshotView();
            var local = new FrameEntity { Id = 1, XCm = 1050, ZCm = 979 };
            view.ApplyFrame(new SnapshotFrame { Tick = 1, Entities = new[] {
                local, new FrameEntity { Id = 5, KindFlags = 3, XCm = 871, ZCm = 836 },
                new FrameEntity { Id = 6, KindFlags = 3, XCm = -1000, ZCm = -1000 },
                new FrameEntity { Id = 7, KindFlags = 1, XCm = 1040, ZCm = 978 } },
                EntityCount = 4, RemovedIds = new ushort[0] });
            double x, z;
            var id = ExpeditionAcceptance.SelectRefillTarget(view, local, out x, out z);
            SelfTest.True(id == 5 && x == 8.71 && z == 8.36,
                "refill follows nearest visible pickup, not nearby sheep or nominal spawn", id.ToString());
            var oldDistance = System.Math.Sqrt((10.5 - 8.71) * (10.5 - 8.71) + (9.79 - 8.36) * (9.79 - 8.36));
            SelfTest.True(oldDistance > 1.8 && x != 10 && z != 10,
                "nominal stopping point leaves drifted crate outside production range", oldDistance.ToString());
            id = ExpeditionAcceptance.SelectRefillTarget(new SnapshotView(), local, out x, out z);
            SelfTest.True(id == 0 && x == 10 && z == 10, "unknown visibility uses nominal fallback", id.ToString());
        }

        private static void NumberKeysSelectRifleShotgun()
        {
            for (var slot = 1; slot <= 2; slot++)
            {
                var sampler = new InputSampler();
                var key = slot == 1 ? KeyCode.Alpha2 : KeyCode.Alpha3;
                sampler.SetKey(key, true);
                sampler.Update(34);
                InputIntent intent;
                SelfTest.True(sampler.TryTakeCommand(out intent)
                    && (intent.Buttons & InputSampler.ButtonSwitchWeapon) != 0 && intent.SwitchTo == slot,
                    "number key produces normal weapon switch intent", slot.ToString());
                sampler.SetKey(key, false); sampler.Update(34);
                while (sampler.TryTakeCommand(out intent)) { }
                sampler.SetKey(key, true); sampler.Update(34);
                SelfTest.True(sampler.TryTakeCommand(out intent)
                    && (intent.Buttons & InputSampler.ButtonSwitchWeapon) != 0 && intent.SwitchTo == slot,
                    "released number key permits a retry press", slot.ToString());
            }
        }

        private static void ToolPhaseRaceIsNarrow()
        {
            // Reflection keeps the diagnostic-only decision internal to Boot, not a gameplay API.
            var predicate = typeof(ExpeditionAcceptance).GetMethod("IsToolPhaseRace",
                System.Reflection.BindingFlags.Static | System.Reflection.BindingFlags.NonPublic);
            SelfTest.True(predicate != null, "acceptance phase-race classifier exists", "missing");
            for (byte op = 1; op <= 7; op++)
                for (byte result = 0; result <= 9; result++)
                    for (byte stage = 0; stage <= 3; stage++)
                    {
                        var actual = (bool)predicate.Invoke(null, new object[] { op, result, stage });
                        var expected = op == 5 && result == 4 && (stage == 1 || stage == 3);
                        SelfTest.True(actual == expected, "only tool wrongPhase at reward/end is recoverable",
                            op + ":" + result + ":" + stage);
                    }
        }

        private static void PendingMatchesExactReceipt()
        {
            var model = new ExpeditionHudModel();
            model.Apply(new ExpeditionStatePayload { RunId = 2, Revision = 1 });
            model.BeginRequest(1);
            model.Apply(new ExpeditionStatePayload { RunId = 2, Revision = 2, LastRequestId = uint.MaxValue, LastResult = 3 });
            SelfTest.True(model.Pending, "unrelated larger wrongRun receipt cannot finish pending request", "MAX vs 1");
            model.Apply(new ExpeditionStatePayload { RunId = 2, Revision = 3, LastRequestId = 1, LastResult = 1 });
            SelfTest.True(!model.Pending, "exact receipt finishes pending request", "1 vs 1");
        }

        private static void WrongRunMaxDoesNotPoisonRequest()
        {
            var socket = new ExpeditionSocket();
            var transport = new UdpTransport(socket, () => 0.0);
            var loop = new GameLoop(new SnapshotView(), new EntityViews(), new Hud(), new FrameProfiler());
            loop.Transport = transport;
            SelfTest.True(transport.Connect("mem", 0), "connect", "false");
            transport.Poll(64);
            SelfTest.True(transport.State == ConnectionState.Connected, "production transport connected", transport.State.ToString());
            var state = new ExpeditionStatePayload { Mode = 1, RunId = 2, Revision = 1, LocalPid = 2, PlayerCount = 1,
                Players = new[] { new ExpeditionPlayerState { Pid = 2 } } };
            loop.OnPacket(new PacketHeader { Type = PacketType.ExpeditionState }, ExpeditionCodec.EncodeState(state));
            SelfTest.True(loop.RequestExpedition(5, 1), "first request sent", "false");
            var first = socket.LastRequest.RequestId;
            state.Revision = 2; state.LastRequestId = uint.MaxValue; state.LastResult = 3;
            loop.OnPacket(new PacketHeader { Type = PacketType.ExpeditionState }, ExpeditionCodec.EncodeState(state));
            SelfTest.True(loop.Expedition.Pending, "wrongRun does not clear in-flight request", "pending false");
            state.Revision = 3; state.LastRequestId = first; state.LastResult = 1;
            loop.OnPacket(new PacketHeader { Type = PacketType.ExpeditionState }, ExpeditionCodec.EncodeState(state));
            SelfTest.True(loop.RequestExpedition(5, 1) && socket.LastRequest.RequestId == first + 1,
                "late wrongRun MAX cannot poison next nonzero request", socket.LastRequest.RequestId.ToString());
            loop.LocalPlayerId = 2;
            state.Revision = 4; state.LastRequestId = socket.LastRequest.RequestId;
            loop.OnPacket(new PacketHeader { Type = PacketType.ExpeditionState }, ExpeditionCodec.EncodeState(state));
            state.Revision = 5; state.LastRequestId = uint.MaxValue;
            loop.OnPacket(new PacketHeader { Type = PacketType.ExpeditionState }, ExpeditionCodec.EncodeState(state));
            var lastSent = socket.LastRequest.RequestId;
            SelfTest.True(!loop.RequestExpedition(5, 1) && !loop.RequestExpedition(5, 1)
                && loop.ExpeditionRequestFailure == "requestIdExhausted" && socket.LastRequest.RequestId == lastSent,
                "exhausted watermark never wraps zero or reuses an old request ID", loop.ExpeditionRequestFailure);
            transport.Close();
        }

        private sealed class ExpeditionSocket : IDatagramSocket
        {
            private readonly System.Collections.Generic.Queue<byte[]> _incoming = new System.Collections.Generic.Queue<byte[]>();
            public ExpeditionRequest LastRequest;
            public bool IsBound { get; private set; }
            public int Port { get { return 40405; } }
            public bool HasDatagram { get { return _incoming.Count != 0; } }
            public bool Bind(int port) { IsBound = true; return true; }
            public void Connect(string host, int port) { IsBound = true; }
            public int Receive(byte[] buffer)
            {
                if (_incoming.Count == 0) return 0;
                var bytes = _incoming.Dequeue(); System.Array.Copy(bytes, buffer, bytes.Length); return bytes.Length;
            }
            public bool Send(byte[] bytes, int length)
            {
                var reader = new PacketReader(bytes); PacketHeader header;
                if (PacketHeader.Read(reader, out header) != DecodeFailure.Ok) return true;
                byte[] body; reader.TryReadBytes(reader.Remaining, out body);
                if (header.Type == PacketType.Hello)
                {
                    var payload = HandshakeCodec.EncodeHelloAck(1234, 0xCAFEBABEu);
                    var ack = new PacketHeader { Version = PacketHeader.CurrentProtocolVersion, Type = PacketType.HelloAck,
                        Flags = (PacketFlags)PacketHeader.RequiredFlags(PacketType.HelloAck), Session = 7, MsgId = 1, Seq = 1 };
                    _incoming.Enqueue(PacketWriter.Build(ack, payload, 0, payload.Length));
                }
                if (header.Type == PacketType.ExpeditionRequest) ExpeditionCodec.DecodeRequest(body, out LastRequest);
                return true;
            }
            public void Close() { IsBound = false; }
        }

        private static void RejectsPreviousRunUntilReset()
        {
            var model = new ExpeditionHudModel();
            model.Apply(new ExpeditionStatePayload { Mode = 1, RunId = 2, Revision = 1, Stage = 2, ObjectiveHp = 900 });
            model.BeginRequest(44);
            var accepted = model.Apply(new ExpeditionStatePayload { Mode = 1, RunId = 1, Revision = 999, Stage = 3, ObjectiveHp = 0 });
            SelfTest.True(!accepted && model.State.RunId == 2 && model.State.ObjectiveHp == 900 && model.Pending,
                "delayed previous run cannot roll back authority or pending", "run2 then run1");
            model.Reset();
            SelfTest.True(model.Apply(new ExpeditionStatePayload { Mode = 1, RunId = 1, Stage = 1 }) && model.State.RunId == 1,
                "identity reset permits a new room run numbering", "reset");
        }

        private static void AuthorityPendingDeadline()
        {
            var model = new ExpeditionHudModel();
            SelfTest.True(!model.Visible, "Classic remains default", "initial");
            model.Apply(new ExpeditionStatePayload { Mode = 1, RunId = 7, Revision = 1, ClockMs = 1000,
                Stage = 1, Node = 0, RewardDeadlineMs = 31000, LocalPid = 2, OfferId = 9,
                OfferCount = 3, Choices = new byte[] { 1, 4, 0 }, RerollsLeft = 1,
                Players = new ExpeditionPlayerState[] { new ExpeditionPlayerState { Pid = 2 } } });
            SelfTest.True(model.RewardVisible && model.RemainingSeconds == 30, "server offer opens 30 seconds", "reward");
            model.BeginRequest(12);
            SelfTest.True(model.Pending && model.LocalCardMask == 0, "pending does not grant a card", "request");
            model.Tick(1500);
            SelfTest.True(model.RemainingSeconds == 29, "countdown advances from authority clock", "time");
            model.Apply(new ExpeditionStatePayload { Mode = 1, RunId = 7, Revision = 2, ClockMs = 2500,
                Stage = 1, RewardDeadlineMs = 31000, LocalPid = 2, OfferId = 9, OfferCount = 3,
                Choices = new byte[] { 1, 4, 0 }, LastRequestId = 12, LastResult = 6,
                Players = new ExpeditionPlayerState[] { new ExpeditionPlayerState { Pid = 2 } } });
            SelfTest.True(!model.Pending && model.LastResult == 6 && model.LocalCardMask == 0,
                "rejection clears pending without local reward", "reject");
        }

        private static void LoopStateRestoresAuthority()
        {
            var loop = new GameLoop(new SnapshotView(), new EntityViews(), new Hud(), new FrameProfiler());
            var state = new ExpeditionStatePayload { Mode = 1, RunId = 8, Revision = 4, ClockMs = 10000,
                Stage = 1, Node = 1, RewardDeadlineMs = 40000, LocalPid = 2, OfferId = 20,
                OfferCount = 3, Choices = new byte[] { 2, 3, 0 }, RerollsLeft = 0, PlayerCount = 1,
                ObjectiveHp = 720, ProgressMs = 123000,
                Players = new ExpeditionPlayerState[] { new ExpeditionPlayerState { Pid = 2, CardMask = 1, ToolReadyAtMs = 12000 } } };
            loop.OnPacket(new PacketHeader { Type = PacketType.ExpeditionState }, ExpeditionCodec.EncodeState(state));
            SelfTest.True(loop.ExpeditionStatesApplied == 1 && loop.ExpeditionInputBlocked, "production packet opens modal", "state14");
            SelfTest.True(loop.Expedition.LocalCardMask == 1 && loop.Expedition.State.ObjectiveHp == 720
                && loop.Expedition.State.ProgressMs == 123000 && loop.Expedition.ToolSeconds == 2,
                "complete authority restores build/objective/tool without local reward", "restore");
            loop.OnPacket(new PacketHeader { Type = PacketType.ExpeditionState }, new byte[1]);
            SelfTest.True(loop.DecodeFailures == 1 && loop.Expedition.LocalCardMask == 1, "malformed state cannot erase authority", "bad length");
        }

        private static void InteractReloadFireRemainsHeld()
        {
            var sampler = new InputSampler();
            sampler.SetKey(KeyCode.E, true);
            sampler.SetKey(KeyCode.R, true);
            sampler.SetKey(KeyCode.Mouse0, true);
            var count = 0;
            for (var i = 0; i < 60; i++)
            {
                sampler.Update(34);
                InputIntent intent;
                while (sampler.TryTakeCommand(out intent))
                {
                    count++;
                    SelfTest.True((intent.Buttons & InputSampler.ButtonInteract) != 0,
                        "held E survives reload and fire every production sample", intent.Buttons.ToString());
                }
            }
            SelfTest.True(count >= 60, "continuous production samples cover refill interval", count.ToString());
        }

        private static void ModalZeroAndRelease()
        {
            var sampler = new InputSampler();
            sampler.SetReadyHeld(true);
            sampler.SetKey(KeyCode.Mouse0, true);
            sampler.SetConfirm(true);
            sampler.SetModalBlocked(true);
            SelfTest.True(!sampler.RequestWeaponSlot(2), "modal rejects settings weapon selection", "switch");
            sampler.Update(34);
            InputIntent intent;
            while (sampler.TryTakeCommand(out intent))
                SelfTest.True(intent.Buttons == 0 && intent.MoveX == 0, "modal still sends zero commands", "zero");
            SelfTest.True(!sampler.ConfirmPressed && sampler.CurrentButtons == 0, "modal suppresses Enter and fire", "buttons");
            sampler.SetModalBlocked(false);
            sampler.Update(34);
            SelfTest.True(sampler.CurrentButtons == 0 && !sampler.ConfirmPressed, "release frame cannot click through", "release");
            sampler.SetConfirm(false);
            sampler.SetKey(KeyCode.Mouse0, false);
            sampler.Update(34);
            SelfTest.True(sampler.CurrentButtons == 0, "actual release remains neutral", "released");
            sampler.SetKey(KeyCode.Mouse0, true);
            sampler.Update(34);
            SelfTest.True((sampler.CurrentButtons & InputSampler.ButtonFire) != 0, "new input after release works", "fresh press");
        }
    }
}
