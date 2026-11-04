using System;
using System.IO;
using Ac.Core;
using Ac.Net;
using Ac.Sim;
using Ac.UI;
using UnityEngine;

namespace Ac.Boot
{
    // Opt-in intent driver only. Never writes world, HP, cards, clocks or authority.
    public static class ExpeditionAcceptance
    {
        private static Driver _driver;
        private static bool _checked;
        public static void Drive(GameLoop loop)
        {
            if (!_checked)
            {
                _checked = true;
                var args = Environment.GetCommandLineArgs();
                var output = BenchJson.Arg(args, "-expeditionAcceptanceOut", null);
                if (output != null) _driver = new Driver(output, args);
            }
            if (_driver == null) return;
            try { _driver.Tick(loop); }
            catch (Exception ex) { _driver.Finish(loop, false, "driver-exception:" + ex.Message); }
        }
        [Serializable] private sealed class Result
        {
            public bool pass, humanFunVerified = false, recovered, identityPreserved;
            public string reason, build, recoveryStage;
            public bool combatRecovered, rewardRecovered, rewardStable;
            public int processId, team, playerId, session, commands, snapshots, states, kills, acceptedChoices, acceptedTools;
            public int nodes, progressNodes, recoveryInterruptions, stableSeconds;
            public uint runId, triggers;
        }
        [Serializable] private sealed class Entry
        {
            public double elapsed;
            public string kind;
            public int snapshots, commands, states, session, playerId;
            public byte op, choice;
            public string authorityBase64;
            public uint runId, revision, clockMs, progressMs, rewardDeadlineMs, offerId, lastRequestId;
            public int node, stage, result, lastResult, cardMask, hp, mag, reserve, kills;
            public int[] candidates;
            public ushort target;
            public float x, z;
            public double yaw, pitch;
        }
        private sealed class Driver
        {
            private readonly string _out, _name, _build;
            private readonly int _team, _timeout, _stable;
            private readonly bool _recovery, _rewardRecovery;
            private bool _rewardStable;
            private readonly double _start;
            private double _nextAction, _nextTrace, _stableAt = -1;
            private bool _done, _armed, _interrupted, _recovered;
            private ushort _pid, _session;
            private uint _run, _lastAck;
            private bool _refilling;
            private byte _previousStage = 255;
            private int _combatReleaseFrames;

