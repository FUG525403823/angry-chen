using Ac.Sim;
using UnityEngine;

namespace Ac.View
{
    // C07 §5(a)：场地数值。几何值一律从 S06 的 MoveConfig 派生，保证"视觉与模拟不打架"是结构性的而非巧合。
    public struct ArenaParams
    {
        public double ArenaHalfSize;
        public double GridCellM;
        public double FenceHeightM;
        public double FenceThicknessM;
        public double BarnMinX;
        public double BarnMaxX;
        public double BarnMinZ;
        public double BarnMaxZ;
        public double BarnMaxY;
        public double PlayerRadiusM;
        public double OuterRingAmplitudeM;
        public double OuterRingOuterM;
        public uint Seed;

        public const int GridCells = 20;             // 80m / 4m
        public const double RingCellM = 4.0;         // 环带格：与 ±40m 边界整除，内缘正好落在 40m
        public const int OuterRingVerticesCap = 1024;
        public const double GrassTileM = 6.0;        // §5(c) 的铺贴单元
        public const double DirtTileM = 4.0;
        public const double WoodTileM = 2.4;

        public static ArenaParams Default()
        {
            var move = MoveConfig.Default();
            var arena = default(ArenaParams);
            arena.ArenaHalfSize = move.HalfSizeMeters;                       // 40
            arena.GridCellM = 4.0;
            arena.FenceHeightM = 3.0;
            arena.FenceThicknessM = move.FenceHalfThicknessMeters * 2.0;      // 0.5
            arena.BarnMinX = move.BarnMinX;
            arena.BarnMaxX = move.BarnMaxX;
            arena.BarnMinZ = move.BarnMinZ;
            arena.BarnMaxZ = move.BarnMaxZ;
            arena.BarnMaxY = move.BarnMaxY;
            arena.PlayerRadiusM = move.RadiusMeters;                          // 0.4
            arena.OuterRingAmplitudeM = 1.5;
            arena.OuterRingOuterM = 60.0;
            arena.Seed = 1337u;
            return arena;
        }

        public double FenceInnerM { get { return ArenaHalfSize - FenceThicknessM * 0.5; } }        // 39.75
        public double WalkableLimitM { get { return FenceInnerM - PlayerRadiusM; } }               // 39.35
    }

    public struct MeshStats
    {
        public int Vertices;
        public int Triangles;
        public int Materials;
        public int DrawCalls;
    }

    // §9：程序化场地网格。每个部件都造出真 Mesh（顶点 + 索引 + UV），统计值由各 Mesh 自身求和而来，
    // 不写凑数常量；渲染器与 Collider 的场景装配留给场景步骤（批处理没有渲染上下文）。
    public sealed class ArenaMesh
    {
        private readonly ArenaParams _arena;

        public const int GroundVertexSide = ArenaParams.GridCells + 1;   // 21
        public const int GroundTriangles = ArenaParams.GridCells * ArenaParams.GridCells * 2;   // 800

        public Mesh GroundMesh { get; private set; }
        public Mesh OuterRingMesh { get; private set; }
        public Mesh FenceMesh { get; private set; }
        public Mesh BarnMesh { get; private set; }
        public Mesh BarnRoofMesh { get; private set; }
        public Mesh DirtYardMesh { get; private set; }
        public Mesh HayBaleMesh { get; private set; }

        public ArenaMesh(in ArenaParams arena) { _arena = arena; }

        public MeshStats Build()
        {
            GroundMesh = MakeMesh("ArenaGround", BuildGround(), BuildGroundIndices(), GroundUvs());
            FenceMesh = MakeBoxes("ArenaFence", BuildFenceBoxes());
            BarnMesh = MakeBoxes("ArenaBarn", Barn());
            BarnRoofMesh = MakeBoxes("ArenaBarnRoof", BarnRoof());
            HayBaleMesh = MakeBoxes("ArenaHay", HayBale());
            DirtYardMesh = MakeMesh("ArenaDirtYard", BuildDirtYard(), BuildDirtIndices(), DirtUvs());
            OuterRingMesh = MakeMesh("ArenaOuterRing", BuildOuterRing(), BuildRingIndices(OuterRingCells()), RingUvs());

            var stats = default(MeshStats);
            stats.Vertices = GroundMesh.vertexCount + FenceMesh.vertexCount + BarnMesh.vertexCount
                + BarnRoofMesh.vertexCount + HayBaleMesh.vertexCount + DirtYardMesh.vertexCount + OuterRingMesh.vertexCount;
            stats.Triangles = GroundMesh.triangles.Length / 3 + FenceMesh.triangles.Length / 3 + BarnMesh.triangles.Length / 3
                + BarnRoofMesh.triangles.Length / 3 + HayBaleMesh.triangles.Length / 3 + DirtYardMesh.triangles.Length / 3
                + OuterRingMesh.triangles.Length / 3;
            stats.Materials = Materials.MaterialCount;   // grass / dirt / fence / barnWall / barnRoof / hay 各一组
            stats.DrawCalls = Materials.MaterialCount;
            return stats;
        }

