using System;
using System.IO;
using Ac.UI;
using Ac.Net;
using UnityEngine;

namespace Ac.Boot
{
    // Opt-in real-player smoke: only intent is automated; packets and frames remain production-owned.
    public sealed class PlayerAcceptance : MonoBehaviour
    {
        private string _output;
        private string _name;
        private int _team;
        private double _start;
        private double _playingAt = -1;
        private int _playingSnapshots;
        private bool _sawTeam;
        private bool _done;
        private double _nextReady;
        private byte _lastPhase = 255;
        private bool _recovery;
        private bool _armed;
        private bool _interrupted;
        private bool _recovered;
        private int _stableSeconds;
        private double _stableAt = -1;
        private ConnectionState _previousState;
        private int _recoveryInterruptions;
        private ushort _originalPlayer;
        private ushort _originalSession;
        private int _resumeSnapshots;
        private int _resumeStates;
        [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.BeforeSceneLoad)]
        private static void Boot()
        {
            var args = Environment.GetCommandLineArgs();
            var output = BenchJson.Arg(args, "-playerAcceptanceOut", null);
            if (output == null) return;
            var go = new GameObject("PlayerAcceptance");
            DontDestroyOnLoad(go);
            var driver = go.AddComponent<PlayerAcceptance>();
            driver._output = output;
            driver._name = BenchJson.Arg(args, "-playerAcceptanceName", "peer");
            driver._recovery = BenchJson.ArgInt(args, "-playerAcceptanceRecovery", 0) == 1;
            driver._stableSeconds = BenchJson.ArgInt(args, "-playerAcceptanceStableSeconds", 0);
            driver._team = BenchJson.ArgInt(args, "-playerAcceptanceTeam", 2);
            driver._start = Time.realtimeSinceStartupAsDouble;
            Application.runInBackground = true;
        }
        private void Update()
        {
            if (_done) return;
            var now = Time.realtimeSinceStartupAsDouble;
            if (now - _start > 90) { Finish(false, "timeout"); return; }
            var loop = GameBootstrap.Loop;
            if (loop == null || loop.Sampler == null) return;
            if (GameBootstrap.Presentation != null) GameBootstrap.Presentation.Flow.Lobby.SetName(_name);
            loop.LocalName = _name;
            if (_armed && !_interrupted && loop.Transport.State != ConnectionState.Connected)
            {
                _interrupted = true;
                _resumeSnapshots = loop.SnapshotsApplied;
                _resumeStates = loop.MatchStateCount;
            }
            if (_recovered && loop.Transport.State != ConnectionState.Connected)
            {
                if (_previousState == ConnectionState.Connected) _recoveryInterruptions++;
                _stableAt = -1;
            }
            _previousState = loop.Transport.State;
            var state = loop.LastMatchState;
            if (state.Phase != _lastPhase)
            {
                _lastPhase = state.Phase;
                Debug.Log("PLAYER ACCEPTANCE phase=" + state.Phase + " snapshots=" + loop.SnapshotsApplied);
            }
            if (_armed && _interrupted && loop.Transport.State == ConnectionState.Connected
                && loop.SnapshotsApplied - _resumeSnapshots >= 20 && loop.MatchStateCount > _resumeStates)
            {
                _recovered = true;
                if (_stableAt < 0) _stableAt = now;
                if (now - _stableAt >= _stableSeconds)
                {
                    Finish(loop.LocalPlayerId == _originalPlayer && loop.Transport.Session == _originalSession
                        && _recoveryInterruptions == 0, "recovered-authoritative-state");
                    return;
                }
            }
            if (state.Players == null || state.Players.Length != _team || loop.LocalPlayerId == 0) return;
            _sawTeam = true;
            if (state.Phase == Hud.PhaseLobby && now >= _nextReady)
            {
                loop.Sampler.SetReadyHeld(true);
                loop.Sampler.Flush();
                _nextReady = now + 1;
            }
            if (state.Phase == Hud.PhasePlaying)
            {
                if (_playingAt < 0) { _playingAt = now; _playingSnapshots = loop.SnapshotsApplied; }
                if (now - _playingAt >= 5 && loop.SnapshotsApplied - _playingSnapshots >= 20)
                {
                    if (!_recovery) { Finish(true, "connected-team-playing-snapshots"); return; }
                    if (!_armed)
                    {
                        _armed = true;
                        _originalPlayer = loop.LocalPlayerId;
                        _originalSession = loop.Transport.Session;
                        File.WriteAllText(_output + ".ready", "playing");
                    }

                }
            }
        }
        private void Finish(bool pass, string reason)
        {
            _done = true;
            var loop = GameBootstrap.Loop;
            var pid = System.Diagnostics.Process.GetCurrentProcess().Id;
            var json = "{\"pass\":" + (pass ? "true" : "false") + ",\"reason\":\"" + reason + "\",\"processId\":" + pid
                + ",\"session\":" + (loop == null || loop.Transport == null ? 0 : loop.Transport.Session)
                + ",\"playerId\":" + (loop == null ? 0 : loop.LocalPlayerId) + ",\"team\":" + _team
                + ",\"sawTeam\":" + (_sawTeam ? "true" : "false")
                + ",\"snapshots\":" + (loop == null ? 0 : loop.SnapshotsApplied)
                + ",\"playingSnapshots\":" + (loop == null ? 0 : loop.SnapshotsApplied - _playingSnapshots)
                + ",\"retransmitExhaustions\":" + (loop == null || loop.Transport == null ? 0 : loop.Transport.Machine.RetransmitExhaustions)
                + ",\"receiveTimeouts\":" + (loop == null || loop.Transport == null ? 0 : loop.Transport.Machine.ReceiveTimeouts)
                + ",\"stableSeconds\":" + (_stableAt < 0 ? 0 : (int)(Time.realtimeSinceStartupAsDouble - _stableAt))
                + ",\"recoveryInterruptions\":" + _recoveryInterruptions
                + ",\"recovered\":" + (_recovered ? "true" : "false")
                + ",\"recoverySnapshots\":" + (loop == null || !_interrupted ? 0 : loop.SnapshotsApplied - _resumeSnapshots)
                + ",\"identityPreserved\":" + (loop != null && loop.LocalPlayerId == _originalPlayer && loop.Transport.Session == _originalSession ? "true" : "false") + "}";
            Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(_output)));
            File.WriteAllText(_output, json);
            Debug.Log("PLAYER ACCEPTANCE " + (pass ? "OK " : "FAIL ") + json);
            Application.Quit(pass ? 0 : 1);
        }
    }
}
