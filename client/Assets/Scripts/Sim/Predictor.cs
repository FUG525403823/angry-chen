namespace Ac.Sim
{
    // C05 §5.4：预测骨架。50ms 定长子步进 + 渲染外推；和解重放留给后续计划。
    public sealed class Predictor
    {
        public const int SubstepMs = (int)LocalStep.StepDtMs;   // 与服务端 tick 同源，不允许出现第二个 50
        public const int MaxSubstepsPerFrame = 5;
        public const int MaxAccumulatorMs = 50;

        private MoveConfig _config;
        private int _accumulatorMs;

        public Predictor()
        {
            _config = MoveConfig.Default();
            State = new MoveState();
        }

        public MoveState State { get; private set; }
        public int AccumulatorMs { get { return _accumulatorMs; } }
        public int Steps { get; private set; }
        public int DroppedSubsteps { get; private set; }

        public void SetConfig(in MoveConfig config) { _config = config; }

        // §9：单命令步进（后续计划的和解重放共用这一条路径）。
        public bool StepWith(in StepCommand command)
        {
            var state = State;
            if (!LocalStep.StepLocalPlayer(ref state, command, _config, SubstepMs)) return false;
            State = state;
            Steps += 1;
            return true;
        }

        // §5.4：累加 dt，按 50ms 出步；一帧最多 5 步，超出丢弃余量并计 droppedSubsteps；
        // 余量上限 50ms（只保留一个子步），避免追帧雪崩。
        public int Advance(int dtMs, in StepCommand command)
        {
            if (dtMs <= 0) return 0;
            _accumulatorMs += dtMs;
            var stepped = 0;
            while (_accumulatorMs >= SubstepMs && stepped < MaxSubstepsPerFrame)
            {
                _accumulatorMs -= SubstepMs;
                StepWith(command);
                stepped += 1;
            }
            if (_accumulatorMs >= SubstepMs)
            {
                var dropped = _accumulatorMs / SubstepMs;
                DroppedSubsteps += dropped;
                _accumulatorMs = MaxAccumulatorMs;
            }
            return stepped;
        }

        // §5.4：renderPos = state.pos + state.vel * (accumulatorMs / 1000)
        public void RenderPosition(out double x, out double y, out double z)
        {
            var t = _accumulatorMs / 1000.0;
            x = State.X + State.Vx * t;
            y = State.Y + State.Vy * t;
            z = State.Z + State.Vz * t;
        }

        // 权威姿态写入（和解重放留给后续计划，这里只提供基线）。
        public void SetAuthoritative(double x, double y, double z, ushort yawUnits, ushort pitchUnits)
        {
            var state = State;
            state.X = x;
            state.Y = y;
            state.Z = z;
            state.YawRad = Quantize.DequantizeAngle(yawUnits);
            state.PitchRad = Quantize.DequantizeAngle(pitchUnits);
            State = state;
        }
    }
}
