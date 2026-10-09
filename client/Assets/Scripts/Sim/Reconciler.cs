namespace Ac.Sim
{
    // C06 §5(d)：对账用的本机权威量。位置/朝向来自快照本机实体，lastAckedSeq 来自该帧的通用字段。
    public struct LocalAuthority
    {
        public bool Found;
        public double X;
        public double Y;
        public double Z;
        public double YawRad;
        public double PitchRad;
        public ushort YawUnits;
        public ushort PitchUnits;
        public ushort LastAckedSeq;
        public uint Tick;

        public static bool TryProject(SnapshotView view, out LocalAuthority authority)
        {
            authority = default(LocalAuthority);
            if (view == null) return false;
            SnapshotFrame newest;
            if (!view.TryGetFrame(0, out newest)) return false;
            FrameEntity entity;
            if (!view.TryGetLocalAuthority(out entity)) return false;
            authority.Found = true;
            authority.X = Quantize.DequantizePosition(entity.XCm);
            authority.Y = Quantize.DequantizePosition(entity.YCm);
            authority.Z = Quantize.DequantizePosition(entity.ZCm);
            authority.YawUnits = entity.YawUnits;
            authority.PitchUnits = entity.PitchUnits;
            authority.YawRad = Quantize.DequantizeAngle(entity.YawUnits);
            authority.PitchRad = Quantize.DequantizeAngle(entity.PitchUnits);
            authority.LastAckedSeq = newest.LastAckedSeq;
            authority.Tick = newest.Tick;
            return true;
        }
    }

    public struct ReconcileResult
    {
        public int Replayed;
        public double ErrorM;
        public double OffsetX;
        public double OffsetY;
        public double OffsetZ;
        public bool HardCorrect;
    }

    // C06 §5(b)：回滚重放与误差判定。平滑器在 View 层（C04 的 ErrorSmoother），Sim 层只给误差与偏移。
    public sealed class Reconciler
    {
        public const double HardCorrectThresholdM = 1.0;

        public int HardCorrectCount { get; private set; }
        public double LastErrorM { get; private set; }
        public double MaxErrorM { get; private set; }

        public ReconcileResult Reconcile(in LocalAuthority authority, CommandBuffer buffer, Predictor predictor)
        {
            var result = default(ReconcileResult);
            if (!authority.Found || buffer == null || predictor == null) return result;

            var beforeX = predictor.State.X;
            var beforeY = predictor.State.Y;
            var beforeZ = predictor.State.Z;

            // 残留①③修复：不再回滚重放，改为与「投影到客户端当前预测网格的权威位置」对账。
            // 权威快照是服务端 tick T 时刻（T−1 步）的位置，客户端此刻已经预测到 P_now 步；
            // 直接拿它当基线，相位级的 0/1 子步差（0.005/0.225m 交替）就每次都进误差 ⇒ 平滑器
            // 被周期性灌偏移，移动画面来回抖。调用方（GameLoop ③）按步数差把权威外推到客户端
            // 当前网格（本地速度 × (P_now−(T−1))×50ms）：恒定移动时投影位置 ≡ 本地预测位置 ⇒
            // 误差恒 ≈ 0（丢包也一样——服务端持最近命令，丢包不缺步）；输入真的变了（转向/变速）
            // 才出现误差，该纠正的纠正。重放被投影取代：权威已外推到当前网格，缓冲里的步已含在
            // 投影里，再重放就是双重计数；而按区间重放（含 ack 裁剪）在到达抖动下仍有 0/1 子步
            // 相位残差（实测 29mm）。缓冲照旧按子步入队（C06 §5(b)），这里清空即可。
            predictor.SetAuthoritative(authority.X, authority.Y, authority.Z, authority.YawUnits, authority.PitchUnits);
            buffer.Reset();

            var dx = beforeX - predictor.State.X;
            var dy = beforeY - predictor.State.Y;
            var dz = beforeZ - predictor.State.Z;
            result.OffsetX = dx;
            result.OffsetY = dy;
            result.OffsetZ = dz;
            result.ErrorM = System.Math.Sqrt(dx * dx + dy * dy + dz * dz);
            result.HardCorrect = result.ErrorM > HardCorrectThresholdM;

            LastErrorM = result.ErrorM;
            if (result.ErrorM > MaxErrorM) MaxErrorM = result.ErrorM;
            if (result.HardCorrect) HardCorrectCount += 1;
            return result;
        }

        public void Reset()
        {
            HardCorrectCount = 0;
            LastErrorM = 0.0;
            MaxErrorM = 0.0;
        }

        // §4 第 7 条：调试日志行（字符串形式；Ac.Sim 不引 UnityEngine，真正的 Debug.Log 落点在后续步骤）。
        public string DebugLine(double rejectedTotal)
        {
            return "reconcile lastErrM=" + LastErrorM.ToString("F4") + " maxErrM=" + MaxErrorM.ToString("F4")
                + " hardCorrect=" + HardCorrectCount + " rejected=" + rejectedTotal;
        }
    }
}