        public int OuterRingTriangles() { return OuterRingCells() * 2; }

        // 环带格子：4m 格覆盖 ±60m。判据按格子边界：只要有一轴完全在 ±40m 之外就属于环带，
        // 于是内缘正好落在 40m（无空洞），且每个顶点都满足 max(|x|,|z|) ≥ 40。
        // 环带拆成四条：南北两条（z ∈ ±[40,60]、沿 x 8m 格）+ 东西两条（x ∈ ±[40,60]、沿 z 8m 格），
        // 带宽 20m 用 4m 格以便内缘正好落在 40m。总格数 2×(5×15 + 10×5) = 250，顶点 1000 ≤ 1024。
        private const int RingBandCells = 5;      // 20m / 4m
        private const int RingLongCells = 15;     // 120m / 8m
        private const int RingShortCells = 10;    // 80m / 8m

        private int OuterRingCells()
        {
            return 2 * (RingBandCells * RingLongCells + RingShortCells * RingBandCells);
        }

        private Vector3[] BuildGround()
        {
            var vertices = new Vector3[GroundVertexSide * GroundVertexSide];
            var half = _arena.ArenaHalfSize;
            for (var z = 0; z < GroundVertexSide; z++)
            {
                for (var x = 0; x < GroundVertexSide; x++)
                {
                    vertices[z * GroundVertexSide + x] = new Vector3((float)(-half + _arena.GridCellM * x), 0f, (float)(-half + _arena.GridCellM * z));
                }
            }
            return vertices;
        }

        private int[] BuildGroundIndices()
        {
            var indices = new int[GroundTriangles * 3];
            var cursor = 0;
            for (var z = 0; z < ArenaParams.GridCells; z++)
            {
                for (var x = 0; x < ArenaParams.GridCells; x++)
                {
                    var v = z * GroundVertexSide + x;
                    indices[cursor] = v;
                    indices[cursor + 1] = v + GroundVertexSide;
                    indices[cursor + 2] = v + 1;
                    indices[cursor + 3] = v + 1;
                    indices[cursor + 4] = v + GroundVertexSide;
                    indices[cursor + 5] = v + GroundVertexSide + 1;
                    cursor += 6;
                }
            }
            return indices;
        }

        // §5(c)：铺贴单元写进 UV（6m 一格），贴图本身由 Materials 挂上
        private Vector2[] GroundUvs()
        {
            var uvs = new Vector2[GroundVertexSide * GroundVertexSide];
            for (var z = 0; z < GroundVertexSide; z++)
            {
                for (var x = 0; x < GroundVertexSide; x++)
                {
                    uvs[z * GroundVertexSide + x] = new Vector2(
                        (float)(_arena.GridCellM * x / ArenaParams.GrassTileM),
                        (float)(_arena.GridCellM * z / ArenaParams.GrassTileM));
                }
            }
            return uvs;
        }

        private Vector2[] DirtUvs()
        {
            var uvs = new Vector2[DirtVertexCount];
            for (var i = 0; i < uvs.Length; i++) uvs[i] = new Vector2(0.5f, 0.5f);
            return uvs;
        }

        private Vector2[] RingUvs()
        {
            var uvs = new Vector2[OuterRingCells() * 4];
            for (var i = 0; i < uvs.Length; i++) uvs[i] = new Vector2(0.5f, 0.5f);
            return uvs;
        }

