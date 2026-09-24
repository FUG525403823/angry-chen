using UnityEngine;

namespace Ac.View
{
    // C08 §5(a)：四种羊形。编号顺序与冻结表一致（0..3）。
    public enum SheepKind : byte { Grunt = 0, Ram = 1, Elite = 2, King = 3 }

    public struct SheepForm
    {
        public SheepKind Kind;
        public double HeightM;
        public double RadiusM;
        public double Scale;
        public int WoolClusters;
        public double EmblemSizeM;
    }

    // §9：四种羊形的程序化网格生成器。共用一份生成器，按羊形缩放；每形顶点/三角形 ≤ 512。
    public static class SheepMesh
    {
        public const int FormCount = 4;
        public const int VertexBudgetPerForm = 512;
        public const int TriangleBudgetPerForm = 512;
        public const double BodyHeightM = 0.62;
        public const double WoolClusterRadiusM = 0.21;
        public const double ClusterSpreadX = 0.62;
        public const double ClusterSpreadZ = 0.42;
        public const double ClusterSpreadY = 0.20;
        public const double ClusterScaleMin = 0.86;
        public const double ClusterScaleMax = 1.14;
        public const double HeadOffsetY = 0.90;
        public const double HeadOffsetZ = 0.92;
        public const float HeadHalfM = 0.16f;
        public const float HornHalfM = 0.07f;
        public const float HornOffsetM = 0.10f;
        public const float HornSpreadM = 0.14f;
        public const float LegHalfM = 0.07f;
        public const float LegOffsetX = 0.18f;
        public const float LegOffsetY = 0.12f;
        public const float LegOffsetZ = 0.20f;

        // §5(d) 额标配色：主色 + 问界羊自发光 + 羊王自发光
        public const int EmblemColorBase = 0xE8E8F0;
        public const int EmblemColorAwakened = 0x7AD1FF;
        public const int EmblemColorKing = 0xFF8A4C;

        public static SheepForm Form(SheepKind kind)
        {
            var form = default(SheepForm);
            form.Kind = kind;
            if (kind == SheepKind.Grunt) { form.HeightM = 0.9; form.RadiusM = 0.50; form.Scale = 1.00; form.WoolClusters = 8; form.EmblemSizeM = 0.22; }
            else if (kind == SheepKind.Ram) { form.HeightM = 1.0; form.RadiusM = 0.55; form.Scale = 1.06; form.WoolClusters = 12; form.EmblemSizeM = 0.22; }
            else if (kind == SheepKind.Elite) { form.HeightM = 1.1; form.RadiusM = 0.60; form.Scale = 1.12; form.WoolClusters = 12; form.EmblemSizeM = 0.22; }
            else { form.HeightM = 2.4; form.RadiusM = 1.60; form.Scale = 1.60; form.WoolClusters = 12; form.EmblemSizeM = 0.36; }
            return form;
        }

        public static float FormScale(SheepKind kind) { return (float)Form(kind).Scale; }

        public static SheepKind KindOf(byte kind)
        {
            if (kind > (byte)SheepKind.King) return SheepKind.King;
            return (SheepKind)kind;
        }

        // 团缩放抖动：整数哈希，同 id 永远同形（§5(b)）；网格本身用羊形固定种子，逐实例抖动用这里
        public static float WoolJitter(SheepKind kind, ushort entityId)
        {
            var noise = ArenaMesh.Hash((uint)entityId * 40503u + (uint)kind);
            var unit = (noise % 1001) / 1000f;
            return (float)(ClusterScaleMin + (ClusterScaleMax - ClusterScaleMin) * unit);
        }

