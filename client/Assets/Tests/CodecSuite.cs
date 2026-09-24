using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Text;
using Ac.Core;
using Ac.Net;
using Ac.Sim;
using UnityEngine;

namespace Ac.Tests
{
    // C02 §5.1/§5.2/§5.7：直读服务端与客户端共用的字节 fixture（S03 §5.5），
    // 断言 `# expect:` 里的每个键都被本端解码器复现；随后是反例与命令编码回环。
    internal static class CodecSuite
    {
        private static readonly string[] _sharedFixtures =
        {
            "command", "event_hit", "fragment", "hello", "hello_ack", "keepalive", "resume",
            "snapshot_diff", "snapshot_full", "truncated_command",
        };

        public static void Register()
        {
            SelfTest.Add("codec.roundtrip", ChecksCodecs);
        }

        private static void ChecksCodecs()
        {
            var directory = FixtureDirectory();
            SelfTest.True(Directory.Exists(directory), "共享字节 fixture 目录存在", directory);
            foreach (var name in _sharedFixtures)
            {
                CheckSharedFixture(directory, name);
            }
            ChecksRejections();
            ChecksCommandRoundtrip();
        }

        // ---- 共享 fixture（S03 §5.5，两侧读同一批文件）------------------------------------------

        private static void CheckSharedFixture(string directory, string name)
        {
            var fixture = ReadFixture(directory, name);
            var expect = fixture.Expect;
            CheckField(expect, "bytes", fixture.Bytes.Length);

            var reader = new PacketReader(fixture.Bytes);
            PacketHeader header;
            var failure = PacketHeader.Read(reader, out header);

            long expectedFailure;
            if (expect.TryGetValue("failure", out expectedFailure))
            {
                expect.Remove("failure");
                SelfTest.Equal(expectedFailure, (long)failure);
                CommandPayload ignored;
                SelfTest.Equal(expectedFailure, (long)CommandCodec.Decode(Slice(fixture.Bytes, reader.Position), out ignored));
                SelfTest.Equal(0, expect.Count);
                Debug.Log("[codec] " + name + " 按期望失败");
                return;
            }

            SelfTest.Equal(0, (long)failure);
            CheckField(expect, "version", header.Version);
            CheckField(expect, "type", (byte)header.Type);
            CheckField(expect, "flags", (ushort)header.Flags);
            CheckField(expect, "session", header.Session);
            CheckField(expect, "seq", header.Seq);
            CheckField(expect, "msgid", header.MsgId);
            CheckField(expect, "ackbase", header.AckBase);
            CheckField(expect, "ackbits", header.AckBits);
            CheckField(expect, "fragid", header.FragId);
            CheckField(expect, "fragindex", header.FragIndex);
            CheckField(expect, "fragcount", header.FragCount);

            var payload = Slice(fixture.Bytes, reader.Position);
            switch (header.Type)
            {
                case PacketType.Command:
                {
                    CommandPayload command;
                    SelfTest.Equal(0, (long)CommandCodec.Decode(payload, out command));
                    CheckField(expect, "movex", command.MoveX);
                    CheckField(expect, "movey", command.MoveY);
                    CheckField(expect, "yaw", command.Yaw);
                    CheckField(expect, "pitch", command.Pitch);
                    CheckField(expect, "buttons", command.Buttons);
                    CheckField(expect, "switchto", command.SwitchTo);
                    CheckField(expect, "cmdseq", command.Seq);
                    CheckField(expect, "clienttick", command.ClientTick);
                    break;
                }
                case PacketType.Snapshot:
                {
                    SnapshotPayload snapshot;
                    SelfTest.Equal(0, (long)SnapshotCodec.Decode(payload, out snapshot));
                    CheckField(expect, "tick", snapshot.Tick);
                    CheckField(expect, "servertimems", snapshot.ServerTimeMs);
                    CheckField(expect, "lastackedseq", snapshot.LastAckedSeq);
                    CheckField(expect, "baselinetick", snapshot.BaselineTick);
                    CheckField(expect, "count", snapshot.Records.Length);
                    CheckField(expect, "removedcount", snapshot.RemovedIds.Length);
                    CheckField(expect, "eventcount", snapshot.Events.Length);
                    if (snapshot.RemovedIds.Length > 0) CheckField(expect, "removedid", snapshot.RemovedIds[0]);
                    if (snapshot.Records.Length > 0)
                    {
                        var record = snapshot.Records[0];
                        CheckField(expect, "recordid", record.Id);
                        CheckField(expect, "kindflags", record.KindFlags);
                        CheckField(expect, "xcm", (ushort)record.XCm);
                        CheckField(expect, "ycm", (ushort)record.YCm);
                        CheckField(expect, "zcm", (ushort)record.ZCm);
                        CheckField(expect, "yawunits", record.YawUnits);
                        CheckField(expect, "pitchunits", record.PitchUnits);
                        CheckField(expect, "hpratiounits", record.HpRatioUnits);
                        CheckField(expect, "state", record.State);
                    }
                    break;
                }
                case PacketType.Event:
                {
                    EventFrame frame;
                    SelfTest.Equal(0, (long)EventCodec.DecodeFrame(payload, out frame));
                    CheckField(expect, "tick", frame.Tick);
                    CheckField(expect, "count", frame.Entries.Length);
                    if (frame.Entries.Length > 0)
                    {
                        var entry = frame.Entries[0];
                        CheckField(expect, "eventid", entry.EventId);
                        CheckField(expect, "eventtype", (byte)entry.Type);
                        CheckField(expect, "subjectid", entry.SubjectId);
                        CheckField(expect, "targetid", entry.TargetId);
                        CheckField(expect, "value", entry.Value);
                        CheckField(expect, "eventflags", entry.Flags);
                        CheckField(expect, "hitx", (ushort)entry.HitX);
                        CheckField(expect, "hity", (ushort)entry.HitY);
                        CheckField(expect, "hitz", (ushort)entry.HitZ);
                    }
                    break;
                }
                case PacketType.Hello:
                case PacketType.HelloAck:
                case PacketType.Resume:
                {
                    // C02 不含这三类的语义解码（C03 负责），这里只按 fixture 注释读载荷，
                    // 确认两侧对同一批字节的解释一致。
                    var payloadReader = new PacketReader(payload);
                    if (header.Type == PacketType.HelloAck)
                    {
                        uint serverTick;
                        uint salt;
                        payloadReader.TryReadU32(out serverTick);
                        payloadReader.TryReadU32(out salt);
                        CheckField(expect, "servertick", serverTick);
                        CheckField(expect, "salt", salt);
                    }
                    else
                    {
                        if (header.Type == PacketType.Hello)
                        {
                            uint nonce;
                            payloadReader.TryReadU32(out nonce);
                            CheckField(expect, "nonce", nonce);
                        }
                        byte[] token;
                        payloadReader.TryReadBytes(8, out token);
                        CheckField(expect, "token", ParseAsciiHex(token));
                    }
                    break;
                }
                default:
                    CheckField(expect, "payloadbytes", payload.Length);
                    break;
            }

            SelfTest.Equal(0, expect.Count);
            Debug.Log("[codec] " + name + " ok");
        }

