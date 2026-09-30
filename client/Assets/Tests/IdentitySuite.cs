using System;
using System.Collections.Generic;
using System.Text;
using Ac.Boot;
using Ac.Core;
using Ac.Net;
using Ac.Sim;
using Ac.UI;
using Ac.View;
using UnityEngine;

namespace Ac.Tests
{
    // C12「输入昵称与房间码进大厅」的身份接线守门用例：昵称键入 → 清洗（与 wire 上限一致）→
    // 从 type=10 的 MatchState 载荷里认领本地 pid → GameLoop.LocalPlayerId / SnapshotView →
    // 相机与 HUD 真的绑到那个实体上（走 PresentationLayer 的既有路径，不绕过）。
    //
    // 协议 2 身份只来自服务端权威 LocalPid；昵称仅作为 Join 输入。
    // 每个用例结束时 Dispose 掉呈现层与帧回路（DestroyImmediate）并复位 RenderSettings。
    internal static class IdentitySuite
    {
        public static void Register()
        {
            SelfTest.Add("identity.name_input", ChecksNameInput);
            SelfTest.Add("identity.name_wire_limits", ChecksNameWireLimits);
            SelfTest.Add("identity.resolve_from_match_state", ChecksResolveFromMatchState);
            SelfTest.Add("identity.zero_local_pid_unbinds", ChecksZeroLocalPidUnbinds);
            SelfTest.Add("identity.rebinds_on_room_change", ChecksRebindsOnRoomChange);
            SelfTest.Add("identity.lobby_typing_drives_loop", ChecksLobbyTypingDrivesLoop);
            SelfTest.Add("identity.camera_hud_follow_local", ChecksCameraHudFollowLocal);
            SelfTest.Add("identity.not_resolved_per_frame", ChecksNotResolvedPerFrame);
            SelfTest.Add("identity.session_lifecycle", ChecksSessionLifecycle);
        }
        // 呈现层 + 帧回路的最小组合。相机/HUD 用例必须走真实的 PresentationLayer，不能绕过。
        private sealed class Rig : IDisposable
        {
            internal PresentationLayer Layer;
            internal GameLoop Loop;
            internal SnapshotFrame Frame;

            // LightingRig 改的是场景级 RenderSettings：装配前后各存/放一次，免得留给下一个用例。
            private readonly bool _fog;
            private readonly UnityEngine.Rendering.AmbientMode _ambientMode;
            private readonly Color _ambientLight;
            private readonly float _ambientIntensity;
            private readonly UnityEngine.FogMode _fogMode;
            private readonly float _fogStart;
            private readonly float _fogEnd;
            private readonly Color _fogColor;

            internal Rig(in SettingsSnapshot settings)
            {
                _fog = RenderSettings.fog;
                _ambientMode = RenderSettings.ambientMode;
                _ambientLight = RenderSettings.ambientLight;
                _ambientIntensity = RenderSettings.ambientIntensity;
                _fogMode = RenderSettings.fogMode;
                _fogStart = RenderSettings.fogStartDistance;
                _fogEnd = RenderSettings.fogEndDistance;
                _fogColor = RenderSettings.fogColor;

                Layer = PresentationLayer.Create(settings);
                Loop = new GameLoop(new SnapshotView(), new EntityViews(), new Hud(), new FrameProfiler());
                Layer.Attach(Loop);      // 昵称订阅也在这条既有缝上：Lobby.OnNameChanged → loop.LocalName
                Frame = default(SnapshotFrame);
                Frame.Entities = new FrameEntity[SnapshotView.MaxRecordsPerFrame];
                Frame.RemovedIds = new ushort[SnapshotView.MaxRemovedPerFrame];
            }

            internal void WireSeams()
            {
                Loop.Fx = Layer.FxSink;          // 相机姿态在 fx 段（SyncCamera）
                Loop.Draw = Layer.DrawSink;
                Loop.Overlay = Layer.OverlaySink; // 屏幕流/队伍表在 overlay 段
            }

            public void Dispose()
            {
                if (Layer != null) Layer.Dispose();
                Layer = null;
                Loop = null;
                RenderSettings.fog = _fog;
                RenderSettings.ambientMode = _ambientMode;
                RenderSettings.ambientLight = _ambientLight;
                RenderSettings.ambientIntensity = _ambientIntensity;
                RenderSettings.fogMode = _fogMode;
                RenderSettings.fogStartDistance = _fogStart;
                RenderSettings.fogEndDistance = _fogEnd;
                RenderSettings.fogColor = _fogColor;
            }
        }