        private Vector3[] BuildFenceBoxes()
        {
            var half = _arena.ArenaHalfSize;
            var thick = _arena.FenceThicknessM;
            var vertices = new Vector3[8 * 4];
            var boxes = new Vector3[8 * 4];
            Box(boxes, 0, -half - thick * 0.5, -half - thick * 0.5, half + thick * 0.5, -half + thick * 0.5, 0.0, _arena.FenceHeightM);
            Box(boxes, 8, -half - thick * 0.5, half - thick * 0.5, half + thick * 0.5, half + thick * 0.5, 0.0, _arena.FenceHeightM);
            Box(boxes, 16, -half - thick * 0.5, -half, -half + thick * 0.5, half, 0.0, _arena.FenceHeightM);
            Box(boxes, 24, half - thick * 0.5, -half, half + thick * 0.5, half, 0.0, _arena.FenceHeightM);
            System.Array.Copy(boxes, vertices, vertices.Length);
            return vertices;
        }

        private Vector3[] Barn()
        {
            var vertices = new Vector3[8];
            Box(vertices, 0, _arena.BarnMinX, _arena.BarnMinZ, _arena.BarnMaxX, _arena.BarnMaxZ, 0.0, _arena.BarnMaxY);
            return vertices;
        }

        private Vector3[] BarnRoof()
        {
            var vertices = new Vector3[8];
            Box(vertices, 0, _arena.BarnMinX, _arena.BarnMinZ, _arena.BarnMaxX, _arena.BarnMaxZ, _arena.BarnMaxY, _arena.BarnMaxY + 1.5);
            return vertices;
        }

        private Vector3[] HayBale()
        {
            var vertices = new Vector3[8];
            Box(vertices, 0, 8.0, -2.2, 10.0, 0.0, 0.0, 1.2);
            return vertices;
        }

        public const int DirtVertexCount = 17;   // 圆心 + 16 个圆周点（不重复首点）

        private Vector3[] BuildDirtYard()
        {
            var vertices = new Vector3[DirtVertexCount];
            vertices[0] = new Vector3(0f, 0.01f, 0f);   // 抬高 1cm 免得与地面共面闪烁
            for (var i = 0; i < DirtVertexCount - 1; i++)
            {
                var angle = i * (360.0 / (DirtVertexCount - 1)) * System.Math.PI / 180.0;
                vertices[i + 1] = new Vector3((float)(System.Math.Cos(angle) * 7.5), 0.01f, (float)(System.Math.Sin(angle) * 7.5));
            }
            return vertices;
        }

        private int[] BuildDirtIndices()
        {
            var indices = new int[(DirtVertexCount - 1) * 3];
            for (var i = 0; i < DirtVertexCount - 1; i++)
            {
                indices[i * 3] = 0;
                indices[i * 3 + 1] = i + 1;
                indices[i * 3 + 2] = (i + 1) % (DirtVertexCount - 1) + 1;
            }
            return indices;
        }

        // §5(a)：起伏只允许在 ±40m 以外的环带，不进可走区域、不生成碰撞体
        private Vector3[] BuildOuterRing()
        {
            var vertices = new Vector3[OuterRingCells() * 4];
            var half = _arena.ArenaHalfSize;
            var outer = _arena.OuterRingOuterM;
            var used = 0;
            for (var side = 0; side < 2; side++)
            {
                var zSign = side == 0 ? 1.0 : -1.0;
                for (var row = 0; row < RingBandCells; row++)
                {
                    var z0 = zSign > 0 ? half + 4.0 * row : -half - 4.0 * (row + 1);
                    for (var col = 0; col < RingLongCells; col++)
                    {
                        Emit(vertices, ref used, -outer + 8.0 * col, z0, 8.0, 4.0);
                    }
                }
            }
            for (var side = 0; side < 2; side++)
            {
                var xSign = side == 0 ? 1.0 : -1.0;
                for (var col = 0; col < RingBandCells; col++)
                {
                    var x0 = xSign > 0 ? half + 4.0 * col : -half - 4.0 * (col + 1);
                    for (var row = 0; row < RingShortCells; row++)
                    {
                        Emit(vertices, ref used, x0, -half + 8.0 * row, 4.0, 8.0);
                    }
                }
            }
            return vertices;
        }