        // ---- 反例 --------------------------------------------------------------------------

        private static void ChecksRejections()
        {
            CommandPayload command;
            EventFrame frame;
            SnapshotPayload snapshot;
            MatchStatePayload state;

            PacketHeader header;
            SelfTest.Equal((long)DecodeFailure.BadVersion,
                (long)PacketHeader.Read(new PacketReader(new byte[] { 2, (byte)PacketType.Command, 0, 0, 0, 0, 0, 0 }), out header));
            SelfTest.Equal((long)DecodeFailure.BadType,
                (long)PacketHeader.Read(new PacketReader(new byte[] { 1, 11, 0, 0, 0, 0, 0, 0 }), out header));
            SelfTest.Equal((long)DecodeFailure.Truncated,  // flags 合法但包头的 seq 不完整
                (long)PacketHeader.Read(new PacketReader(new byte[] { 1, (byte)PacketType.Command, 1, 0, 0, 0 }), out header));

            // §5.1 通道与类型映射：flags 与 type 不符即 BadValue（与 decodePacket 的 isFlagsValidForType 同表）。
            SelfTest.Equal((long)DecodeFailure.BadValue,
                (long)PacketHeader.Read(new PacketReader(new byte[] { 1, (byte)PacketType.Snapshot, 1, 0, 0, 0, 0, 0 }), out header));
            SelfTest.Equal((long)DecodeFailure.BadValue,
                (long)PacketHeader.Read(new PacketReader(new byte[] { 1, (byte)PacketType.Command, 0, 0, 0, 0, 0, 0 }), out header));
            SelfTest.Equal((long)DecodeFailure.BadValue,
                (long)PacketHeader.Read(new PacketReader(new byte[] { 1, (byte)PacketType.Fragment, 1, 0, 0, 0, 0, 0 }), out header));
            SelfTest.Equal((long)DecodeFailure.BadValue,
                (long)PacketHeader.Read(new PacketReader(new byte[] { 1, (byte)PacketType.KeepAlive, 1, 0, 0, 0, 0, 0 }), out header));
            SelfTest.Equal((long)DecodeFailure.Truncated,
                (long)PacketHeader.Read(new PacketReader(new byte[] { 1, (byte)PacketType.KeepAlive, 5, 0, 0, 0, 0, 0 }), out header));

            SelfTest.Equal((long)DecodeFailure.Truncated, (long)CommandCodec.Decode(null, out command));  // null 载荷不抛异常
            SelfTest.Equal((long)DecodeFailure.Truncated, (long)CommandCodec.Decode(new byte[13], out command));
            SelfTest.Equal((long)DecodeFailure.BadLength, (long)CommandCodec.Decode(new byte[15], out command));
            SelfTest.Equal((long)DecodeFailure.Ok, (long)CommandCodec.Decode(new byte[CommandCodec.PayloadSize], out command));

            SelfTest.Equal((long)DecodeFailure.UnknownEvent,
                (long)EventCodec.DecodeFrame(BuildEventFrame(100u, BuildRawEntry(1u, 11)), out frame));
            SelfTest.Equal((long)DecodeFailure.Truncated,
                (long)EventCodec.DecodeFrame(BuildEventFrame(100u, BuildRawEntry(1u, 1)), out frame));
            SelfTest.Equal((long)DecodeFailure.BadLength,
                (long)EventCodec.DecodeFrame(
                    Concat(BuildEventFrame(100u, PlayerHitEntry(1u, 3, 17, 25, 5, 120, 100, -250)), new byte[] { 0 }), out frame));

            var duplicate = PlayerHitEntry(1u, 3, 17, 25, 5, 120, 100, -250);
            SelfTest.Equal((long)DecodeFailure.Ok,
                (long)EventCodec.DecodeFrame(BuildEventFrame(100u, duplicate, duplicate), out frame));
            SelfTest.Equal(1, frame.Entries.Length);
            SelfTest.Equal(1, frame.DroppedDuplicates);

            // 跨帧幂等键（S03 §5.4）：同一个 tracker 下重复的 eventId 在第二帧被丢弃。
            var tracker = new EventIdTracker();
            SelfTest.Equal((long)DecodeFailure.Ok,
                (long)EventCodec.DecodeFrame(BuildEventFrame(100u, PlayerHitEntry(7u, 3, 17, 25, 5, 120, 100, -250)), tracker, out frame));
            SelfTest.Equal(1, frame.Entries.Length);
            SelfTest.Equal((long)DecodeFailure.Ok,
                (long)EventCodec.DecodeFrame(BuildEventFrame(101u, PlayerHitEntry(7u, 3, 17, 25, 5, 120, 100, -250)), tracker, out frame));
            SelfTest.Equal(0, frame.Entries.Length);
            SelfTest.Equal(1, frame.DroppedDuplicates);
            SelfTest.Equal(7, tracker.MaxEventId);

            var full = BuildSnapshotPayload(BuildEntityRecord(1, 0, 150, 0, -200, 16384, 0, 128, 0), new ushort[0],
                new byte[] { 0 });
            SelfTest.Equal((long)DecodeFailure.Ok, (long)SnapshotCodec.Decode(full, out snapshot));
            SelfTest.Equal(1, snapshot.Records.Length);
            SelfTest.True(snapshot.IsFull, "baselineTick=0 判为全量", "判为差分");

            SelfTest.Equal((long)DecodeFailure.BadLength,
                (long)SnapshotCodec.Decode(Concat(full, new byte[] { 0 }), out snapshot));
            SelfTest.Equal((long)DecodeFailure.Truncated,
                (long)SnapshotCodec.Decode(Slice(full, full.Length - 1), out snapshot));

            var tooManyRecords = (byte[])full.Clone();
            tooManyRecords[14] = 129;  // recordCount 在载荷偏移 14
            SelfTest.Equal((long)DecodeFailure.BadValue, (long)SnapshotCodec.Decode(tooManyRecords, out snapshot));

            SelfTest.Equal((long)DecodeFailure.BadValue,
                (long)SnapshotCodec.Decode(BuildSnapshotPayload(BuildEntityRecord(0, 0, 0, 0, 0, 0, 0, 0, 0), new ushort[0],
                    new byte[] { 0 }), out snapshot));

            SelfTest.Equal((long)DecodeFailure.BadValue,
                (long)SnapshotCodec.Decode(BuildSnapshotPayload(new byte[0], new ushort[] { 5, 3 }, new byte[] { 0 }),
                    out snapshot));
            SelfTest.Equal((long)DecodeFailure.BadValue,
                (long)SnapshotCodec.Decode(BuildSnapshotPayload(new byte[0], new ushort[] { 0 }, new byte[] { 0 }),
                    out snapshot));

            var matchState = BuildMatchState(1, 2, 3000,
                BuildPlayer(3, "ace", 1, 1),
                BuildPlayer(17, "玩家一", 2, 0));
            SelfTest.Equal((long)DecodeFailure.Ok, (long)MatchStateCodec.Decode(matchState, out state));
            SelfTest.Equal(2, state.Players.Length);
            SelfTest.Equal(3, state.Players[0].Pid);
            SelfTest.Equal("ace", state.Players[0].Name);
            SelfTest.True(state.Players[0].Ready, "ready=1 解为 true", "解为 false");
            SelfTest.Equal(1, state.Players[0].Weapon);
            SelfTest.Equal(12, state.Players[0].Mag);
            SelfTest.Equal(60, state.Players[0].Reserve);
            SelfTest.Equal(3, state.Players[0].Kills);
            SelfTest.Equal("玩家一", state.Players[1].Name);
            SelfTest.Equal(3000, state.IntermissionMs);

            SelfTest.Equal((long)DecodeFailure.BadValue,
                (long)MatchStateCodec.Decode(BuildMatchState(1, 1, 0,
                    BuildPlayer(1, "a", 0, 0), BuildPlayer(2, "b", 0, 0), BuildPlayer(3, "c", 0, 0),
                    BuildPlayer(4, "d", 0, 0), BuildPlayer(5, "e", 0, 0)), out state));

            SelfTest.Equal((long)DecodeFailure.BadValue,
                (long)MatchStateCodec.Decode(BuildMatchState(1, 1, 0,
                    BuildRawPlayer(1, new byte[] { (byte)'a' }, 13, 0, 0)), out state));
            SelfTest.Equal((long)DecodeFailure.BadValue,
                (long)MatchStateCodec.Decode(BuildMatchState(1, 1, 0, BuildRawPlayer(1, new byte[0], 0, 0, 0)), out state));
            SelfTest.Equal((long)DecodeFailure.BadValue,
                (long)MatchStateCodec.Decode(BuildMatchState(1, 1, 0, BuildPlayer(1, "a", 3, 0)), out state));
            SelfTest.Equal((long)DecodeFailure.BadValue,
                (long)MatchStateCodec.Decode(BuildMatchState(1, 1, 0,
                    BuildRawPlayer(1, new byte[] { 0xFF, 0xFF }, 2, 0, 0)), out state));
            SelfTest.Equal((long)DecodeFailure.BadLength,
                (long)MatchStateCodec.Decode(Concat(matchState, new byte[] { 0 }), out state));
            SelfTest.Equal((long)DecodeFailure.Truncated,
                (long)MatchStateCodec.Decode(Slice(matchState, matchState.Length - 1), out state));
        }

