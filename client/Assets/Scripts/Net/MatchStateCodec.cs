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

        private static readonly UTF8Encoding _strictUtf8 = new UTF8Encoding(false, true);

        public static DecodeFailure Decode(byte[] payload, out MatchStatePayload state)
        {
            state = default(MatchStatePayload);
            var reader = new PacketReader(payload);
            if (!reader.TryReadU8(out state.Phase)) return DecodeFailure.Truncated;
            if (!reader.TryReadU8(out state.Wave)) return DecodeFailure.Truncated;
            if (!reader.TryReadU16(out state.IntermissionMs)) return DecodeFailure.Truncated;

            byte count;
            if (!reader.TryReadU8(out count)) return DecodeFailure.Truncated;
            if (count > MaxPlayers) return DecodeFailure.BadValue;

            var players = new MatchStatePlayer[count];
            for (var i = 0; i < count; i++)
            {
                MatchStatePlayer player;
                if (!reader.TryReadU16(out player.Pid)) return DecodeFailure.Truncated;

                byte nameLength;
                if (!reader.TryReadU8(out nameLength)) return DecodeFailure.Truncated;
                if (nameLength < MinNameBytes || nameLength > MaxNameBytes) return DecodeFailure.BadValue;

                byte[] nameBytes;
                if (!reader.TryReadBytes(nameLength, out nameBytes)) return DecodeFailure.Truncated;
                try
                {
                    player.Name = _strictUtf8.GetString(nameBytes);
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
                for (var i = 0; i < players.Length; i++)
                    if (players[i].Pid == state.LocalPid) matches += 1;
                if (matches != 1) return DecodeFailure.BadValue;
            }
            state.Players = players;
            return DecodeFailure.Ok;
        }
    }
}
