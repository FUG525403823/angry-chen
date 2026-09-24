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

            // 非法的武器槽回落 0（手枪），不判失败：§5.2 对读写两侧只定一条规则。
            if (command.SwitchTo > (byte)WeaponSlot.Shotgun) command.SwitchTo = (byte)WeaponSlot.Pistol;
            return DecodeFailure.Ok;
        }

        // C05 §9：Ac.Core 的采样结果 → 命令载荷（Ac.Core 不能引用 Ac.Net，映射放在这一侧）。
        public static CommandPayload IntentToPayload(in global::Ac.Core.InputIntent intent)
        {
            var command = default(CommandPayload);
            command.MoveX = intent.MoveX;
            command.MoveY = intent.MoveY;
            command.Yaw = intent.Yaw;
            command.Pitch = intent.Pitch;
            command.Buttons = intent.Buttons;
            command.SwitchTo = intent.SwitchTo;
            command.Seq = intent.Seq;
            command.ClientTick = intent.ClientTick;
            return command;
        }

        // C05 §9：命令载荷 → Ac.Sim 的步进视图（本地预测与权威模拟共用同一套语义）。
        public static global::Ac.Sim.StepCommand ToStepCommand(in CommandPayload command)
        {
            var step = default(global::Ac.Sim.StepCommand);
            step.MoveX = command.MoveX;
            step.MoveY = command.MoveY;
            step.Yaw = command.Yaw;
            step.Pitch = command.Pitch;
            step.Buttons = command.Buttons;
            return step;
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
            // 写侧同样回落，保证 Encode 产出的字节一定落在 §5.2 的合法域内。
            payload[7] = command.SwitchTo > (byte)WeaponSlot.Shotgun ? (byte)WeaponSlot.Pistol : command.SwitchTo;
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
