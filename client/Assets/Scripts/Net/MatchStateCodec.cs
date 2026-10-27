using System;
using System.Text;

namespace Ac.Net
{
    public struct MatchStatePlayer
    {
        public ushort Pid;
        public string Name;
        public bool Ready;
        public byte Weapon;
        public byte HpRatio;
        public ushort Kills;
        public byte Mag;
        public ushort Reserve;
        public byte ReloadLeft10Ms;
        public byte Rage;
        public byte RageLeft100Ms;
        public bool Downed;
        public byte ReviveRatio255;
        // S16：波次升级（0..5 / points 0..255）。
        public byte UpgradePoints;
        public byte UpgradeDamage;
        public byte UpgradeSpeed;
        public byte UpgradeReload;
        public byte UpgradeReserve;
    }

    public struct MatchStatePayload
    {
        public byte Phase;
        public byte Wave;
        public ushort IntermissionMs;
        public MatchStatePlayer[] Players;
        public ushort LocalPid;
    }

    // C02 §5.2 / S03 §5.7 MatchState（type=10，可靠单播）。
    public static class MatchStateCodec
    {
        public const int MaxPlayers = 4;
        public const int MinNameBytes = 1;
        public const int MaxNameBytes = 12;
        // 名称之外的定长部分：pid u16 + nameLen u8 + ready/weapon/hp/kills/mag/reserve/reload/rage/rageLeft/downed/revive
        // + 升级段（points + damage/speed/reload/reserve 等级，S16）。
        // 与 server/src/net/codec.hpp 的 kMatchStatePlayerFixedBytes 同值（21），两侧必须一起改。
        public const int FixedRecordBytes = 21;

        public static DecodeFailure Decode(byte[] payload, out MatchStatePayload state)
        {
            state = default(MatchStatePayload);
            return new MatchStateDecoder().Decode(payload, default(MatchStatePayload), out state);
        }
    }

    // Scratch belongs to the decoder; published player arrays are immutable snapshots.
    public sealed class MatchStateDecoder
    {
        private readonly PacketReader _reader = new PacketReader(null);
        private readonly MatchStatePlayer[] _scratch = new MatchStatePlayer[MatchStateCodec.MaxPlayers];
        private readonly char[] _name = new char[MatchStateCodec.MaxNameBytes];
        private static readonly UTF8Encoding _strictUtf8 = new UTF8Encoding(false, true);

