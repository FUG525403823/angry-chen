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
    }

    public struct MatchStatePayload
    {
        public byte Phase;
        public byte Wave;
        public ushort IntermissionMs;
        public MatchStatePlayer[] Players;
    }

    // C02 §5.2 / S03 §5.7 MatchState（type=10，可靠单播）。
    public static class MatchStateCodec
    {
        public const int MaxPlayers = 4;
        public const int MinNameBytes = 1;
        public const int MaxNameBytes = 12;
        public const int FixedRecordBytes = 13;  // 名称之外的定长部分

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
                players[i] = player;
            }

            if (reader.Remaining != 0) return DecodeFailure.BadLength;
            state.Players = players;
            return DecodeFailure.Ok;
        }
    }
}