        private static PacketHeader MatchStateHeader()
        {
            var header = default(PacketHeader);
            header.Type = PacketType.MatchState;      // type=10
            return header;
        }

        // 每帧全量的快照：id → (x, z)。本地实体不与别的实体共 id。
        private static void FeedSnapshot(Rig rig, uint tick, ushort[] ids, double[] xs, double[] zs, byte[] hp)
        {
            var frame = rig.Frame;
            frame.Tick = tick;
            frame.ServerTimeMs = tick * 16u;
            frame.LastAckedSeq = (ushort)tick;
            frame.BaselineTick = 0u;
            frame.RemovedCount = 0;
            frame.EntityCount = ids.Length;
            for (var i = 0; i < ids.Length; i++)
            {
                var entity = default(FrameEntity);
                entity.Id = ids[i];
                entity.KindFlags = 0;                                  // 线上 kind 0 = 玩家
                entity.XCm = (short)Math.Round(xs[i] * 100.0);
                entity.ZCm = (short)Math.Round(zs[i] * 100.0);
                entity.YCm = 0;
                entity.YawUnits = 0;
                entity.PitchUnits = 0;
                entity.HpRatioUnits = hp[i];
                frame.Entities[i] = entity;
            }
            SelfTest.True(rig.Loop.ApplySnapshot(frame), "快照应当被镜像接受", "被丢弃");
        }

        // ---- 输入捕获 ----

        private static void ChecksNameInput()
        {
            var input = new NameInput();
            SelfTest.True(!input.Feed(""), "空输入不算变化", "算成变化了");
            SelfTest.True(!input.Feed(null), "null 输入不算变化", "算成变化了");
            SelfTest.Equal(0, (long)input.ChangeCount);
            SelfTest.True(input.Text == string.Empty, "初始是空串", input.Text);

            SelfTest.True(input.Feed("bo"), "键入要算变化", "没算");
            SelfTest.True(input.Text == "bo", "键入进缓冲区", input.Text);
            SelfTest.Equal(1, (long)input.ChangeCount);
            SelfTest.True(ReferenceEquals(input.Text, input.Text), "Text 必须是缓存的那一份（不是每次现拼）", "每次取都是新对象");

            SelfTest.True(input.Feed("b"), "继续键入", "没算");
            SelfTest.True(input.Text == "bob", "顺序拼接", input.Text);
            SelfTest.True(input.Feed("\b"), "退格算变化", "没算");
            SelfTest.True(input.Text == "bo", "退格删一个", input.Text);
            SelfTest.True(!input.Feed("\n\r\t"), "回车/制表不进昵称", "进去了");
            SelfTest.True(input.Feed("\b\b"), "退格到空算变化", "没算");
            SelfTest.True(input.Text == string.Empty, "两次退格删光", input.Text);
            SelfTest.True(!input.Feed("\b"), "空缓冲区上退格不算变化", "算成变化了");
            SelfTest.True(input.Text == string.Empty, "退格过多不会负长度", input.Text);

            // 代理对：一个字符（两个码元）退格要整个删，不能在缓冲区里留孤立代理。
            var emoji = new NameInput();
            emoji.Feed("🐑");
            SelfTest.True(emoji.Text == "🐑", "整只羊进缓冲区", emoji.Text);
            emoji.Feed("\b");
            SelfTest.True(emoji.Text == string.Empty, "退格删掉整只羊（不留半个代理）", emoji.Text);

            // 上限：缓冲区按码元封顶，超出记账丢弃（清洗层的 12 字节截断是另一道）。
            var capped = new NameInput();
            capped.Feed(new string('x', 200));
            SelfTest.Equal(NameInput.MaxChars, (long)capped.Length);
            SelfTest.True(capped.DroppedCount == 200 - NameInput.MaxChars, "超出部分要记账", capped.DroppedCount.ToString());

            var cleared = new NameInput();
            cleared.Feed("ab");
            cleared.Clear();
            SelfTest.True(cleared.Text == string.Empty && cleared.Length == 0, "Clear 清空", cleared.Text);

            // 没有输入的那些帧必须一次都不分配（帧预算 0 B 的守门）。
            var quiet = new NameInput();
            quiet.Feed("a");
            for (var i = 0; i < 100; i++) quiet.Feed("");
            var before = AllocMeter.Begin();
            for (var i = 0; i < 1000; i++) quiet.Feed("");
            AllocMeter.AssertZero(before);
        }