        // ---- 命令编码回环 -------------------------------------------------------------------

        private static void ChecksCommandRoundtrip()
        {
            var original = default(CommandPayload);
            original.MoveX = -127;
            original.MoveY = 64;
            original.Yaw = 0xABCD;
            original.Pitch = 32768;
            original.Buttons = (byte)(CommandButtons.Fire | CommandButtons.Ready);
            original.SwitchTo = (byte)WeaponSlot.Shotgun;
            original.Seq = 65535;
            original.ClientTick = 4294967295u;

            var encoded = CommandCodec.Encode(original);
            SelfTest.Equal(CommandCodec.PayloadSize, encoded.Length);

            CommandPayload decoded;
            SelfTest.Equal(0, (long)CommandCodec.Decode(encoded, out decoded));
            SelfTest.Equal(original.MoveX, decoded.MoveX);
            SelfTest.Equal(original.MoveY, decoded.MoveY);
            SelfTest.Equal(original.Yaw, decoded.Yaw);
            SelfTest.Equal(original.Pitch, decoded.Pitch);
            SelfTest.Equal(original.Buttons, decoded.Buttons);
            SelfTest.Equal(original.SwitchTo, decoded.SwitchTo);
            SelfTest.Equal(original.Seq, decoded.Seq);
            SelfTest.Equal(original.ClientTick, decoded.ClientTick);

            var illegal = (byte[])encoded.Clone();
            illegal[7] = 9;  // switchTo 越界
            SelfTest.Equal(0, (long)CommandCodec.Decode(illegal, out decoded));
            SelfTest.Equal(0, decoded.SwitchTo);

            // 写端口径：轴与角度先经 §5.3 的量化。
            SelfTest.Equal(127, Quantize.QuantizeAxis(1.0));
            SelfTest.Equal(10430, Quantize.QuantizeAngle(1.0));
        }

