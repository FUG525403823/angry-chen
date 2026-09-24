using System;
using Ac.Net;
using Ac.Sim;

namespace Ac.View
{
    // C04 §5.4：一个远端/本地实体的视图对象。本份只承载姿态与表现状态，绑 GameObject 留到 C05。
    public sealed class EntityView
    {
        public ushort Id;
        public byte Kind;
        public bool Visible;
        public double X;
        public double Y;
        public double Z;
        public double YawRad;
        public double PitchRad;
        public double HpRatio;
        public byte State;
        public byte Flags;
        public readonly ErrorSmoother Smoother = new ErrorSmoother();
        public bool HasPrediction;
        public double PredictedX;
        public double PredictedY;
        public double PredictedZ;
        public double PredictedYawRad;
        public double PredictedPitchRad;

        // 渲染变换 = 姿态 + 平滑偏移（§5.5：偏移只作用于渲染，不写回姿态）。
        public double RenderX { get { return X + Smoother.OffsetX; } }
        public double RenderY { get { return Y + Smoother.OffsetY; } }
        public double RenderZ { get { return Z + Smoother.OffsetZ; } }
    }

    // C04 §3/§5.4：实体视图池。按 id 复用同一对象，进场吸附、离场立刻回收，帧内零分配。
    public sealed class EntityViews
    {
        public const int Capacity = SnapshotView.MaxEntities;

        private readonly EntityView[] _pool = new EntityView[Capacity + 1];
        private readonly bool[] _active = new bool[Capacity + 1];
        private readonly ushort[] _activeIds = new ushort[Capacity + 1];
        private int _activeCount;

        private readonly uint[] _times = new uint[SnapshotView.HistoryFrames];
        private readonly int[] _seen = new int[Capacity + 1];
        private int _seenToken;

        private readonly SnapshotView.EntityVisitor _visitor;

        public EntityViews()
        {
            _visitor = MarkVisible;   // 缓存委托：每帧遍历不产生闭包分配
        }

        public ushort LocalPlayerId { get; private set; }
        public int ActiveCount { get { return _activeCount; } }
        public double LastAuthorityX { get; private set; }
        public double LastAuthorityY { get; private set; }
        public double LastAuthorityZ { get; private set; }

        public void SetLocalPlayer(ushort id) { LocalPlayerId = id; }

        public bool TryGet(ushort id, out EntityView view)
        {
            view = null;
            if (id < 1 || id > Capacity) return false;
            view = _pool[id];
            return view != null && _active[id];
        }

        // 池化取用：同一 id 永远拿到同一个对象（§5.4 的「复用视图对象」），不存在则创建。
        public EntityView Get(ushort id)
        {
            if (id < 1 || id > Capacity) return null;
            var view = _pool[id];
            if (view == null)
            {
                view = new EntityView();
                view.Id = id;
                _pool[id] = view;
            }
            if (!_active[id])
            {
                _active[id] = true;
                _activeIds[_activeCount] = id;
                _activeCount += 1;
            }
            return view;
        }

        public void Release(ushort id)
        {
            if (id < 1 || id > Capacity) return;
            if (!_active[id]) return;
            _active[id] = false;
            for (var i = 0; i < _activeCount; i++)
            {
                if (_activeIds[i] != id) continue;
                _activeCount -= 1;
                _activeIds[i] = _activeIds[_activeCount];
                break;
            }
            var view = _pool[id];
            if (view == null) return;
            view.Visible = false;
            view.HasPrediction = false;
            view.Smoother.Reset();
        }