        // ---- 昵称清洗/校验与 wire 上限一致 ----

        private static void ChecksNameWireLimits()
        {
            // 两侧上限必须是同一个数：服务端 §5.8 的 MatchState 名字 1..12 字节。
            SelfTest.Equal((long)MatchStateCodec.MinNameBytes, (long)Lobby.NameMinBytes);
            SelfTest.Equal((long)MatchStateCodec.MaxNameBytes, (long)Lobby.NameMaxBytes);
            // 定长部分 = 16（pid 2 + nameLen 1 + 其后 13 字节），与 server codec.hpp 同名常量一致；
            // "确实等于布局"的检查放在 CodecSuite（那里有按 wire 顺序造记录的 builder）。
            SelfTest.Equal((long)MatchStateCodec.FixedRecordBytes, 16L);

            SelfTest.True(!Lobby.IsValidName(""), "空昵称非法", "判成合法");
            SelfTest.True(!Lobby.IsValidName(null), "null 昵称非法", "判成合法");
            SelfTest.True(Lobby.IsValidName("a"), "1 字节合法", "判成非法");
            SelfTest.True(Lobby.IsValidName(new string('a', 12)), "12 字节合法", "判成非法");
            SelfTest.True(!Lobby.IsValidName(new string('a', 13)), "13 字节非法", "判成合法");

            // 清洗：控制字符与 <>&"' 剔除、首尾空白去掉、按字节截断（不切断多字节字符）。
            SelfTest.True(Lobby.SanitizeName("  a<b>c&d\"e'f  ") == "abcdef", "剥离禁止字符", Lobby.SanitizeName("  a<b>c&d\"e'f  "));
            SelfTest.True(Lobby.SanitizeName("a\u0007\u0001b") == "ab", "剥离控制字符", Lobby.SanitizeName("a\u0007\u0001b"));
            SelfTest.True(Lobby.SanitizeName("\u0007\u0008") == string.Empty, "全是控制字符 ⇒ 空", Lobby.SanitizeName("\u0007\u0008"));
            SelfTest.True(!Lobby.IsValidName(Lobby.SanitizeName("<>&\"'")), "禁止字符全被剥光 ⇒ 非法", "判成合法");

            var twelve = Lobby.SanitizeName(new string('a', 40));
            SelfTest.Equal(12, (long)Chat.Utf8Bytes(twelve));
            SelfTest.True(Lobby.IsValidName(twelve), "截断到 12 字节后合法", twelve);

            // UTF-8 截断不切断字符：10 个 ASCII + 1 个汉字（13 字节）只能留前 10 个。
            var cut = Lobby.SanitizeName(new string('a', 10) + "羊");
            SelfTest.True(Chat.Utf8Bytes(cut) <= Lobby.NameMaxBytes, "截断后不超 12 字节", Chat.Utf8Bytes(cut).ToString());
            SelfTest.Equal(10, (long)cut.Length);
            SelfTest.True(cut[cut.Length - 1] == 'a', "没有半个汉字尾巴", cut);

            // 汉字：4 个 = 12 字节，合法；5 个 = 15 字节，截到 4 个。
            var four = Lobby.SanitizeName(new string('羊', 4));
            var five = Lobby.SanitizeName(new string('羊', 5));
            SelfTest.Equal(12, (long)Chat.Utf8Bytes(four));
            SelfTest.True(Lobby.IsValidName(four), "4 个汉字合法", four);
            SelfTest.Equal(4, (long)five.Length);
            SelfTest.Equal(12, (long)Chat.Utf8Bytes(five));

            // 代理对（4 字节）：3 只 = 12 字节合法，第 4 只放不下。
            // 注意 string.Length 是 UTF-16 码元数：一只羊 = 2 个码元。
            var emojiThree = Lobby.SanitizeName("🐑🐑🐑");
            SelfTest.Equal(12, (long)Chat.Utf8Bytes(emojiThree));
            SelfTest.Equal(6, (long)emojiThree.Length);
            SelfTest.Equal(6, (long)Lobby.SanitizeName("🐑🐑🐑🐑").Length);
            SelfTest.Equal(12, (long)Chat.Utf8Bytes(Lobby.SanitizeName("🐑🐑🐑🐑")));

            // Lobby 记录的是清洗后的值，且同名不重入（键入被剥掉的字符不该产生变化）。
            var lobby = new Lobby();
            var changes = 0;
            lobby.OnNameChanged += name => { changes += 1; };
            lobby.SetName("牧 羊");
            SelfTest.True(lobby.Name == "牧 羊", "中间的空白保留、首尾去掉", lobby.Name);
            SelfTest.Equal(1, changes);
            lobby.SetName("牧 羊");
            SelfTest.Equal(1, changes);
            lobby.SetName("  牧 羊  ");     // 清洗后还是同一个值 ⇒ 不重入
            SelfTest.Equal(1, changes);
            lobby.SetName("<>");
            SelfTest.Equal(2, changes);
            SelfTest.True(!lobby.IsNameValid, "被剥光后无效", lobby.Name);
            lobby.SetName("<<>>");          // 还是空串 ⇒ 不再回调
            SelfTest.Equal(2, changes);
        }