        // ---- fixture 读取与断言工具 ----------------------------------------------------------

        private sealed class HexFixture
        {
            internal byte[] Bytes;
            internal readonly Dictionary<string, long> Expect = new Dictionary<string, long>();
        }

        private static string FixtureDirectory()
        {
            var dataParent = Path.GetDirectoryName(Application.dataPath);
            if (!string.IsNullOrEmpty(dataParent))
            {
                var candidate = Path.GetFullPath(Path.Combine(dataParent, "..", "server", "tests", "fixtures"));
                if (Directory.Exists(candidate)) return candidate;
            }
            return Path.GetFullPath(Path.Combine("server", "tests", "fixtures"));
        }

        private static HexFixture ReadFixture(string directory, string name)
        {
            var path = Path.Combine(directory, name + ".hex");
            SelfTest.True(File.Exists(path), "共享字节 fixture " + name + ".hex 存在", path);
            var fixture = new HexFixture();
            var bytes = new List<byte>();
            foreach (var raw in File.ReadAllLines(path))
            {
                var line = raw.Trim();
                if (line.Length == 0) continue;
                if (line[0] == '#')
                {
                    const string marker = "# expect:";
                    var index = line.IndexOf(marker, StringComparison.Ordinal);
                    if (index < 0) continue;
                    foreach (var pair in line.Substring(index + marker.Length).Split(','))
                    {
                        var parts = pair.Split('=');
                        if (parts.Length != 2) continue;
                        fixture.Expect[parts[0].Trim()] = ParseNumber(parts[1].Trim());
                    }
                    continue;
                }
                foreach (var token in line.Split(' '))
                {
                    if (token.Length > 0) bytes.Add(Convert.ToByte(token, 16));
                }
            }
            fixture.Bytes = bytes.ToArray();
            return fixture;
        }