        // 羊体：羊毛团（八面体）+ 头 + 角 + 腿，全部按羊形缩放
        public static Mesh Build(SheepKind kind)
        {
            var form = Form(kind);
            var scale = (float)form.Scale;
            var clusters = new Vector3[form.WoolClusters];
            for (var i = 0; i < form.WoolClusters; i++)
            {
                var t = form.WoolClusters == 1 ? 0f : i / (float)(form.WoolClusters - 1);
                var angle = (float)(i * 2.399963229728653);   // 黄金角，均匀分布
                var radius = (float)(0.55 + 0.45 * t);
                clusters[i] = new Vector3(
                    Mathf.Cos(angle) * (float)ClusterSpreadX * radius,
                    (float)BodyHeightM + Mathf.Sin(angle * 1.7f) * (float)ClusterSpreadY,
                    Mathf.Sin(angle) * (float)ClusterSpreadZ * radius);
            }

            var vertices = new Vector3[form.WoolClusters * 6 + 8 + 16 + 32];
            var indices = new int[(form.WoolClusters * 8 + 12 + 24 + 48) * 3];
            var v = 0;
            var n = 0;
            for (var i = 0; i < form.WoolClusters; i++)
            {
                Octahedron(vertices, indices, ref v, ref n, clusters[i] * scale, (float)WoolClusterRadiusM * scale);
            }
            Box(vertices, indices, ref v, ref n, new Vector3(0f, (float)HeadOffsetY * scale, (float)HeadOffsetZ * scale), HeadHalfM * scale);
            for (var horn = 0; horn < 2; horn++)
            {
                var sign = horn == 0 ? -1f : 1f;
                Box(vertices, indices, ref v, ref n, new Vector3(sign * HornSpreadM * scale, ((float)HeadOffsetY + HornOffsetM) * scale, (float)HeadOffsetZ * scale), HornHalfM * scale);
            }
            for (var leg = 0; leg < 4; leg++)
            {
                var signX = (leg & 1) == 0 ? -1f : 1f;
                var signZ = leg < 2 ? -1f : 1f;
                Box(vertices, indices, ref v, ref n, new Vector3(signX * LegOffsetX * scale, LegOffsetY * scale, signZ * LegOffsetZ * scale), LegHalfM * scale);
            }

            var mesh = new Mesh();
            mesh.name = "Sheep" + kind;
            mesh.vertices = vertices;
            mesh.SetTriangles(indices, 0);
            mesh.RecalculateNormals();
            mesh.RecalculateBounds();
            mesh.colors32 = VertexColors(vertices.Length, ColorOf(kind));
            return mesh;
        }

        // §5(d)：额标是程序化网格 + 顶点色，不引用任何贴图
        public static Mesh BuildEmblem(SheepKind kind)
        {
            var form = Form(kind);
            var size = (float)form.EmblemSizeM;
            var color = EmblemColorPure(kind);
            // 实心菱形（4 三角形）+ 菱形描边（4 片，8 三角形）+ 外圈方框（4 片，8 三角形）
            var vertices = new Vector3[5 + 8 * 4];
            vertices[0] = new Vector3(0f, 0f, 0f);
            vertices[1] = new Vector3(-size * 0.5f, 0f, 0f);
            vertices[2] = new Vector3(0f, size * 0.5f, 0f);
            vertices[3] = new Vector3(size * 0.5f, 0f, 0f);
            vertices[4] = new Vector3(0f, -size * 0.5f, 0f);
            var indices = new int[(4 + 16) * 3];
            var cursor = 0;
            var faces = new[] { 0, 1, 2, 0, 2, 3, 0, 3, 4, 0, 4, 1 };
            for (var i = 0; i < faces.Length; i++) indices[cursor++] = faces[i];
            var v = 5;
            cursor = AddQuad(vertices, indices, cursor, ref v, new Vector3(-size * 0.5f, 0f, 0f), new Vector3(0f, size * 0.5f, 0f), 0.06f * size);
            cursor = AddQuad(vertices, indices, cursor, ref v, new Vector3(0f, size * 0.5f, 0f), new Vector3(size * 0.5f, 0f, 0f), 0.06f * size);
            cursor = AddQuad(vertices, indices, cursor, ref v, new Vector3(size * 0.5f, 0f, 0f), new Vector3(0f, -size * 0.5f, 0f), 0.06f * size);
            cursor = AddQuad(vertices, indices, cursor, ref v, new Vector3(0f, -size * 0.5f, 0f), new Vector3(-size * 0.5f, 0f, 0f), 0.06f * size);
            var outer = size * 0.78f;
            var inner = size * 0.70f;
            cursor = AddOutline(vertices, indices, cursor, ref v, inner, outer);
            var mesh = new Mesh();
            mesh.name = "Emblem" + kind;
            mesh.vertices = vertices;
            mesh.SetTriangles(indices, 0);
            mesh.RecalculateNormals();
            mesh.RecalculateBounds();
            mesh.colors32 = VertexColors(vertices.Length, color);
            return mesh;
        }