        // ---- 由 MatchState 的权威 localPid 绑定身份 ----

        private static void ChecksResolveFromMatchState()
        {
            var rig = new Rig(SettingsDefaults.Default());
            try
            {
                SelfTest.Equal(0, (long)rig.Loop.LocalPlayerId);
                SelfTest.Equal(0, (long)rig.Loop.View.LocalPlayerId);

                rig.Loop.LocalName = "bob";
                // localPid=0，即使昵称相同也不能绑定。
                rig.Loop.OnPacket(MatchStateHeader(), Payload(Hud.PhaseLobby, 0, 0, new ushort[] { 7 }, new string[] { "bob" }, 0));
                SelfTest.Equal(0, (long)rig.Loop.LocalPlayerId);
                SelfTest.Equal(1, (long)rig.Loop.Identity.ResolveCount);
                SelfTest.Equal(0, (long)rig.Loop.Identity.MatchedCount);

                rig.Loop.OnPacket(MatchStateHeader(), Payload(Hud.PhasePlaying, 1, 0, new ushort[] { 7, 3 }, new string[] { "牧羊人", "bob" }, 3));
                SelfTest.Equal(3, (long)rig.Loop.LocalPlayerId);
                SelfTest.Equal(3, (long)rig.Loop.View.LocalPlayerId);          // 镜像的本地指针同步
                SelfTest.Equal(3, (long)rig.Loop.Views.LocalPlayerId);         // 视图池的本地指针同一处同步（此前零调用者）
                SelfTest.Equal(1, (long)rig.Loop.IdentityChanges);
                SelfTest.Equal(1, (long)rig.Loop.Identity.MatchedCount);

                // 同一条 MatchState 再来一遍：pid 没变 ⇒ 不算一次身份变化（1Hz 周期下发不刷计数）。
                var before = rig.Loop.IdentityChanges;
                rig.Loop.OnPacket(MatchStateHeader(), Payload(Hud.PhasePlaying, 1, 0, new ushort[] { 7, 3 }, new string[] { "牧羊人", "bob" }, 3));
                SelfTest.Equal(before, (long)rig.Loop.IdentityChanges);
                SelfTest.Equal(3, (long)rig.Loop.LocalPlayerId);

                // 同名的两个会话收到相同玩家表，但各自绑定服务端指定的不同 pid。
                var other = new GameLoop(new SnapshotView(), new EntityViews(), new Hud(), new FrameProfiler());
                other.LocalName = "same";
                rig.Loop.LocalName = "same";
                var pids = new ushort[] { 2, 9 };
                var names = new string[] { "same", "same" };
                rig.Loop.OnPacket(MatchStateHeader(), Payload(0, 0, 0, pids, names, 9));
                other.OnPacket(MatchStateHeader(), Payload(0, 0, 0, pids, names, 2));
                SelfTest.Equal(9, rig.Loop.LocalPlayerId);
                SelfTest.Equal(2, other.LocalPlayerId);
                var resolves = rig.Loop.Identity.ResolveCount;
                rig.Loop.LocalName = "renamed";
                SelfTest.Equal(9, rig.Loop.LocalPlayerId);
                SelfTest.Equal(resolves, rig.Loop.Identity.ResolveCount);
                var failures = rig.Loop.DecodeFailures;
                rig.Loop.OnPacket(MatchStateHeader(), Payload(0, 0, 0, pids, names, 99));
                SelfTest.Equal(failures + 1, rig.Loop.DecodeFailures);
                SelfTest.Equal(9, rig.Loop.LocalPlayerId);
            }
            finally { rig.Dispose(); }
        }