        public DecodeFailure Decode(byte[] payload, MatchStatePayload previous, out MatchStatePayload state)
        {
            state = default(MatchStatePayload);
            var reader = _reader;
            reader.Reset(payload);
            if (!reader.TryReadU8(out state.Phase)) return DecodeFailure.Truncated;
            if (!reader.TryReadU8(out state.Wave)) return DecodeFailure.Truncated;
            if (!reader.TryReadU16(out state.IntermissionMs)) return DecodeFailure.Truncated;

            byte count;
            if (!reader.TryReadU8(out count)) return DecodeFailure.Truncated;
            if (count > MatchStateCodec.MaxPlayers) return DecodeFailure.BadValue;

            var players = _scratch;
            for (var i = 0; i < count; i++)
            {
                MatchStatePlayer player;
                if (!reader.TryReadU16(out player.Pid)) return DecodeFailure.Truncated;

                byte nameLength;
                if (!reader.TryReadU8(out nameLength)) return DecodeFailure.Truncated;
                if (nameLength < MatchStateCodec.MinNameBytes || nameLength > MatchStateCodec.MaxNameBytes) return DecodeFailure.BadValue;

                var nameOffset = reader.Position;
                if (!reader.TrySkip(nameLength)) return DecodeFailure.Truncated;
                try
                {
                    var length = _strictUtf8.GetChars(payload, nameOffset, nameLength, _name, 0);
                    var cached = previous.Players != null && i < previous.Players.Length ? previous.Players[i].Name : null;
                    var same = cached != null && cached.Length == length;
                    for (var c = 0; same && c < length; c++) same = cached[c] == _name[c];
                    player.Name = same ? cached : new string(_name, 0, length);
                }
                catch (ArgumentException)
                {
                    return DecodeFailure.BadValue;  // 名称不是合法 UTF-8
                }

                byte ready;
                if (!reader.TryReadU8(out ready)) return DecodeFailure.Truncated;
                player.Ready = ready != 0;

                if (!reader.TryReadU8(out player.Weapon)) return DecodeFailure.Truncated;
                if (player.Weapon > (byte)WeaponSlot.Shotgun) return DecodeFailure.BadValue;

                if (!reader.TryReadU8(out player.HpRatio)) return DecodeFailure.Truncated;
                if (!reader.TryReadU16(out player.Kills)) return DecodeFailure.Truncated;
                if (!reader.TryReadU8(out player.Mag)) return DecodeFailure.Truncated;
                if (!reader.TryReadU16(out player.Reserve)) return DecodeFailure.Truncated;
                if (!reader.TryReadU8(out player.ReloadLeft10Ms)) return DecodeFailure.Truncated;
                if (!reader.TryReadU8(out player.Rage)) return DecodeFailure.Truncated;
                if (!reader.TryReadU8(out player.RageLeft100Ms)) return DecodeFailure.Truncated;

                byte downed;
                if (!reader.TryReadU8(out downed)) return DecodeFailure.Truncated;
                player.Downed = downed != 0;

                if (!reader.TryReadU8(out player.ReviveRatio255)) return DecodeFailure.Truncated;
                // S16：升级段 5 字节（points + 4 个等级），等级上限与 kUpgradeMaxLevel 一致。
                if (!reader.TryReadU8(out player.UpgradePoints)) return DecodeFailure.Truncated;
                if (!reader.TryReadU8(out player.UpgradeDamage)) return DecodeFailure.Truncated;
                if (!reader.TryReadU8(out player.UpgradeSpeed)) return DecodeFailure.Truncated;
                if (!reader.TryReadU8(out player.UpgradeReload)) return DecodeFailure.Truncated;
                if (!reader.TryReadU8(out player.UpgradeReserve)) return DecodeFailure.Truncated;
                if (player.UpgradeDamage > Sim.UpgradeTable.MaxLevel || player.UpgradeSpeed > Sim.UpgradeTable.MaxLevel ||
                    player.UpgradeReload > Sim.UpgradeTable.MaxLevel || player.UpgradeReserve > Sim.UpgradeTable.MaxLevel)
                {
                    return DecodeFailure.BadValue;
                }
                players[i] = player;
            }

            if (!reader.TryReadU16(out state.LocalPid)) return DecodeFailure.Truncated;
            if (reader.Remaining != 0) return DecodeFailure.BadLength;
            if (state.LocalPid != 0)
            {
                var matches = 0;
                for (var i = 0; i < count; i++)
                    if (players[i].Pid == state.LocalPid) matches += 1;
                if (matches != 1) return DecodeFailure.BadValue;
            }
            var unchanged = previous.Players != null && previous.Players.Length == count;
            for (var i = 0; unchanged && i < count; i++) unchanged = Same(players[i], previous.Players[i]);
            if (unchanged) state.Players = previous.Players;
            else
            {
                state.Players = new MatchStatePlayer[count];
                Array.Copy(players, state.Players, count);
            }
            return DecodeFailure.Ok;
        }
        private static bool Same(MatchStatePlayer a, MatchStatePlayer b)
        {
            return a.Pid == b.Pid
                && a.Name == b.Name
                && a.Ready == b.Ready
                && a.Weapon == b.Weapon
                && a.HpRatio == b.HpRatio
                && a.Kills == b.Kills
                && a.Mag == b.Mag
                && a.Reserve == b.Reserve
                && a.ReloadLeft10Ms == b.ReloadLeft10Ms
                && a.Rage == b.Rage
                && a.RageLeft100Ms == b.RageLeft100Ms
                && a.Downed == b.Downed
                && a.ReviveRatio255 == b.ReviveRatio255
                && a.UpgradePoints == b.UpgradePoints
                && a.UpgradeDamage == b.UpgradeDamage
                && a.UpgradeSpeed == b.UpgradeSpeed
                && a.UpgradeReload == b.UpgradeReload
                && a.UpgradeReserve == b.UpgradeReserve;
        }
    }
}