        // 两点之间的一片细带（描边）
        private static int AddQuad(Vector3[] vertices, int[] indices, int cursor, ref int v, Vector3 from, Vector3 to, float width)
        {
            var direction = (to - from).normalized;
            var normal = new Vector3(-direction.y, direction.x, 0f) * (width * 0.5f);
            vertices[v] = from - normal;
            vertices[v + 1] = from + normal;
            vertices[v + 2] = to + normal;
            vertices[v + 3] = to - normal;
            indices[cursor] = v;
            indices[cursor + 1] = v + 1;
            indices[cursor + 2] = v + 2;
            indices[cursor + 3] = v;
            indices[cursor + 4] = v + 2;
            indices[cursor + 5] = v + 3;
            v += 4;
            return cursor + 6;
        }

        private static int AddOutline(Vector3[] vertices, int[] indices, int cursor, ref int v, float inner, float outer)
        {
            cursor = AddQuad(vertices, indices, cursor, ref v, new Vector3(-outer, outer, 0f), new Vector3(outer, outer, 0f), outer - inner);
            cursor = AddQuad(vertices, indices, cursor, ref v, new Vector3(outer, outer, 0f), new Vector3(outer, -outer, 0f), outer - inner);
            cursor = AddQuad(vertices, indices, cursor, ref v, new Vector3(outer, -outer, 0f), new Vector3(-outer, -outer, 0f), outer - inner);
            return AddQuad(vertices, indices, cursor, ref v, new Vector3(-outer, -outer, 0f), new Vector3(-outer, outer, 0f), outer - inner);
        }

        public static Color32 ColorOf(SheepKind kind)
        {
            return kind == SheepKind.King ? ArtPalette.Hex(EmblemColorKing) : ArtPalette.Hex(EmblemColorBase);
        }

        public static Color32 EmblemColorPure(SheepKind kind)
        {
            return kind == SheepKind.King ? ArtPalette.Hex(EmblemColorKing) : ArtPalette.Hex(EmblemColorAwakened);
        }

        private static Color32[] VertexColors(int count, Color32 color)
        {
            var colors = new Color32[count];
            for (var i = 0; i < count; i++) colors[i] = color;
            return colors;
        }

        private static void Octahedron(Vector3[] vertices, int[] indices, ref int v, ref int n, Vector3 center, float radius)
        {
            var baseIndex = v;
            vertices[v] = center + new Vector3(radius, 0f, 0f);
            vertices[v + 1] = center + new Vector3(0f, radius, 0f);
            vertices[v + 2] = center + new Vector3(-radius, 0f, 0f);
            vertices[v + 3] = center + new Vector3(0f, -radius, 0f);
            vertices[v + 4] = center + new Vector3(0f, 0f, radius);
            vertices[v + 5] = center + new Vector3(0f, 0f, -radius);
            v += 6;
            var faces = new[] { 0, 1, 4, 1, 2, 4, 2, 3, 4, 3, 0, 4, 1, 0, 5, 2, 1, 5, 3, 2, 5, 0, 3, 5 };
            for (var i = 0; i < faces.Length; i++) indices[n + i] = baseIndex + faces[i];
            n += faces.Length;
        }

        private static void Box(Vector3[] vertices, int[] indices, ref int v, ref int n, Vector3 center, float half)
        {
            var baseIndex = v;
            for (var i = 0; i < 8; i++)
            {
                vertices[v + i] = center + new Vector3(((i & 1) == 0 ? -half : half), ((i & 2) == 0 ? -half : half), ((i & 4) == 0 ? -half : half));
            }
            v += 8;
            var faces = new[]
            {
                0, 2, 1, 0, 3, 2, 4, 5, 6, 4, 6, 7, 0, 1, 5, 0, 5, 4,
                3, 7, 6, 3, 6, 2, 1, 2, 6, 1, 6, 5, 0, 4, 7, 0, 7, 3,
            };
            for (var i = 0; i < faces.Length; i++) indices[n + i] = baseIndex + faces[i];
            n += faces.Length;
        }

    }
}