        private static long ParseNumber(string text)
        {
            return text.StartsWith("0x", StringComparison.OrdinalIgnoreCase)
                ? Convert.ToInt64(text.Substring(2), 16)
                : long.Parse(text, CultureInfo.InvariantCulture);
        }

        private static void CheckField(Dictionary<string, long> expect, string key, long actual)
        {
            long value;
            if (!expect.TryGetValue(key, out value)) return;
            expect.Remove(key);
            SelfTest.Equal(value, actual);
        }

        private static long ParseAsciiHex(byte[] bytes)
        {
            return Convert.ToInt64(Encoding.ASCII.GetString(bytes), 16);
        }

        private static byte[] Slice(byte[] source, int offset)
        {
            var result = new byte[source.Length - offset];
            Array.Copy(source, offset, result, 0, result.Length);
            return result;
        }

        private static byte[] Concat(byte[] left, byte[] right)
        {
            var result = new byte[left.Length + right.Length];
            Array.Copy(left, 0, result, 0, left.Length);
            Array.Copy(right, 0, result, left.Length, right.Length);
            return result;
        }

        // ---- 字节构造工具 -------------------------------------------------------------------

        private sealed class ByteWriter
        {
            private readonly List<byte> _bytes = new List<byte>();

            internal void U8(byte value) { _bytes.Add(value); }
            internal void I8(sbyte value) { _bytes.Add(unchecked((byte)value)); }