        private void Emit(Vector3[] vertices, ref int used, double x0, double z0, double width, double length)
        {
            var x1 = x0 + width;
            var z1 = z0 + length;
            vertices[used] = new Vector3((float)x0, (float)Height(x0, z0), (float)z0);
            vertices[used + 1] = new Vector3((float)x1, (float)Height(x1, z0), (float)z0);
            vertices[used + 2] = new Vector3((float)x1, (float)Height(x1, z1), (float)z1);
            vertices[used + 3] = new Vector3((float)x0, (float)Height(x0, z1), (float)z1);
            used += 4;
        }

        private double Height(double x, double z)
        {
            var magnitude = System.Math.Abs(x) > System.Math.Abs(z) ? System.Math.Abs(x) : System.Math.Abs(z);
            if (magnitude <= _arena.ArenaHalfSize) return 0.0;
            var t = (magnitude - _arena.ArenaHalfSize) / (_arena.OuterRingOuterM - _arena.ArenaHalfSize);
            if (t > 1.0) t = 1.0;
            var noise = Hash((uint)((long)(x * 16.0) * 73856093) ^ (uint)((long)(z * 16.0) * 19349663) ^ _arena.Seed);
            var unit = (noise % 2001) / 1000.0 - 1.0;   // [-1, 1]，整数派生
            return unit * _arena.OuterRingAmplitudeM * t;
        }

        internal static uint Hash(uint state)
        {
            state += 0x6D2B79F5u;
            var t = state;
            t = (t ^ (t >> 15)) * (t | 1u);
            t ^= t + (t ^ (t >> 7)) * (t | 61u);
            return t ^ (t >> 14);
        }

        private static void Box(Vector3[] vertices, int offset, double minX, double minZ, double maxX, double maxZ, double minY, double maxY)
        {
            vertices[offset] = new Vector3((float)minX, (float)minY, (float)minZ);
            vertices[offset + 1] = new Vector3((float)maxX, (float)minY, (float)minZ);
            vertices[offset + 2] = new Vector3((float)maxX, (float)minY, (float)maxZ);
            vertices[offset + 3] = new Vector3((float)minX, (float)minY, (float)maxZ);
            vertices[offset + 4] = new Vector3((float)minX, (float)maxY, (float)minZ);
            vertices[offset + 5] = new Vector3((float)maxX, (float)maxY, (float)minZ);
            vertices[offset + 6] = new Vector3((float)maxX, (float)maxY, (float)maxZ);
            vertices[offset + 7] = new Vector3((float)minX, (float)maxY, (float)maxZ);
        }

        private static readonly int[] BoxIndices =
        {
            0, 2, 1, 0, 3, 2, 4, 5, 6, 4, 6, 7, 0, 1, 5, 0, 5, 4,
            3, 7, 6, 3, 6, 2, 1, 2, 6, 1, 6, 5, 0, 4, 7, 0, 7, 3,
        };

        private static Mesh MakeBoxes(string name, Vector3[] boxes)
        {
            var count = boxes.Length / 8;
            var vertices = new Vector3[boxes.Length];
            System.Array.Copy(boxes, vertices, boxes.Length);
            var indices = new int[count * BoxIndices.Length];
            var uvs = new Vector2[boxes.Length];
            for (var box = 0; box < count; box++)
            {
                for (var i = 0; i < BoxIndices.Length; i++) indices[box * BoxIndices.Length + i] = BoxIndices[i] + box * 8;
                for (var i = 0; i < 8; i++) uvs[box * 8 + i] = new Vector2(i < 4 ? 0f : 1f, (i % 4) < 2 ? 0f : 1f);
            }
            return MakeMesh(name, vertices, indices, uvs);
        }

        private static int[] BuildRingIndices(int cells)
        {
            var indices = new int[cells * 6];
            for (var i = 0; i < cells; i++)
            {
                var v = i * 4;
                indices[i * 6] = v;
                indices[i * 6 + 1] = v + 1;
                indices[i * 6 + 2] = v + 2;
                indices[i * 6 + 3] = v;
                indices[i * 6 + 4] = v + 2;
                indices[i * 6 + 5] = v + 3;
            }
            return indices;
        }

        private static Mesh MakeMesh(string name, Vector3[] vertices, int[] indices, Vector2[] uvs)
        {
            var mesh = new Mesh();
            mesh.name = name;
            mesh.vertices = vertices;
            mesh.uv = uvs;
            mesh.SetTriangles(indices, 0);
            mesh.RecalculateNormals();
            mesh.RecalculateBounds();
            return mesh;
        }
    }
}
