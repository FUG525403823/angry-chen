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

        // 羊体（C08 §5(a)，2026-09-30 重做）：现在是“圆的”—— 羊毛团用 UV 球
        // （原来是 8 面体，出包里就是一堆白方块，玩家反馈“根本不像羊”），加上头/嘴/耳/尾，
        // 四条腿保持方块但拉长到地面。顶点/三角形仍在 512 预算内。
        public static Mesh Build(SheepKind kind)
        {
            var form = Form(kind);
            var scale = (float)form.Scale;
            var woolColor = ColorOf(kind);
            var darkColor = new Color32(58, 54, 52, 255);      // 头/腿/蹄的深色
            var hornColor = new Color32(206, 196, 176, 255);   // 角/蹄的骨色

            var vertices = new Vector3[MaxVerts];
            var indices = new int[MaxIndices];
            var colors = new Color32[MaxVerts];
            var v = 0;
            var n = 0;

            // 躯干：三个球沿 Z 排成一个略偏长的蛋形（后/中/前），再加一个略高的背部球
            var bodyY = (float)BodyHeightM;
            Sphere(vertices, indices, colors, ref v, ref n, new Vector3(0f, bodyY, -0.20f * scale), 0.190f * scale, woolColor);
            Sphere(vertices, indices, colors, ref v, ref n, new Vector3(0f, bodyY + 0.01f * scale, 0.00f), 0.215f * scale, woolColor);
            Sphere(vertices, indices, colors, ref v, ref n, new Vector3(0f, bodyY, 0.20f * scale), 0.190f * scale, woolColor);
            Sphere(vertices, indices, colors, ref v, ref n, new Vector3(0f, bodyY + 0.13f * scale, 0.02f * scale), 0.155f * scale, woolColor);
            // 额外羊毛团不再按 WoolClusters 无限堆：每个球 35 顶点，
            // 四个固定球 + 头/尾已经把轮廓做圆；再堆就会撞穿 §5(a) 的 512 顶点/三角形预算
            // （用例 sheep.geometry 会直接报越界）。字段仍在冻结表里，只是生成器不再逐个用它。
            _ = form.WoolClusters;

            // 头：球 + 嘴巴（方块拉长）+ 两只耳朵
            var headY = (float)HeadOffsetY * scale;
            var headZ = (float)HeadOffsetZ * 0.62f * scale;
            Sphere(vertices, indices, colors, ref v, ref n, new Vector3(0f, headY, headZ), (float)HeadHalfM * 0.95f * scale, darkColor);
            Box(vertices, indices, colors, ref v, ref n, new Vector3(0f, headY - 0.035f * scale, headZ + (float)HeadHalfM * 1.15f * scale),
                new Vector3(0.062f * scale, 0.048f * scale, 0.055f * scale), darkColor);
            for (var ear = 0; ear < 2; ear++)
            {
                var sign = ear == 0 ? -1f : 1f;
                Box(vertices, indices, colors, ref v, ref n, new Vector3(sign * (float)HornSpreadM * 0.62f * scale, headY + 0.045f * scale, headZ - 0.01f * scale),
                    new Vector3(0.045f * scale, 0.016f * scale, 0.032f * scale), woolColor);
            }

            // 角：羊王与冲撞羊才有（grunt/elite 不长角）
            if (kind == SheepKind.Ram || kind == SheepKind.King)
            {
                for (var horn = 0; horn < 2; horn++)
                {
                    var sign = horn == 0 ? -1f : 1f;
                    Box(vertices, indices, colors, ref v, ref n, new Vector3(sign * (float)HornSpreadM * scale, headY + 0.075f * scale, headZ - 0.02f * scale),
                        new Vector3(0.030f * scale, 0.030f * scale, 0.055f * scale), hornColor);
                }
            }

            // 尾巴
            Sphere(vertices, indices, colors, ref v, ref n, new Vector3(0f, bodyY + 0.02f * scale, -0.40f * scale), 0.070f * scale, woolColor);

            // 四条腿：从躯干下沿到地面（原来只是悬空的小方块）
            var upperLegY = bodyY - 0.10f * scale;
            var lowerLegY = 0.11f * scale;
            for (var leg = 0; leg < 4; leg++)
            {
                var signX = (leg & 1) == 0 ? -1f : 1f;
                var signZ = leg < 2 ? -1f : 1f;
                var x = signX * (float)LegOffsetX * scale;
                var z = signZ * (float)LegOffsetZ * 1.6f * scale;
                Box(vertices, indices, colors, ref v, ref n, new Vector3(x, upperLegY, z),
                    new Vector3((float)LegHalfM * 0.75f * scale, 0.085f * scale, (float)LegHalfM * 0.75f * scale), darkColor);
                Box(vertices, indices, colors, ref v, ref n, new Vector3(x, lowerLegY, z),
                    new Vector3((float)LegHalfM * 0.55f * scale, 0.11f * scale, (float)LegHalfM * 0.55f * scale), darkColor);
                Box(vertices, indices, colors, ref v, ref n, new Vector3(x, 0.018f * scale, z + 0.012f * scale),
                    new Vector3((float)LegHalfM * 0.75f * scale, 0.018f * scale, (float)LegHalfM * 1.15f * scale), hornColor);
            }

            var mesh = new Mesh();
            mesh.name = "Sheep" + kind;
            mesh.vertices = Slice(vertices, v);
            mesh.SetTriangles(Slice(indices, n), 0);
            mesh.colors32 = Slice(colors, v);
            mesh.RecalculateNormals();
            mesh.RecalculateBounds();
            return mesh;
        }

        // 预算（§5(a)：每形 ≤ 512 顶点 / 512 三角形）。球 = 8×5 段 = 54 顶点 / 80 三角形；方块 = 8/12。
        private const int MaxVerts = 512;
        private const int MaxIndices = 512 * 3;

        private static Vector3[] Slice(Vector3[] source, int count)
        {
            var result = new Vector3[count];
            System.Array.Copy(source, result, count);
            return result;
        }

        private static int[] Slice(int[] source, int count)
        {
            var result = new int[count];
            System.Array.Copy(source, result, count);
            return result;
        }

        private static Color32[] Slice(Color32[] source, int count)
        {
            var result = new Color32[count];
            System.Array.Copy(source, result, count);
            return result;
        }

        // UV 球（纬线分段，固定低分辨率：远处羊群不需要更多面）
        private static void Sphere(Vector3[] vertices, int[] indices, Color32[] colors, ref int v, ref int n,
            Vector3 center, float radius, Color32 color)
        {
            const int segments = 6;
            const int rings = 4;
            var baseIndex = v;
            for (var ring = 0; ring <= rings; ring++)
            {
                var phi = Mathf.PI * ring / rings;
                var y = Mathf.Cos(phi);
                var r = Mathf.Sin(phi);
                for (var seg = 0; seg <= segments; seg++)
                {
                    var theta = 2f * Mathf.PI * seg / segments;
                    vertices[v] = center + new Vector3(Mathf.Cos(theta) * r, y, Mathf.Sin(theta) * r) * radius;
                    colors[v] = color;
                    v += 1;
                }
            }
            for (var ring = 0; ring < rings; ring++)
            {
                for (var seg = 0; seg < segments; seg++)
                {
                    var a = baseIndex + ring * (segments + 1) + seg;
                    var b = a + segments + 1;
                    indices[n++] = a; indices[n++] = b; indices[n++] = a + 1;
                    indices[n++] = a + 1; indices[n++] = b; indices[n++] = b + 1;
                }
            }
        }

        // 轴对齐方块（8 顶点 / 12 三角形）
        private static void Box(Vector3[] vertices, int[] indices, Color32[] colors, ref int v, ref int n,
            Vector3 center, Vector3 half, Color32 color)
        {
            var x0 = center.x - half.x; var x1 = center.x + half.x;
            var y0 = center.y - half.y; var y1 = center.y + half.y;
            var z0 = center.z - half.z; var z1 = center.z + half.z;
            var baseIndex = v;
            vertices[v] = new Vector3(x0, y0, z0); colors[v] = color; v++;
            vertices[v] = new Vector3(x1, y0, z0); colors[v] = color; v++;
            vertices[v] = new Vector3(x1, y1, z0); colors[v] = color; v++;
            vertices[v] = new Vector3(x0, y1, z0); colors[v] = color; v++;
            vertices[v] = new Vector3(x0, y0, z1); colors[v] = color; v++;
            vertices[v] = new Vector3(x1, y0, z1); colors[v] = color; v++;
            vertices[v] = new Vector3(x1, y1, z1); colors[v] = color; v++;
            vertices[v] = new Vector3(x0, y1, z1); colors[v] = color; v++;
            Quad(indices, ref n, baseIndex + 0, baseIndex + 1, baseIndex + 2, baseIndex + 3);
            Quad(indices, ref n, baseIndex + 4, baseIndex + 5, baseIndex + 6, baseIndex + 7);
            Quad(indices, ref n, baseIndex + 0, baseIndex + 4, baseIndex + 5, baseIndex + 1);
            Quad(indices, ref n, baseIndex + 3, baseIndex + 7, baseIndex + 6, baseIndex + 2);
            Quad(indices, ref n, baseIndex + 0, baseIndex + 3, baseIndex + 7, baseIndex + 4);
            Quad(indices, ref n, baseIndex + 1, baseIndex + 2, baseIndex + 6, baseIndex + 5);
        }

        private static void Quad(int[] indices, ref int n, int a, int b, int c, int d)
        {
            indices[n++] = a; indices[n++] = b; indices[n++] = c;
            indices[n++] = a; indices[n++] = c; indices[n++] = d;
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