            internal void U16(ushort value)
            {
                _bytes.Add((byte)(value & 0xFF));
                _bytes.Add((byte)(value >> 8));
            }

            internal void I16(short value) { U16(unchecked((ushort)value)); }

            internal void U32(uint value)
            {
                _bytes.Add((byte)(value & 0xFF));
                _bytes.Add((byte)((value >> 8) & 0xFF));
                _bytes.Add((byte)((value >> 16) & 0xFF));
                _bytes.Add((byte)((value >> 24) & 0xFF));
            }

            internal void Bytes(byte[] value) { _bytes.AddRange(value); }

            internal byte[] ToArray() { return _bytes.ToArray(); }
        }

        private static byte[] BuildRawEntry(uint eventId, byte type)
        {
            var writer = new ByteWriter();
            writer.U32(eventId);
            writer.U8(type);
            return writer.ToArray();
        }

        private static byte[] PlayerHitEntry(uint eventId, ushort subjectId, ushort targetId, ushort value, byte flags,
            short hitX, short hitY, short hitZ)
        {
            var writer = new ByteWriter();
            writer.U32(eventId);
            writer.U8((byte)Ac.Net.EventType.PlayerHit);  // UnityEngine 也有 EventType，必须限定
            writer.U16(subjectId);
            writer.U16(targetId);
            writer.U16(value);
            writer.U8(flags);
            writer.I16(hitX);
            writer.I16(hitY);
            writer.I16(hitZ);
            return writer.ToArray();
        }

        private static byte[] BuildEventFrame(uint tick, params byte[][] entries)
        {
            var writer = new ByteWriter();
            writer.U32(tick);
            writer.U8((byte)entries.Length);
            foreach (var entry in entries) writer.Bytes(entry);
            return writer.ToArray();
        }

        private static byte[] BuildEntityRecord(ushort id, byte kindFlags, short xCm, short yCm, short zCm,
            ushort yawUnits, ushort pitchUnits, byte hpRatioUnits, byte state)
        {
            var writer = new ByteWriter();
            writer.U16(id);
            writer.U8(kindFlags);
            writer.I16(xCm);
            writer.I16(yCm);
            writer.I16(zCm);
            writer.U16(yawUnits);
            writer.U16(pitchUnits);
            writer.U8(hpRatioUnits);
            writer.U8(state);
            return writer.ToArray();
        }

        private static byte[] BuildSnapshotPayload(byte[] records, ushort[] removedIds, byte[] eventBlock)
        {
            var writer = new ByteWriter();
            writer.U32(100u);
            writer.U32(5000u);
            writer.U16(7);
            writer.U32(0u);
            writer.U8((byte)(records.Length / SnapshotCodec.RecordSize));
            writer.Bytes(records);
            writer.U8((byte)removedIds.Length);
            foreach (var id in removedIds) writer.U16(id);
            writer.Bytes(eventBlock);
            return writer.ToArray();
        }

        private static byte[] BuildMatchState(byte phase, byte wave, ushort intermissionMs, params byte[][] players)
        {
            var writer = new ByteWriter();
            writer.U8(phase);
            writer.U8(wave);
            writer.U16(intermissionMs);
            writer.U8((byte)players.Length);
            foreach (var player in players) writer.Bytes(player);
            return writer.ToArray();
        }

        private static byte[] BuildPlayer(ushort pid, string name, byte weapon, byte ready, byte downed = 0)
        {
            var nameBytes = Encoding.UTF8.GetBytes(name);
            return BuildRawPlayer(pid, nameBytes, (byte)nameBytes.Length, weapon, ready, downed);
        }

        private static byte[] BuildRawPlayer(ushort pid, byte[] nameBytes, byte declaredLength, byte weapon, byte ready,
            byte downed = 0)
        {
            var writer = new ByteWriter();
            writer.U16(pid);
            writer.U8(declaredLength);
            writer.Bytes(nameBytes);
            writer.U8(ready);
            writer.U8(weapon);
            writer.U8(128);
            writer.U16(3);
            writer.U8(12);
            writer.U16(60);
            writer.U8(0);
            writer.U8(0);
            writer.U8(0);
            writer.U8(downed);
            writer.U8(0);
            return writer.ToArray();
        }
    }
}
