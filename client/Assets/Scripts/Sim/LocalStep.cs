namespace Ac.Sim
{
    // C05 §5.3：局部步进消费的命令视图。Ac.Sim 按 ContractSuite 白名单只能引用 Ac.Core，
    // 所以 Net 层的 `CommandPayload` 由 `CommandCodec.ToStepCommand` 映射过来。
    public struct StepCommand
    {
        public sbyte MoveX;
        public sbyte MoveY;
        public ushort Yaw;
        public ushort Pitch;
        public byte Buttons;
    }

    // C05 §5.3 的移动参数（与 S06 的 `config/player.hpp`、`config/arena.hpp` 逐条同值）。
    public struct MoveConfig
    {
        public double MoveSpeedMps;
        public double SprintSpeedMps;
        public double RadiusMeters;
        public double HalfSizeMeters;
        public double FenceHalfThicknessMeters;
        public double BarnMinX;
        public double BarnMaxX;
        public double BarnMinZ;
        public double BarnMaxZ;
        public double BarnMaxY;

        public static MoveConfig Default()
        {
            MoveConfig config;
            config.MoveSpeedMps = 4.5;
            config.SprintSpeedMps = 6.3;
            config.RadiusMeters = 0.4;
            config.HalfSizeMeters = 40.0;
            config.FenceHalfThicknessMeters = 0.25;
            config.BarnMinX = -4.0;
            config.BarnMaxX = 4.0;
            config.BarnMinZ = -4.0;
            config.BarnMaxZ = 4.0;
            config.BarnMaxY = 5.0;
            return config;
        }
    }

    // 权威模拟里的移动状态子集（S06 的 MoveState）。
    public struct MoveState
    {
        public double X;
        public double Y;
        public double Z;
        public double Vx;
        public double Vy;
        public double Vz;
        public double YawRad;
        public double PitchRad;
    }

    // C05 §5.3 / S06 §5.2：与服务端逐字同序的局部步进。
    // 只用 + - * / 与 sqrt（ADR-010 允许的运算子集），角度一律经 TrigTable 取得。
    public static class LocalStep
    {
        public const uint StepDtMs = 50;
        public const byte ButtonSprint = 2;

        // 命令的 moveX/moveY 是 §5.2 的 i8 轴（±127 = ±1），这里按服务端 decode 的口径反量化。
        public static bool StepLocalPlayer(ref MoveState state, in StepCommand command, in MoveConfig config, uint dtMs)
        {
            if (dtMs != StepDtMs) return false;

            state.YawRad = Quantize.DequantizeAngle(command.Yaw);
            state.PitchRad = Quantize.DequantizeAngle(command.Pitch);

            var moveX = Quantize.DequantizeAxis(command.MoveX);
            var moveY = Quantize.DequantizeAxis(command.MoveY);
            var speed = (command.Buttons & ButtonSprint) != 0 ? config.SprintSpeedMps : config.MoveSpeedMps;

            var yawUnits = Quantize.QuantizeAngle(state.YawRad);
            var fx = TrigTable.Shared.Sin(yawUnits);
            var fz = TrigTable.Shared.Cos(yawUnits);
            var rx = fz;
            var rz = -fx;
            var vx = fx * moveX + rx * moveY;
            var vz = fz * moveX + rz * moveY;
            var magSq = vx * vx + vz * vz;
            if (magSq > 1.0)
            {
                var inv = 1.0 / System.Math.Sqrt(magSq);
                vx *= inv;
                vz *= inv;
            }

            state.Vx = vx * speed;
            state.Vy = 0.0;
            state.Vz = vz * speed;

            var dtSeconds = dtMs / 1000.0;
            state.X += state.Vx * dtSeconds;
            state.Y += state.Vy * dtSeconds;
            state.Z += state.Vz * dtSeconds;

            PushOutOfBarn(ref state, config);
            ClampToFence(ref state, config);
            return true;
        }

        // §5.3 碰撞一：pos.y >= 5 跳过；按半径外扩的 AABB 内部才处理，贴最近的面并把该轴速度清零。
        private static void PushOutOfBarn(ref MoveState state, in MoveConfig config)
        {
            if (state.Y >= config.BarnMaxY) return;
            var minX = config.BarnMinX - config.RadiusMeters;
            var maxX = config.BarnMaxX + config.RadiusMeters;
            var minZ = config.BarnMinZ - config.RadiusMeters;
            var maxZ = config.BarnMaxZ + config.RadiusMeters;
            if (state.X <= minX || state.X >= maxX || state.Z <= minZ || state.Z >= maxZ) return;

            var pushLeft = state.X - minX;
            var pushRight = maxX - state.X;
            var pushBack = state.Z - minZ;
            var pushForward = maxZ - state.Z;
            if (System.Math.Min(pushLeft, pushRight) <= System.Math.Min(pushBack, pushForward))
            {
                state.X = pushLeft < pushRight ? minX : maxX;
                state.Vx = 0.0;
            }
            else
            {
                state.Z = pushBack < pushForward ? minZ : maxZ;
                state.Vz = 0.0;
            }
        }

        // §5.3 碰撞二：limit = halfSize - thickness/2 - radius，越界夹到 limit 并清零该轴速度。
        private static void ClampToFence(ref MoveState state, in MoveConfig config)
        {
            var limit = config.HalfSizeMeters - config.FenceHalfThicknessMeters - config.RadiusMeters;
            if (state.X < -limit)
            {
                state.X = -limit;
                state.Vx = 0.0;
            }
            else if (state.X > limit)
            {
                state.X = limit;
                state.Vx = 0.0;
            }
            if (state.Z < -limit)
            {
                state.Z = -limit;
                state.Vz = 0.0;
            }
            else if (state.Z > limit)
            {
                state.Z = limit;
                state.Vz = 0.0;
            }
        }
    }
}