        // localPid=0 时不得从名字推断身份，HUD 和相机保持未绑定。
        private static void ChecksZeroLocalPidUnbinds()
        {
            var rig = new Rig(SettingsDefaults.Default());
            try
            {
                rig.WireSeams();
                rig.Loop.LocalName = "nobody";
                rig.Loop.OnPacket(MatchStateHeader(), Payload(Hud.PhasePlaying, 1, 0, new ushort[] { 1, 3 }, new string[] { "a", "bob" }, 0));
                SelfTest.Equal(0, (long)rig.Loop.LocalPlayerId);
                SelfTest.Equal(0, (long)rig.Loop.View.LocalPlayerId);
                SelfTest.Equal(0, (long)rig.Loop.Identity.MatchedCount);

                for (uint tick = 1; tick <= 3; tick++)
                {
                    FeedSnapshot(rig, tick, new ushort[] { 1, 3 }, new double[] { 4.0, -9.0 }, new double[] { -2.0, 0.0 }, new byte[] { 200, 100 });
                    rig.Loop.Frame(1000.0 / 60.0);
                }
                FrameEntity ignored;
                SelfTest.True(!rig.Loop.View.TryGetLocalAuthority(out ignored), "没有本地身份就不许有本地权威", "有");
                // 相机没有被任何实体带走：SyncCamera 找不到本地实体就整段早退。
                SelfTest.Equal(0, (long)rig.Layer.CameraPoseUpdates);
                SelfTest.Equal(0, (long)rig.Loop.Sample.HpRatio255);
                SelfTest.True(rig.Layer.FxTicks > 0, "fx 段本身照跑（不是靠没打点混过去）", rig.Layer.FxTicks.ToString());
            }
            finally { rig.Dispose(); }
        }

        // 换房/重连/自己中途离开：名字一样，pid 会变；表里没自己了就必须退回 0（不许留旧 pid）。
        private static void ChecksRebindsOnRoomChange()
        {
            var rig = new Rig(SettingsDefaults.Default());
            try
            {
                rig.Loop.LocalName = "bob";
                rig.Loop.OnPacket(MatchStateHeader(), Payload(Hud.PhasePlaying, 1, 0, new ushort[] { 7, 3 }, new string[] { "牧羊人", "bob" }, 3));
                SelfTest.Equal(3, (long)rig.Loop.LocalPlayerId);

                // 换房/重连后 pid 不同：必须重新解析，而不是继续用 3。
                rig.Loop.OnPacket(MatchStateHeader(), Payload(Hud.PhaseLobby, 0, 0, new ushort[] { 1, 5 }, new string[] { "bob", "x" }, 1));
                SelfTest.Equal(1, (long)rig.Loop.LocalPlayerId);
                SelfTest.Equal(1, (long)rig.Loop.View.LocalPlayerId);
                SelfTest.Equal(2, (long)rig.Loop.IdentityChanges);
                SelfTest.Equal(2, (long)rig.Loop.Identity.MatchedCount);

                // 自己离开对局（表里没这一行了）：退回 0，实体 id 会被回收，不能留旧值指到别人。
                rig.Loop.OnPacket(MatchStateHeader(), Payload(Hud.PhasePlaying, 2, 0, new ushort[] { 5 }, new string[] { "x" }, 0));
                SelfTest.Equal(0, (long)rig.Loop.LocalPlayerId);
                SelfTest.Equal(0, (long)rig.Loop.View.LocalPlayerId);
                SelfTest.Equal(3, (long)rig.Loop.IdentityChanges);

                // 再回来（同名同 pid）：又要能认领上。
                rig.Loop.OnPacket(MatchStateHeader(), Payload(Hud.PhasePlaying, 3, 0, new ushort[] { 5, 1 }, new string[] { "x", "bob" }, 1));
                SelfTest.Equal(1, (long)rig.Loop.LocalPlayerId);
                SelfTest.Equal(4, (long)rig.Loop.IdentityChanges);
            }
            finally { rig.Dispose(); }
        }

        // ---- 键入 → 清洗 → 身份，端到端 ----

