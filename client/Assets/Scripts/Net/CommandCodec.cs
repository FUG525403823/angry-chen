namespace Ac.Net
{
    // C02 §5.2 / S03 §5.2 的按钮位域（写端 & 0xff）。
    [System.Flags]
    public enum CommandButtons : byte
    {
        None = 0,
        Fire = 1,
        Sprint = 2,
        Jump = 4,
        Reload = 8,
        Interact = 16,
        Rage = 32,
        SwitchWeapon = 64,
        Ready = 128,
    }

    public enum WeaponSlot : byte
    {
        Pistol = 0,
        Rifle = 1,
        Shotgun = 2,
    }

    public struct CommandPayload
    {
        public sbyte MoveX;
        public sbyte MoveY;
        public ushort Yaw;
        public ushort Pitch;
        public byte Buttons;
        public byte SwitchTo;
        public ushort Seq;
        public uint ClientTick;

        public CommandButtons ButtonFlags { get { return (CommandButtons)Buttons; } }
    }

    // C02 §5.2 命令载荷（type=4，固定 14 字节）。
    public static class CommandCodec
    {
        public const int PayloadSize = 14;

        public static DecodeFailure Decode(byte[] payload, out CommandPayload command)
        {
            command = default(CommandPayload);
            var reader = new PacketReader(payload);
            // 短于契约 = 帧被截断（共享 fixture truncated_command.hex 期望 Truncated）；
            // 长于契约 = 长度违约。两者都不是合法帧，但拒绝原因必须区分。
            if (reader.Remaining < PayloadSize) return DecodeFailure.Truncated;
            if (reader.Remaining > PayloadSize) return DecodeFailure.BadLength;

            reader.TryReadI8(out command.MoveX);
            reader.TryReadI8(out command.MoveY);
            reader.TryReadU16(out command.Yaw);
            reader.TryReadU16(out command.Pitch);
            reader.TryReadU8(out command.Buttons);
            reader.TryReadU8(out command.SwitchTo);
            reader.TryReadU16(out command.Seq);
            reader.TryReadU32(out command.ClientTick);

            // 非法的武器槽回落 0（手枪），不判失败：这是 §5.2 冻结的写端口径。
            if (command.SwitchTo > (byte)WeaponSlot.Shotgun) command.SwitchTo = (byte)WeaponSlot.Pistol;
            return DecodeFailure.Ok;
        }

        // 命令编码侧：供回环测试与 C03 的发送路径；量化由调用方经 Quantize 完成。
        public static byte[] Encode(CommandPayload command)
        {
            var payload = new byte[PayloadSize];
            payload[0] = unchecked((byte)command.MoveX);
            payload[1] = unchecked((byte)command.MoveY);
            payload[2] = (byte)(command.Yaw & 0xFF);
            payload[3] = (byte)(command.Yaw >> 8);
            payload[4] = (byte)(command.Pitch & 0xFF);
            payload[5] = (byte)(command.Pitch >> 8);
            payload[6] = command.Buttons;
            payload[7] = command.SwitchTo;
            payload[8] = (byte)(command.Seq & 0xFF);
            payload[9] = (byte)(command.Seq >> 8);
            payload[10] = (byte)(command.ClientTick & 0xFF);
            payload[11] = (byte)((command.ClientTick >> 8) & 0xFF);
            payload[12] = (byte)((command.ClientTick >> 16) & 0xFF);
            payload[13] = (byte)((command.ClientTick >> 24) & 0xFF);
            return payload;
        }
    }
}