            private int _seenStates, _nodeBits, _progressBits, _choices, _tools, _kills, _interruptions;
            private byte _pendingOp;
            private ExpeditionStatePayload _beforeRecovery;
            private int _recoveryStates, _recoverySnapshots;
            private uint _triggers;
            private bool _identityPreserved;
            public Driver(string output, string[] args)
            {
                _out = Path.GetFullPath(output);
                Directory.CreateDirectory(Path.GetDirectoryName(_out));
                _name = BenchJson.Arg(args, "-expeditionAcceptanceName", "p1peer");
                _build = BenchJson.Arg(args, "-expeditionAcceptanceBuild", "hunter");
                _team = BenchJson.ArgInt(args, "-expeditionAcceptanceTeam", 1);
                _timeout = BenchJson.ArgInt(args, "-expeditionAcceptanceTimeout", 900);
                _recovery = BenchJson.ArgInt(args, "-expeditionAcceptanceRecovery", 0) == 1;
                _rewardRecovery = _recovery && BenchJson.Arg(args, "-expeditionAcceptanceRecoveryStage", "combat") == "reward";
                _stable = BenchJson.ArgInt(args, "-expeditionAcceptanceStableSeconds", 15);
                _start = Time.realtimeSinceStartupAsDouble;
                Application.runInBackground = true;
            }
            private void Trace(GameLoop loop, string kind, byte op = 0, byte choice = 0, ushort target = 0)
            {
                FrameEntity local;
                loop.View.TryGetLocalAuthority(out local);
                var s = loop.Expedition.State;
                var e = new Entry {
                    elapsed = Time.realtimeSinceStartupAsDouble - _start, kind = kind, op = op, choice = choice,
                    authorityBase64 = s == null ? null : Convert.ToBase64String(ExpeditionCodec.EncodeState(s)),
                    snapshots = loop.SnapshotsApplied, commands = loop.CommandsSent,
                    states = loop.ExpeditionStatesApplied, session = loop.Transport.Session, playerId = loop.LocalPlayerId,
                    target = target, x = local.XCm / 100f, z = local.ZCm / 100f,
                    yaw = loop.Sampler.YawRad, pitch = loop.Sampler.PitchRad,
                    hp = local.HpRatioUnits, mag = loop.Ammo.Mag, reserve = loop.Ammo.Reserve, kills = _kills,
                    cardMask = loop.Expedition.LocalCardMask };
                if (s != null) {
                    e.runId=s.RunId; e.revision=s.Revision; e.clockMs=s.ClockMs; e.progressMs=s.ProgressMs;
                    e.rewardDeadlineMs=s.RewardDeadlineMs; e.offerId=s.OfferId; e.lastRequestId=s.LastRequestId;
                    e.node=s.Node; e.stage=s.Stage; e.result=s.Result; e.lastResult=s.LastResult;
                    e.candidates=new int[] {s.Choices[0],s.Choices[1],s.Choices[2]};
                }
                File.AppendAllText(_out + ".timeline.jsonl", JsonUtility.ToJson(e) + "\n");
            }
            private bool Request(GameLoop loop, byte op, byte choice = 0)
            {
                if (!loop.RequestExpedition(op, choice)) return false;
                _pendingOp = op;
                Trace(loop, "request", op, choice);
                _nextAction = Time.realtimeSinceStartupAsDouble + 1;
                return true;
            }
            public void Tick(GameLoop loop)
            {
                if (_done || loop == null || loop.Sampler == null || loop.Transport == null) return;
                var now = Time.realtimeSinceStartupAsDouble;
                if (now - _start > _timeout) { Finish(loop, false, "timeout"); return; }
                var sampler = loop.Sampler;
                // Explicit injected-input seam also supports independent background peers.
                sampler.SetKey(KeyCode.Mouse0, false);
                sampler.AllowBackgroundInjectedInput = true;
                sampler.OnFocusChanged(true);
                sampler.SetKey(KeyCode.W, false); sampler.SetKey(KeyCode.S, false);
                sampler.SetKey(KeyCode.A, false); sampler.SetKey(KeyCode.D, false);
                sampler.SetKey(KeyCode.R, false); sampler.SetKey(KeyCode.E, false);
                loop.LocalName = _name;
                if (GameBootstrap.Presentation != null) GameBootstrap.Presentation.Flow.Lobby.SetName(_name);
                var connected = loop.Transport.State == ConnectionState.Connected;
                if (_armed && !connected && !_interrupted)
                {
                    _interrupted = true; _recoveryStates = loop.ExpeditionStatesApplied;
                    _recoverySnapshots = loop.SnapshotsApplied; Trace(loop, "interrupted");
                }
                if (_recovered && !connected && _stableAt >= 0) { _interruptions++; _stableAt = -1; }
                var state = loop.Expedition.State;
                if (!connected || loop.LocalPlayerId == 0) return;
                if (state == null)
                {
                    var members = loop.LastMatchState.Players;
                    if (members != null && members.Length == _team && loop.LastMatchState.Phase == Hud.PhaseLobby)
                    {
                        var host = members[0].Pid;
                        for (var i = 1; i < members.Length; i++) host = Math.Min(host, members[i].Pid);
                        if (loop.LocalPlayerId == host && now >= _nextAction) Request(loop, 1);
                    }
                    return;
                }
                if (_seenStates != loop.ExpeditionStatesApplied)
                {
                    _seenStates = loop.ExpeditionStatesApplied;
                    Trace(loop, "authority");
                    if (_run != 0 && state.RunId != _run) { Finish(loop, false, "run-changed"); return; }
                    if (state.RunId != 0) _run = state.RunId;
                    if (state.LastRequestId != 0 && state.LastRequestId != _lastAck)
                    {
                        _lastAck = state.LastRequestId;
                        Trace(loop, "receipt", _pendingOp);
                        if (state.LastResult != 1 && !(_pendingOp == 5 && state.LastResult == 8))
                        { Finish(loop, false, "request-rejected:" + state.LastResult); return; }
                        if (state.LastResult == 1 && _pendingOp == 2) _choices++;
                        if (state.LastResult == 1 && _pendingOp == 5) _tools++;
                        if (state.LastResult == 8) { Trace(loop, "tool-unavailable-retry-after-1s"); _nextAction = now + 1; }
                        _pendingOp = 0;
                    }
                    if (state.Stage == 2 && state.Node >= 1) _nodeBits |= 1 << (state.Node - 1);
                    if (state.ProgressMs >= 170000 && state.Node >= 1) _progressBits |= 1 << (state.Node - 1);
                    for (var i = 0; i < state.PlayerCount; i++) if (state.Players[i].Pid == loop.LocalPlayerId)
                        _triggers = Math.Max(_triggers, state.Players[i].TriggerCount);
                    if (_armed && !_interrupted) _beforeRecovery = state;
                }
                if (_rewardRecovery && _armed && !_rewardStable &&
                    (state.Stage != 1 || state.ClockMs >= _beforeRecovery.RewardDeadlineMs))
                { Finish(loop, false, "reward-recovery-deadline-expired"); return; }
                if (_interrupted && !_recovered && loop.ExpeditionStatesApplied > _recoveryStates &&
                    (_rewardRecovery || loop.SnapshotsApplied > _recoverySnapshots + 20))
                {
                    _identityPreserved = loop.LocalPlayerId == _pid && loop.Transport.Session == _session;
                    var b = _beforeRecovery;
                    // Full payload must restore stable identity/build/offer/deadline/receipt, not just snapshots.
                    var same = b != null && state.RunId == b.RunId && state.Node == b.Node && state.Stage == b.Stage
                        && state.OfferId == b.OfferId && state.RewardDeadlineMs == b.RewardDeadlineMs
                        && state.LastRequestId == b.LastRequestId && state.LastResult == b.LastResult;
                    if (_rewardRecovery) same &= state.OfferCount == b.OfferCount && state.Resolved == 0
                        && b.Resolved == 0 && state.RerollsLeft == b.RerollsLeft;
                    for (var i = 0; same && i < 3; i++) same &= state.Choices[i] == b.Choices[i];
                    for (var i = 0; same && i < b.PlayerCount; i++)
                    {
                        var found = false;
                        for (var j = 0; j < state.PlayerCount; j++) if (state.Players[j].Pid == b.Players[i].Pid)
                            found = state.Players[j].CardMask == b.Players[i].CardMask;
                        same &= found;
                    }
                    if (!same || !_identityPreserved) { Finish(loop, false, "full-state-recovery-mismatch"); return; }
                    _recovered = true; _stableAt = now; Trace(loop, "full-state-recovered");
                }
                if (_recovered && connected && _stableAt < 0) _stableAt = now;
                if (state.Stage == 3)
                {
                    var pass = state.Result == 1 && state.Node == 3 && _nodeBits == 7 && _progressBits == 7
                        && _choices >= 3 && _tools > 0 && _kills > 0 && _triggers > 0 && loop.CommandsSent > 1000
                        && (!_recovery || (_recovered && _identityPreserved && _interruptions == 0 && now - _stableAt >= _stable))
                        && (!_rewardRecovery || _rewardStable);
                    Finish(loop, pass, pass ? "three-node-authoritative-success" : "ended-with-incomplete-gates:" + state.Result); return;
                }
                var roster = loop.LastMatchState.Players;
                if (roster == null || roster.Length != _team) return;
                for (var i = 0; i < roster.Length; i++) if (roster[i].Pid == loop.LocalPlayerId) _kills = Math.Max(_kills, roster[i].Kills);
                if (loop.LastMatchState.Phase == Hud.PhaseLobby)
                {
                    var host = roster[0].Pid;
                    for (var i = 1; i < roster.Length; i++) host = Math.Min(host, roster[i].Pid);
                    if (state.Mode == 0 && loop.LocalPlayerId == host && now >= _nextAction) Request(loop, 1);
                    if (state.Mode == 1) sampler.SetReadyHeld(true);
                    return;
                }
                if (state.Stage != 2) _previousStage = state.Stage;
                if (_rewardRecovery && !_rewardStable)
                {
                    if (!_armed && state.Stage == 1 && state.OfferId != 0 && state.Resolved == 0 && !loop.Expedition.Pending)
                    {
                        _armed = true; _pid = loop.LocalPlayerId; _session = loop.Transport.Session;
                        _beforeRecovery = state;
                        File.WriteAllText(_out + ".ready", "reward"); Trace(loop, "reward-recovery-armed");
                    }
                    if (_armed)
                    {
                        var baseline = _beforeRecovery;
                        var preserved = state.RunId == baseline.RunId && state.Node == baseline.Node
                            && state.OfferId == baseline.OfferId && state.OfferCount == baseline.OfferCount
                            && state.RewardDeadlineMs == baseline.RewardDeadlineMs && state.Resolved == 0
                            && state.RerollsLeft == baseline.RerollsLeft && state.LastRequestId == baseline.LastRequestId
                            && state.LastResult == baseline.LastResult;
                        for (var c = 0; c < 3; c++) preserved &= state.Choices[c] == baseline.Choices[c];
                        if (!preserved) { Finish(loop, false, "reward-offer-changed-during-stability"); return; }
                        // Preserve this real unresolved offer until recovery plus continuous stable period.
                        // No clock/deadline mutation, no request while the recovery gate is pending.
                        if (!_recovered || now - _stableAt < _stable) return;
                        _rewardStable = true; Trace(loop, "reward-recovery-stable");
                    }
                }
                if (state.Stage == 1 && loop.Expedition.CanChoose && now >= _nextAction)
                {
                    byte choice = 0;
                    var preferred = _build == "breach" ? new byte[] {4, 6, 5} : new byte[] {1, 2, 3};
                    foreach (var p in preferred) foreach (var c in state.Choices) if (c == p && choice == 0) choice = c;
                    Request(loop, 2, choice); return;
                }
                if (state.Stage != 2) { _previousStage = state.Stage; return; }
                if (_previousStage != 2) { _previousStage = 2; _combatReleaseFrames = 3; }
                // Respect production modal resume guard: release fire for complete sampling frames.
                // Never clear/bypass the guard or sample an opening reward click as a shot.
                if (_combatReleaseFrames > 0)
                { _combatReleaseFrames--; Trace(loop, "combat-release-frame"); return; }
                if (_recovery && !_rewardRecovery && !_armed && state.Node == 1 && state.ProgressMs >= 10000 && !loop.Expedition.Pending)
                {
                    _armed = true; _pid = loop.LocalPlayerId; _session = loop.Transport.Session;
                    _beforeRecovery = state; File.WriteAllText(_out + ".ready", "combat"); Trace(loop, "recovery-armed");
                }
                // Keep request receipt stable across the scheduled outage and 15s recovery window.
                if ((!_armed || (_recovered && now - _stableAt >= _stable)) && now >= _nextAction && loop.Expedition.ToolSeconds == 0)
                    Request(loop, 5, 1);
                Combat(loop, now);
            }
            private static bool BarnBlocks(double x, double z, double tx, double tz, double clearance)
            {
                var low = -4.0 - clearance; var high = 4.0 + clearance;
                var enter = 0.0; var leave = 1.0;
                var dx = tx - x; var dz = tz - z;
                if (Math.Abs(dx) < 1e-9) { if (x < low || x > high) return false; }
                else { var a=(low-x)/dx; var b=(high-x)/dx; enter=Math.Max(enter,Math.Min(a,b)); leave=Math.Min(leave,Math.Max(a,b)); }
                if (Math.Abs(dz) < 1e-9) { if (z < low || z > high) return false; }
                else { var a=(low-z)/dz; var b=(high-z)/dz; enter=Math.Max(enter,Math.Min(a,b)); leave=Math.Min(leave,Math.Max(a,b)); }
                return enter <= leave;
            }
            private void Combat(GameLoop loop, double now)
            {
                FrameEntity local;
                if (!loop.View.TryGetLocalAuthority(out local)) return;
                var x = local.XCm / 100.0; var z = local.ZCm / 100.0;
                FrameEntity target = default(FrameEntity); var distance = double.MaxValue;
                loop.View.ForEachVisible((ushort id, in FrameEntity e) => {
                    if ((e.KindFlags & 3) != 1 || e.HpRatioUnits == 0 || e.State == 8) return;
                    if (BarnBlocks(x, z, e.XCm / 100.0, e.ZCm / 100.0, 0)) return;
                    var dx = e.XCm / 100.0 - x; var dz = e.ZCm / 100.0 - z;
                    var d = dx * dx + dz * dz;
                    if (d < distance) { distance = d; target = e; }
                });
                var yaw = loop.Sampler.YawRad; var pitch = 0.0;
                if (target.Id != 0)
                {
                    // Production yaw=atan2(dx,dz), +pitch is up, eye=base+1.6.
                    // Sheep head boxes use local +Z; rotate its center by authority yaw.
                    var tyaw = target.YawUnits * (2 * Math.PI / 65536.0);
                    var dx = target.XCm / 100.0 + Math.Sin(tyaw) * .66 - x;
                    var dz = target.ZCm / 100.0 + Math.Cos(tyaw) * .66 - z;
                    yaw = Math.Atan2(dx, dz);
                    pitch = Math.Atan2(target.YCm / 100.0 + .90 - (local.YCm / 100.0 + 1.6), Math.Sqrt(dx * dx + dz * dz));
                }
                var scale = InputSampler.MouseRadPerPixel * loop.Sampler.SensitivityValue;
                var delta = yaw - loop.Sampler.YawRad;
                while (delta > Math.PI) delta -= 2 * Math.PI;
                while (delta < -Math.PI) delta += 2 * Math.PI;
                loop.Sampler.AddMouse(delta / scale, (pitch - loop.Sampler.PitchRad) / scale);
                // Move along world-to-view basis toward a safe interior point. Movement remains normal commands.
                if (loop.Ammo.Reserve < 15) _refilling = true;
                if (loop.Ammo.Reserve >= 100) _refilling = false;
                // Existing production crate at (10,10), E hold 1500ms. No ammo grants in driver.
                var goalX = _refilling ? 10.0 : 12.0; var goalZ = _refilling ? 10.0 : 0.0;
                var mx = goalX - x; var mz = goalZ - z;
                loop.Sampler.SetKey(KeyCode.E, _refilling && mx * mx + mz * mz <= 2.25);
                // Barn is [-4,4] x [-4,4]; give player radius .4 and digital-axis steering clearance.
                if (BarnBlocks(x, z, goalX, goalZ, 1.2))
                {
                    var side = z >= 0 ? 6.0 : -6.0;
                    if (Math.Abs(z) < 5.5) { mx = (x < 0 ? -6.0 : 6.0) - x; mz = side - z; }
                    else { mx = 6.0 - x; mz = side - z; }
                }
                if (mx * mx + mz * mz > 1.0)
                {
                    var forward = Math.Sin(yaw) * mx + Math.Cos(yaw) * mz;
                    var right = Math.Cos(yaw) * mx - Math.Sin(yaw) * mz;
                    loop.Sampler.SetKey(KeyCode.W, forward > .3); loop.Sampler.SetKey(KeyCode.S, forward < -.3);
                    // InputSampler A is positive moveY, matching production movement's right basis.
                    loop.Sampler.SetKey(KeyCode.A, right > .3); loop.Sampler.SetKey(KeyCode.D, right < -.3);
                }
                loop.Sampler.SetKey(KeyCode.Mouse0, target.Id != 0);
                loop.Sampler.SetKey(KeyCode.R, loop.Ammo.Mag == 0);
                // GameLoop owns normal 30Hz sampling/uplink; never Flush per render frame.
                if (now >= _nextTrace) { Trace(loop, "combat-intent", target: target.Id); _nextTrace = now + 1; }
            }
            public void Finish(GameLoop loop, bool pass, string reason)
            {
                if (_done) return; _done = true;
                var now = Time.realtimeSinceStartupAsDouble;
                var result = new Result { pass = pass, reason = reason, build = _build,
                    recoveryStage = _rewardRecovery ? "reward" : "combat",
                    rewardRecovered = _rewardRecovery && _recovered, rewardStable = _rewardStable,
                    combatRecovered = _recovery && !_rewardRecovery && _recovered,
                    processId = System.Diagnostics.Process.GetCurrentProcess().Id, team = _team,
                    playerId = loop == null ? 0 : loop.LocalPlayerId, session = loop == null || loop.Transport == null ? 0 : loop.Transport.Session,
                    runId = _run, nodes = _nodeBits, progressNodes = _progressBits, acceptedChoices = _choices,
                    acceptedTools = _tools, kills = _kills, triggers = _triggers,
                    commands = loop == null ? 0 : loop.CommandsSent, snapshots = loop == null ? 0 : loop.SnapshotsApplied,
                    states = loop == null ? 0 : loop.ExpeditionStatesApplied, recovered = _recovered, identityPreserved = _identityPreserved,
                    recoveryInterruptions = _interruptions, stableSeconds = _stableAt < 0 ? 0 : (int)(now - _stableAt) };
                File.WriteAllText(_out, JsonUtility.ToJson(result, true));
                Debug.Log("EXPEDITION ACCEPTANCE " + (pass ? "OK " : "FAIL ") + reason);
                Application.Quit(pass ? 0 : 1);
            }
        }
    }
}