        private static void ChecksLobbyTypingDrivesLoop()
        {
            var rig = new Rig(SettingsDefaults.Default());
            try
            {
                rig.WireSeams();   // 相位由 overlay 段推给屏幕流，这条用例要走真实那一帧
                var flow = rig.Layer.Flow;
                SelfTest.True(flow.LobbyVisible, "起始相位就是大厅", "不是");

                // 一个字符一个字符地敲（每帧一段 inputString，和 Input.inputString 的语义一致）。
                SelfTest.True(flow.CaptureName("b"), "大厅里键入了字符", "没接收");
                SelfTest.True(flow.CaptureName("o"), "继续键入", "没接收");
                SelfTest.True(flow.CaptureName("b"), "继续键入", "没接收");
                SelfTest.True(flow.Lobby.Name == "bob", "键入进大厅昵称", flow.Lobby.Name);
                SelfTest.True(rig.Loop.LocalName == "bob", "昵称必须同步到帧回路（Join 的输入）", rig.Loop.LocalName);
                SelfTest.True(!flow.CaptureName(""), "没有输入的那一帧不算变化", "算成变化了");

                // 清洗与 wire 上限在这条路上真的生效：超 12 字节被截到 12。
                var longOne = new string('a', 20);
                SelfTest.True(flow.CaptureName(longOne), "长串照收（清洗层截断）", "没收");
                SelfTest.True(flow.Lobby.Name == ("bob" + longOne).Substring(0, 12), "截到 12 字节", flow.Lobby.Name);
                SelfTest.True(rig.Loop.LocalName == flow.Lobby.Name, "帧回路拿到的是清洗后的名字", rig.Loop.LocalName);

                // 敲进去的名字真的能把身份认领出来（不靠测试直接写 LocalName）。
                rig.Loop.OnPacket(MatchStateHeader(), Payload(Hud.PhasePlaying, 1, 0,
                    new ushort[] { 7, 4 }, new string[] { "牧羊人", flow.Lobby.Name }, 4));
                SelfTest.Equal(4, (long)rig.Loop.LocalPlayerId);

                // 改名只更新 Join 昵称；不会认领另一条恰好同名的记录。
                flow.NameInput.Clear();
                SelfTest.True(flow.CaptureName("cat"), "清空后重新键入", "没收");
                SelfTest.True(flow.Lobby.Name == "cat", "改名进了大厅", flow.Lobby.Name);
                SelfTest.Equal(4, (long)rig.Loop.LocalPlayerId);
                rig.Loop.OnPacket(MatchStateHeader(), Payload(Hud.PhasePlaying, 2, 0,
                    new ushort[] { 4, 9 }, new string[] { "bob", "cat" }, 4));
                SelfTest.Equal(4, (long)rig.Loop.LocalPlayerId);
                SelfTest.Equal(4, (long)rig.Loop.View.LocalPlayerId);

                // 相位由服务器下发：跑一帧让 overlay 段把 playing 推给屏幕流，再验"非大厅不收昵称"。
                FeedSnapshot(rig, 1, new ushort[] { 9, 4 }, new double[] { 1.0, 2.0 }, new double[] { 0.0, 0.0 }, new byte[] { 255, 255 });
                rig.Loop.Frame(1000.0 / 60.0);
                SelfTest.True(!flow.LobbyVisible, "相位已切到 playing", flow.Phase.ToString());

                // 对局相位里不接收昵称键入（对局的按键属于 InputSampler）。
                var name = flow.Lobby.Name;
                SelfTest.True(!flow.CaptureName("z"), "大厅之外的相位不收昵称", "收了");
                SelfTest.True(flow.Lobby.Name == name, "非大厅相位昵称不许变", flow.Lobby.Name);
            }
            finally { rig.Dispose(); }
        }

        // ---- 相机 / HUD 真的绑到认领出来的实体（走 PresentationLayer 的既有路径） ----

