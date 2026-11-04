using System;
using Ac.Boot;
using Ac.Core;
using Ac.Net;
using Ac.Sim;
using Ac.UI;
using Ac.View;
using UnityEngine;
using UnityEngine.EventSystems;
using UnityEngine.UI;
using UnityEditor;

namespace Ac.Tests
{
    [InitializeOnLoad]
    public static class ExpeditionKeyboardSuite
    {
        private const string Active = "Ac.P1.KeyboardPlay.Active";
        private const string Deadline = "Ac.P1.KeyboardPlay.Deadline";
        static ExpeditionKeyboardSuite()
        {
            if (SessionState.GetBool(Active, false)) EditorApplication.update += CheckPlayMode;
        }
        public static void Register()
        {
            SelfTest.Add("expedition.keyboard_enter_modal_zero_command", EnterModalZeroCommand);
        }
        public static void RunInPlayMode()
        {
            SessionState.SetBool(Active, true);
            SessionState.SetFloat(Deadline, (float)EditorApplication.timeSinceStartup + 120f);
            EditorApplication.update -= CheckPlayMode;
            EditorApplication.update += CheckPlayMode;
            EditorApplication.EnterPlaymode();
        }
        private static void CheckPlayMode()
        {
            if (EditorApplication.timeSinceStartup > SessionState.GetFloat(Deadline, 0))
            { FinishPlayMode(1, "KEYBOARD PLAYMODE FAIL timeout"); return; }
            if (!EditorApplication.isPlaying || GameBootstrap.Loop == null || GameBootstrap.Loop.Frames < 60) return;
            try { FocusSubmitAndGuards(); FinishPlayMode(0, "KEYBOARD PLAYMODE OK official-focus-and-request13"); }
            catch (Exception exception) { Debug.LogException(exception); FinishPlayMode(1, "KEYBOARD PLAYMODE FAIL " + exception.Message); }
        }
        private static void FinishPlayMode(int code, string message)
        {
            SessionState.SetBool(Active, false);
            EditorApplication.update -= CheckPlayMode;
            if (code == 0) Debug.Log(message); else Debug.LogError(message);
            EditorApplication.Exit(code);
        }
        private static void EnterModalZeroCommand()
        {
            var sampler = new InputSampler(); sampler.SetReadyHeld(true); sampler.SetConfirm(true);
            sampler.SetKey(KeyCode.Mouse0, true); sampler.SetModalBlocked(true); sampler.Update(34);
            InputIntent intent;
            SelfTest.True(!sampler.ConfirmPressed, "reward Enter not Ready", "confirm");
            while (sampler.TryTakeCommand(out intent)) SelfTest.True(intent.Buttons == 0, "reward sends no Ready/fire", intent.Buttons.ToString());
        }
        private static void FocusSubmitAndGuards()
        {
            var settings = SettingsDefaults.Default();
            var layer = PresentationLayer.Create(settings);
            var loop = new GameLoop(new SnapshotView(), new EntityViews(), new Hud(), new FrameProfiler());
            var socket = new Socket();
            var transport = new UdpTransport(socket, () => 0.0);
            try
            {
                layer.Attach(loop);
                layer.BindSettings(new SettingsStore());
                var renderer = layer.Root.AddComponent<ExpeditionUiRenderer>();
                renderer.Bind(loop, layer);
                loop.Transport = transport;
                SelfTest.True(transport.Connect("mem", 0), "connect", "failed");
                transport.Poll(64);
                var state = Offer(1, 1);
                Feed(loop, state);
                renderer.HandleRewardNavigation(0, false);
                SelfTest.True(EventSystem.current != null, "official EventSystem exists after renderer Bind", "EventSystem.current null");
                SelfTest.True(EventSystem.current.currentSelectedGameObject != null, "first offer has official selected object", "currentSelectedGameObject null");
                SelfTest.True(EventSystem.current.currentSelectedGameObject.name == "Card0", "first offer focuses card0", "focus");
                SelfTest.True(renderer.HandleRewardNavigation(1, false), "advance focus", "false");
                SelfTest.True(EventSystem.current.currentSelectedGameObject.name == "Card1", "official focus advanced to card1", "focus");
                renderer.HandleRewardNavigation(0, true);
                SelfTest.True(socket.Requests == 1 && socket.Last.Op == 2 && socket.Last.ChoiceId == 4,
                    "official Button submit sends actual type13 choice", socket.Requests.ToString());
                renderer.HandleRewardNavigation(1, true);
                SelfTest.True(socket.Requests == 1, "pending blocks navigation/submit", "duplicate request");
                state = Offer(2, 2); state.LastRequestId = socket.Last.RequestId; state.LastResult = 1; Feed(loop, state);
                renderer.HandleRewardNavigation(0, false);
                SelfTest.True(EventSystem.current.currentSelectedGameObject.name == "Card0", "new offer resets focus", "focus");
                var skip = layer.Root.transform.Find("Ac.ExpeditionCanvas/Reward/Skip").GetComponent<Button>();
                skip.Select();
                state = Offer(2, 3); Feed(loop, state);
                renderer.HandleRewardNavigation(0, false);
                SelfTest.True(EventSystem.current.currentSelectedGameObject == skip.gameObject, "unrelated state preserves mouse-selected focus", "focus reset");
                renderer.HandleRewardNavigation(-1, false);
                SelfTest.True(EventSystem.current.currentSelectedGameObject.name == "Reroll", "reverse navigation", "focus");
                state = Offer(2, 4); state.RerollsLeft = 0; Feed(loop, state);
                skip.Select(); renderer.HandleRewardNavigation(-1, false);
                SelfTest.True(EventSystem.current.currentSelectedGameObject.name == "Card2", "disabled reroll skipped", "focus");
                renderer.HandleRewardNavigation(8, true);
                SelfTest.True(socket.Requests == 1, "invalid direction ignores submit", "request");
                state = Offer(3, 5); state.Resolved = 1; Feed(loop, state);
                renderer.HandleRewardNavigation(0, true);
                SelfTest.True(socket.Requests == 1, "resolved blocks submit", "request");
                state = Offer(4, 6); state.RewardDeadlineMs = state.ClockMs; Feed(loop, state);
                renderer.HandleRewardNavigation(1, true);
                SelfTest.True(socket.Requests == 1, "expired deadline blocks submit", "request");
                state = Offer(5, 7); Feed(loop, state);
                layer.ToggleSettings();
                SelfTest.True(layer.SettingsPanel.Visible && layer.SettingsInputBlocked, "real settings panel blocks input", "settings setup");
                renderer.HandleRewardNavigation(0, true);
                SelfTest.True(socket.Requests == 1, "settings blocks reward submit", "request");
            }
            catch (Exception exception)
            {
                Debug.LogException(exception);
                throw;
            }
            finally
            {
                transport.Close();
                layer.Dispose();
            }
        }
        private static ExpeditionStatePayload Offer(uint offer, uint revision)
        { return new ExpeditionStatePayload { Mode = 1, RunId = 1, Revision = revision, Stage = 1,
            ClockMs = 1000, RewardDeadlineMs = 31000, LocalPid = 1, PlayerCount = 1, OfferId = offer,
            OfferCount = 3, Choices = new byte[] { 1, 4, 0 }, RerollsLeft = 1,
            Players = new[] { new ExpeditionPlayerState { Pid = 1 } } }; }
        private static void Feed(GameLoop loop, ExpeditionStatePayload state)
        { loop.OnPacket(new PacketHeader { Type = PacketType.ExpeditionState }, ExpeditionCodec.EncodeState(state)); }
        private sealed class Socket : IDatagramSocket
        {
            private readonly System.Collections.Generic.Queue<byte[]> _queue = new System.Collections.Generic.Queue<byte[]>();
            public ExpeditionRequest Last; public int Requests;
            public bool IsBound { get; private set; } public int Port { get { return 40406; } }
            public bool HasDatagram { get { return _queue.Count != 0; } }
            public bool Bind(int port) { IsBound = true; return true; }
            public void Connect(string host, int port) { IsBound = true; }
            public int Receive(byte[] buffer) { if (_queue.Count == 0) return 0; var b = _queue.Dequeue(); Array.Copy(b, buffer, b.Length); return b.Length; }
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
                    _queue.Enqueue(PacketWriter.Build(ack, payload, 0, payload.Length));
                }
                if (header.Type == PacketType.ExpeditionRequest) { ExpeditionCodec.DecodeRequest(body, out Last); Requests++; }
                return true;
            }
            public void Close() { IsBound = false; }
        }
    }
}
