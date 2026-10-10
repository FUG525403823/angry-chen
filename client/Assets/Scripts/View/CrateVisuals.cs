namespace Ac.View
{
    // S16：弹药补给箱的视图集合（纯模型，不引用 UnityEngine —— 与 SheepInstancePool 同纪律）。
    // 补给箱是服务端实体（EntityKind::kPickup = 3），随比赛生成/回收、随快照到达，所以渲染**只认
    // 权威快照**：每帧把 view.Kind == 3 的活动实体收进小数组，Unity 侧按这份数据摆静态立方体。
    // 不自己发明箱子位置（kAmmoCrateCount 是服务端配置，客户端不重复一份坐标表）。
    public sealed class CrateVisuals
    {
        // 线上 kind 码（server/src/sim/entity_table.hpp 的 EntityKind）：3 = 拾取物/补给箱。
        public const byte KindPickup = 3;
        // 上界留裕量（kAmmoCrateCount = 4，10 足够应付后续加箱而不动容量）。
        public const int MaxCrates = 10;

        public struct CrateInstance
        {
            public ushort Id;
            public float X;
            public float Y;
            public float Z;
            public bool Visible;
        }

        private readonly CrateInstance[] _instances = new CrateInstance[MaxCrates];

        public int Count { get; private set; }
        public CrateInstance[] Instances { get { return _instances; } }
        public int OverflowCount { get; private set; }

        // 每帧入口：复位游标（上一帧的旧记录必须失活，否则箱子会留下幽灵）。
        public void BeginFrame()
        {
            Count = 0;
            OverflowCount = 0;
        }

        // 写一条箱子实例；非拾取/不可见/超容直接返回。
        public void Write(in EntityView view)
        {
            if (!view.Visible) return;
            if (view.Kind != KindPickup) return;
            if (Count >= MaxCrates) { OverflowCount += 1; return; }
            CrateInstance instance;
            instance.Id = view.Id;
            instance.X = (float)view.X;
            instance.Y = (float)view.Y;
            instance.Z = (float)view.Z;
            instance.Visible = true;
            _instances[Count] = instance;
            Count += 1;
        }
    }
}