        private static void ChecksCameraHudFollowLocal()
        {
            var rig = new Rig(SettingsDefaults.Default());
            try
            {
                rig.WireSeams();
                var layer = rig.Layer;
                var flow = layer.Flow;

                // 玩家自己在大厅敲的昵称 → 身份从 type=10 的载荷里认领（pid 3）。
                flow.CaptureName("bob");
                rig.Loop.OnPacket(MatchStateHeader(), Payload(Hud.PhasePlaying, 1, 0,
                    new ushort[] { 7, 3 }, new string[] { "牧羊人", "bob" }, 3));
                SelfTest.Equal(3, (long)rig.Loop.LocalPlayerId);

                // 快照：3 号在 (4,-2)，7 号在 (-9,0) —— 相机只能跟 3 号走。
                for (uint tick = 1; tick <= 3; tick++)
                {
                    FeedSnapshot(rig, tick, new ushort[] { 3, 7 }, new double[] { 4.0, -9.0 }, new double[] { -2.0, 0.0 }, new byte[] { 128, 255 });
                    rig.Loop.Frame(1000.0 / 60.0);
                }

                SelfTest.True(layer.CameraPoseUpdates > 0, "相机姿态必须每帧被同步", "一次都没同步");
                var eye = layer.MainCamera.transform.position;
                SelfTest.True(Math.Abs(eye.x - 4f) < 0.01f, "相机落在本地实体 3 的 x 上", eye.x.ToString("R"));
                SelfTest.True(Math.Abs(eye.z + 2f) < 0.01f, "相机落在本地实体 3 的 z 上", eye.z.ToString("R"));
                SelfTest.True(Math.Abs(eye.y - (float)FpsCamera.EyeHeightMeters) < 1e-4, "眼高来自 FpsCamera", eye.y.ToString("R"));
                SelfTest.True(Math.Abs(eye.x + 9f) > 0.5f, "相机不许跟 7 号走", eye.ToString("R"));

                // HUD 采样来自本地实体（3 号 hp=128，不是 7 号的 255）。
                SelfTest.Equal(128, (long)rig.Loop.Sample.HpRatio255);
                // 队伍表/主机判定也按认领出的 pid 走（min pid = 3 ⇒ 自己是主机）。
                SelfTest.Equal(3, (long)flow.SelfPid);
                SelfTest.Equal(3, (long)flow.Lobby.SelfPid);
                SelfTest.True(flow.Lobby.IsHost, "3 是最小 pid ⇒ 自己是主机", flow.Lobby.HostPid.ToString());
                SelfTest.Equal(2, (long)flow.PlayerCount);

                // 改名不会解绑；只有权威 localPid=0 才停止跟随。
                var updates = layer.CameraPoseUpdates;
                rig.Loop.LocalName = new string('z', 12);
                SelfTest.Equal(3, (long)rig.Loop.LocalPlayerId);
                SelfTest.Equal(3, (long)rig.Loop.View.LocalPlayerId);
                rig.Loop.OnPacket(MatchStateHeader(), Payload(Hud.PhasePlaying, 2, 0,
                    new ushort[] { 7, 3 }, new string[] { "牧羊人", "bob" }, 0));
                SelfTest.Equal(0, (long)rig.Loop.LocalPlayerId);
                for (uint tick = 4; tick <= 6; tick++)
                {
                    FeedSnapshot(rig, tick, new ushort[] { 3, 7 }, new double[] { 41.0, -9.0 }, new double[] { -2.0, 0.0 }, new byte[] { 128, 255 });
                    rig.Loop.Frame(1000.0 / 60.0);
                }
                SelfTest.Equal(updates, (long)layer.CameraPoseUpdates);
            }
            finally { rig.Dispose(); }
        }

        // 身份解析只发生在权威包或会话切换时，不发生在每帧或改名时。
        private static void ChecksNotResolvedPerFrame()
        {
            var rig = new Rig(SettingsDefaults.Default());
            try
            {
                rig.WireSeams();
                rig.Layer.Flow.CaptureName("bob");
                rig.Loop.OnPacket(MatchStateHeader(), Payload(Hud.PhasePlaying, 1, 0,
                    new ushort[] { 7, 3 }, new string[] { "牧羊人", "bob" }, 3));
                SelfTest.Equal(3, (long)rig.Loop.LocalPlayerId);

                uint tick = 1;
                // 用例自己的合成载荷数组在**测量窗口之外**建一次：它们也分配托管内存，
                // 留在窗口里这条闸量到的就是测试脚手架，而不是"帧路径有没有分配"。
                var ids = new ushort[] { 3, 7, 8 };
                var xs = new double[] { 4.0, -9.0, 6.0 };
                var zs = new double[] { -2.0, 0.0, 1.0 };
                var hp = new byte[] { 128, 255, 255 };
                for (; tick <= 30; tick++)
                {
                    FeedSnapshot(rig, tick, ids, xs, zs, hp);
                    rig.Loop.Frame(1000.0 / 60.0);
                }

                var resolves = rig.Loop.Identity.ResolveCount;
                var changes = rig.Loop.IdentityChanges;
                var before = AllocMeter.Begin();
                for (; tick <= 90; tick++)
                {
                    FeedSnapshot(rig, tick, ids, xs, zs, hp);
                    rig.Loop.Frame(1000.0 / 60.0);
                }
                SelfTest.Equal(resolves, (long)rig.Loop.Identity.ResolveCount);   // 没有包 ⇒ 一次都不重解析
                SelfTest.Equal(changes, (long)rig.Loop.IdentityChanges);
                AllocMeter.AssertZero(before);
            }
            finally { rig.Dispose(); }
        }