        // §5.4 的主更新：按渲染时间在关键帧环里找区间，区间内插值、新进场吸附、离场回收。
        // 返回本帧参与渲染的实体数。dtMs 用于平滑偏移的衰减。
        public int SyncFrame(SnapshotView view, Interpolation.RenderClock clock, double dtMs)
        {
            if (view == null) return 0;
            var count = view.CopyServerTimes(_times);
            if (count <= 0) return _activeCount;

            var renderTimeMs = clock == null ? 0.0 : clock.RenderTimeMs;
            int olderIndex;
            int newerIndex;
            Interpolation.FindBracket(_times, count, renderTimeMs, out olderIndex, out newerIndex);
            var olderAge = count - 1 - olderIndex;
            var newerAge = count - 1 - newerIndex;

            var older = default(SnapshotFrame);
            SnapshotFrame newer;
            var hasOlder = olderIndex != newerIndex && view.TryGetFrame(olderAge, out older);
            if (!view.TryGetFrame(newerAge, out newer)) return _activeCount;

            _seenToken += 1;
            if (_seenToken == int.MaxValue)
            {
                for (var i = 0; i < _seen.Length; i++) _seen[i] = 0;
                _seenToken = 1;
            }
            // 最新一帧的可见集合来自镜像本身（只读遍历）：它决定本帧该渲染谁、谁该回收。
            view.ForEachVisible(_visitor);

            for (var i = 0; i < newer.EntityCount; i++)
            {
                var record = newer.Entities[i];
                if (record.Id >= 1 && record.Id <= Capacity && _seen[record.Id] != _seenToken) continue;  // 已在本帧被移除
                var target = Get(record.Id);
                if (target == null) continue;
                target.Visible = true;

                double x;
                double y;
                double z;
                double yaw;
                double pitch;
                if (record.Id == LocalPlayerId && !target.HasPrediction) continue;   // §5.4：无预测值时姿态已由最新帧吸附
                if (record.Id == LocalPlayerId && target.HasPrediction)
                {
                    x = target.PredictedX;
                    y = target.PredictedY;
                    z = target.PredictedZ;
                    yaw = target.PredictedYawRad;
                    pitch = target.PredictedPitchRad;
                }
                else
                {
                    var targetPose = ToPose(record);
                    FrameEntity previous;
                    if (hasOlder && view.TryGetEntityInFrame(olderAge, record.Id, out previous))
                    {
                        var alpha = Interpolation.InterpolationAlpha(renderTimeMs, older.ServerTimeMs, newer.ServerTimeMs);
                        var olderPose = ToPose(previous);
                        x = Interpolation.Lerp(olderPose.X, targetPose.X, alpha);
                        y = Interpolation.Lerp(olderPose.Y, targetPose.Y, alpha);
                        z = Interpolation.Lerp(olderPose.Z, targetPose.Z, alpha);
                        yaw = Interpolation.LerpAngle(olderPose.YawRad, targetPose.YawRad, alpha);
                        pitch = Interpolation.LerpAngle(olderPose.PitchRad, targetPose.PitchRad, alpha);
                    }
                    else
                    {
                        x = targetPose.X;      // 新进场：吸附，不插值
                        y = targetPose.Y;
                        z = targetPose.Z;
                        yaw = targetPose.YawRad;
                        pitch = targetPose.PitchRad;
                    }
                }

                target.X = x;
                target.Y = y;
                target.Z = z;
                target.YawRad = yaw;
                target.PitchRad = pitch;
            }

            // 离场：镜像已应用移除列表，本帧没再出现的 id 立刻回收（§5.4，不做淡出）。
            for (var i = _activeCount - 1; i >= 0; i--)
            {
                var id = _activeIds[i];
                if (id >= 1 && id <= Capacity && _seen[id] == _seenToken) continue;
                Release(id);
            }

            for (var i = 0; i < _activeCount; i++)
            {
                var entity = _pool[_activeIds[i]];
                if (entity == null) continue;
                entity.Smoother.Decay(dtMs);   // §5.5：偏移按 0.12s 时间常数衰减，归零由 smoother 负责
            }

            // §5.4/§9：本地权威姿态（有预测值时渲染用预测值，权威值留作和解基线）。
            FrameEntity localAuthority;
            if (view.TryGetLocalAuthority(out localAuthority))
            {
                LastAuthorityX = Quantize.DequantizePosition(localAuthority.XCm);
                LastAuthorityY = Quantize.DequantizePosition(localAuthority.YCm);
                LastAuthorityZ = Quantize.DequantizePosition(localAuthority.ZCm);
            }
            return _activeCount;
        }

        // 只读遍历的回调：既标记「本帧可见」（回收判据），也把 §5.3 规定「不插值」的字段
        // （kind/flags/hpRatio/state）直接取自最新帧；本地玩家无预测值时同样按最新帧姿态吸附。
        private void MarkVisible(ushort id, in FrameEntity entity)
        {
            if (id < 1 || id > Capacity) return;
            _seen[id] = _seenToken;
            var target = Get(id);
            if (target == null) return;
            // 本帧是否"新出现"必须在置 Visible 之前取：它决定要不要在这里吸附姿态。
            var wasVisible = target.Visible;
            target.Visible = true;
            target.Kind = (byte)(entity.KindFlags & EntityRecord.KindMask);
            target.Flags = entity.KindFlags;
            target.HpRatio = Quantize.DequantizeRatio(entity.HpRatioUnits);
            target.State = entity.State;
            // 姿态只能有两个来源：①本地玩家无预测值 ⇒ 按最新帧吸附（原有语义）；
            // ②**本帧新出现**的实体 ⇒ 同样必须在这里吸附。最新帧是**差分**帧，没变化过的实体
            //   根本不在它的记录里，而下面的插值循环只遍历差分记录 ⇒ 不吸附就会以 (0,0,0)
            //   在场地中心渲染若干帧（100–250ms）才被覆盖（审计 M7）。
            var snapFromMirror = id != LocalPlayerId ? !wasVisible : !target.HasPrediction;
            if (!snapFromMirror) return;
            target.X = Quantize.DequantizePosition(entity.XCm);
            target.Y = Quantize.DequantizePosition(entity.YCm);
            target.Z = Quantize.DequantizePosition(entity.ZCm);
            target.YawRad = Quantize.DequantizeAngle(entity.YawUnits);
            target.PitchRad = Quantize.DequantizeAngle(entity.PitchUnits);
        }

        // §5.5：模拟被硬纠正后调用——把「纠正前的渲染位置 - 权威姿态」交给 smoother，
        // 渲染保持连续；误差超过 1m 时 smoother 返回 Snap，调用方直接吸附。
        public SmoothingAction ApplyCorrection(EntityView view, double previousX, double previousY, double previousZ)
        {
            if (view == null) return SmoothingAction.None;
            return view.Smoother.Apply(previousX - view.X, previousY - view.Y, previousZ - view.Z);
        }

        private struct Pose
        {
            public double X;
            public double Y;
            public double Z;
            public double YawRad;
            public double PitchRad;
        }

        private static Pose ToPose(in FrameEntity record)
        {
            Pose pose;
            pose.X = Quantize.DequantizePosition(record.XCm);
            pose.Y = Quantize.DequantizePosition(record.YCm);
            pose.Z = Quantize.DequantizePosition(record.ZCm);
            pose.YawRad = Quantize.DequantizeAngle(record.YawUnits);
            pose.PitchRad = Quantize.DequantizeAngle(record.PitchUnits);
            return pose;
        }

    }
}
