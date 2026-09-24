using Ac.Sim;
using UnityEngine;

namespace Ac.View
{
    public struct BoxSpec
    {
        public double MinX;
        public double MinZ;
        public double MaxX;
        public double MaxZ;
        public double MinY;
        public double MaxY;
    }

    // C07 §5(b)/§9：静态碰撞盒与锚点。碰撞数据是纯数据（场景装配时再实例化 Collider），
    // 数值从 ArenaParams 派生，而 ArenaParams 又从 S06 的 MoveConfig 派生。
    public sealed class ArenaColliders
    {
        public const int BoxCount = 5;            // 四面围栏 + 谷仓
        public const int PlayerSpawnCount = 4;
        public const int SpawnPointCount = 12;
        public const double SpawnPointRadiusM = 38.0;

        public BoxSpec[] Boxes { get; private set; }
        public Vector2[] PlayerSpawns { get; private set; }
        public Vector2[] SpawnPoints { get; private set; }
        public double WalkableLimitM { get; private set; }
        public double FenceInnerM { get; private set; }

        public static ArenaColliders Build()
        {
            var arena = ArenaParams.Default();
            var half = arena.ArenaHalfSize;
            var thick = arena.FenceThicknessM;
            var inner = arena.FenceInnerM;
            var set = new ArenaColliders();
            set.FenceInnerM = inner;
            set.WalkableLimitM = arena.WalkableLimitM;

            set.Boxes = new BoxSpec[BoxCount];
            set.Boxes[0] = Fence(arena, -half - thick * 0.5, -half - thick * 0.5, half + thick * 0.5, -inner);
            set.Boxes[1] = Fence(arena, -half - thick * 0.5, inner, half + thick * 0.5, half + thick * 0.5);
            set.Boxes[2] = Fence(arena, -half - thick * 0.5, -half, -inner, half);
            set.Boxes[3] = Fence(arena, inner, -half, half + thick * 0.5, half);
            set.Boxes[4] = Fence(arena, arena.BarnMinX, arena.BarnMinZ, arena.BarnMaxX, arena.BarnMaxZ);
            set.Boxes[4].MinY = 0.0;
            set.Boxes[4].MaxY = arena.BarnMaxY;

            set.PlayerSpawns = new[]
            {
                new Vector2(-4.5f, 7f), new Vector2(-1.5f, 7f), new Vector2(1.5f, 7f), new Vector2(4.5f, 7f),
            };
            set.SpawnPoints = BuildSpawnPoints();
            return set;
        }

        // 12 个生成点：半径 38m 圆周 12 等分。坐标写成表而不是算 sin/cos，
        // 保证 (0,38)/(38,0)/(0,-38)/(-38,0) 逐位精确（与 §5(a) 的四点一致）。
        private static Vector2[] BuildSpawnPoints()
        {
            return new[]
            {
                new Vector2(0f, 38f), new Vector2(19f, (float)(38.0 * 0.8660254037844386)), new Vector2((float)(38.0 * 0.8660254037844386), 19f),
                new Vector2(38f, 0f), new Vector2((float)(38.0 * 0.8660254037844386), -19f), new Vector2(19f, (float)(-38.0 * 0.8660254037844386)),
                new Vector2(0f, -38f), new Vector2(-19f, (float)(-38.0 * 0.8660254037844386)), new Vector2((float)(-38.0 * 0.8660254037844386), -19f),
                new Vector2(-38f, 0f), new Vector2((float)(-38.0 * 0.8660254037844386), 19f), new Vector2(-19f, (float)(38.0 * 0.8660254037844386)),
            };
        }

        // 场景装配用：把 AABB 数据变成真正的 BoxCollider（批处理下不调用）
        public int CreateColliders(GameObject root)
        {
            if (root == null || Boxes == null) return 0;
            var created = 0;
            for (var i = 0; i < Boxes.Length; i++)
            {
                var box = Boxes[i];
                var child = new GameObject("Box" + i);
                child.transform.SetParent(root.transform, false);
                var collider = child.AddComponent<BoxCollider>();
                collider.center = new Vector3((float)((box.MinX + box.MaxX) * 0.5), (float)((box.MinY + box.MaxY) * 0.5), (float)((box.MinZ + box.MaxZ) * 0.5));
                collider.size = new Vector3((float)(box.MaxX - box.MinX), (float)(box.MaxY - box.MinY), (float)(box.MaxZ - box.MinZ));
                created += 1;
            }
            return created;
        }

        private static BoxSpec Fence(in ArenaParams arena, double minX, double minZ, double maxX, double maxZ)
        {
            var box = default(BoxSpec);
            box.MinX = minX;
            box.MinZ = minZ;
            box.MaxX = maxX;
            box.MaxZ = maxZ;
            box.MinY = 0.0;
            box.MaxY = arena.FenceHeightM;
            return box;
        }
    }
}