        private sealed class QuietSocket : IDatagramSocket
        {
            public bool IsBound { get { return true; } }
            public int Port { get { return 0; } }
            public bool HasDatagram { get { return false; } }
            public bool Bind(int port) { return true; }
            public void Connect(string host, int port) { }
            public int Receive(byte[] buffer) { return 0; }
            public bool Send(byte[] datagram, int length) { return true; }
            public void Close() { }
        }

        private static void ChecksSessionLifecycle()
        {
            var loop = new GameLoop(new SnapshotView(), new EntityViews(), new Hud(), new FrameProfiler());
            var transport = new UdpTransport(new QuietSocket(), () => 0.0);
            loop.Transport = transport;
            var machine = transport.Machine;
            machine.StartConnect("mem:0", 1u, 0.0);
            SelfTest.True(machine.OnHelloAck(7, 1u, 2u, 0.0), "建立会话", "失败");
            loop.OnPacket(MatchStateHeader(), Payload(0, 0, 0, new ushort[] { 3 }, new string[] { "same" }, 3));
            machine.OnRetransmitExhausted(1.0);
            machine.Tick(2.0);
            SelfTest.Equal((long)ConnectionState.Reconnecting, (long)machine.State);
            SelfTest.Equal(3, loop.LocalPlayerId);
            machine.OnPacket(7, 3.0);
            SelfTest.Equal((long)ConnectionState.Connected, (long)machine.State);
            SelfTest.Equal(3, loop.LocalPlayerId);
            machine.OnDisconnect(DisconnectReason.ServerShutdown, 4.0);
            SelfTest.Equal(0, loop.LocalPlayerId);
            SelfTest.Equal(0, loop.View.LocalPlayerId);
            SelfTest.Equal(0, loop.Views.LocalPlayerId);
            SelfTest.Equal(0, loop.ResolveLocalPlayer());
            machine.StartConnect("mem:0", 2u, 5.0);
            SelfTest.True(machine.OnHelloAck(8, 1u, 3u, 5.0), "建立新会话", "失败");
            SelfTest.Equal(0, loop.LocalPlayerId);
            loop.OnPacket(MatchStateHeader(), Payload(0, 0, 0, new ushort[] { 9 }, new string[] { "same" }, 9));
            SelfTest.Equal(9, loop.LocalPlayerId);
            machine.StartConnect("mem:1", 3u, 6.0);
            SelfTest.Equal(0, loop.LocalPlayerId);
            loop.Transport = null;
            transport.Close();
        }

        // ---- 合成载荷 ----

        private static MatchStatePlayer Row(ushort pid, string name)
        {
            var row = default(MatchStatePlayer);
            row.Pid = pid;
            row.Name = name;
            row.HpRatio = 255;
            return row;
        }

        // type=10 的载荷：phase u8 | wave u8 | intermissionMs u16 | count u8 | 玩家块。
        // 生产侧没有匹配的编码器（这个包只有服务端 → 客户端一个方向），所以用例自带一个小写入器。
        private static byte[] Payload(byte phase, byte wave, ushort intermissionMs, ushort[] pids, string[] names, ushort localPid)
        {
            var bytes = new List<byte>(64);
            bytes.Add(phase);
            bytes.Add(wave);
            PutU16(bytes, intermissionMs);
            bytes.Add((byte)pids.Length);
            for (var i = 0; i < pids.Length; i++)
            {
                PutU16(bytes, pids[i]);
                var nameBytes = Encoding.UTF8.GetBytes(names[i]);
                bytes.Add((byte)nameBytes.Length);
                for (var b = 0; b < nameBytes.Length; b++) bytes.Add(nameBytes[b]);
                bytes.Add(1);        // ready
                bytes.Add(0);        // weapon（0 ≤ Shotgun）
                bytes.Add(255);      // hpRatio
                PutU16(bytes, 0);    // kills
                bytes.Add(0);        // mag
                PutU16(bytes, 0);    // reserve
                bytes.Add(0);        // reloadLeft10Ms
                bytes.Add(0);        // rage
                bytes.Add(0);        // rageLeft100Ms
                bytes.Add(0);        // downed
                bytes.Add(0);        // reviveRatio255
            }
            PutU16(bytes, localPid);
            return bytes.ToArray();
        }

        private static void PutU16(List<byte> bytes, ushort value)
        {
            bytes.Add((byte)(value & 0xFF));
            bytes.Add((byte)((value >> 8) & 0xFF));
        }
    }
}
