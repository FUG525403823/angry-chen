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
        public const double ClusterScaleMin = 0.94;
        public const double ClusterScaleMax = 1.06;

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

        // A compact fleece silhouette, short legs and an expressive face, within the frozen budgets.
        public static Mesh Build(SheepKind kind)
        {
            var mesh = new Builder();
            var wool = ColorOf(kind);
            var fleece = Shade(wool, 8);
            var face = new Color32(83, 68, 65, 255);
            var muzzle = new Color32(143, 103, 87, 255);
            var hoof = new Color32(38, 35, 39, 255);
            var bone = new Color32(213, 175, 112, 255);
            var cheek = new Color32(192, 126, 117, 255);
            var width = kind == SheepKind.Ram ? 1.15f : kind == SheepKind.Elite ? .90f : kind == SheepKind.King ? 1.20f : 1f;
            var headWidth = kind == SheepKind.Ram || kind == SheepKind.King ? .235f : .215f;

            // One continuous scalloped fleece surface avoids intersecting, pointed wool clumps.
            mesh.Ellipsoid(new Vector3(0, .50f, -.10f), new Vector3(.36f * width, .29f, .46f), wool, 16, 4, true);
            mesh.Ellipsoid(new Vector3(0, .59f, .28f), new Vector3(.205f, .20f, .22f), wool, 8, 1);
            mesh.Ellipsoid(new Vector3(0, .655f, .46f), new Vector3(headWidth, .202f, .195f), face, 8, 2);
            mesh.Ellipsoid(new Vector3(0, .565f, .595f), new Vector3(.17f, .095f, .112f), muzzle, 6, 2);
            mesh.Ellipsoid(new Vector3(0, .80f, .407f), new Vector3(.21f, .10f, .174f), fleece, 8, 3);
            mesh.Ellipsoid(new Vector3(0, .51f, -.51f), new Vector3(.09f, .105f, .13f), fleece, 6, 1);

            for (var side = -1; side <= 1; side += 2)
            {
                mesh.Ellipsoid(new Vector3(side * .235f, .719f, .415f), new Vector3(.145f, .055f, .078f), face, 6, 1);
                mesh.Diamond(new Vector3(side * .285f, .722f, .471f), new Vector3(.064f, side * -.014f, 0), new Vector3(0, .022f, 0), cheek);
                // Shallow eyes sit inside the face rather than projecting like spheres.
                var eye = new Vector3(side * .12f, .681f, .642f);
                mesh.Eye(eye, .049f, .052f, new Color32(250, 239, 216, 255));
                mesh.Diamond(eye + new Vector3(-side * .006f, -.002f, .001f), new Vector3(.019f, 0, 0), new Vector3(0, .029f, 0), hoof);
                mesh.Diamond(eye + new Vector3(-side * .011f, .012f, .002f), new Vector3(.006f, 0, 0), new Vector3(0, .008f, 0), new Color32(255, 255, 255, 255));
                var frown = kind == SheepKind.Grunt ? -.002f : kind == SheepKind.Elite ? .018f : .026f;
                mesh.Diamond(new Vector3(side * .12f, .738f, .643f), new Vector3(.052f, side * frown, 0), new Vector3(0, .011f, 0), hoof);
                mesh.Diamond(new Vector3(side * .052f, .580f, .687f), new Vector3(.014f, 0, 0), new Vector3(0, .008f, 0), hoof);
            }
            mesh.Diamond(new Vector3(0, .538f, .697f), new Vector3(.045f, 0, 0), new Vector3(0, .006f, 0), hoof);
            for (var leg = 0; leg < 4; leg++)
            {
                var x = ((leg & 1) == 0 ? -1f : 1f) * .20f * width;
                var z = leg < 2 ? -.29f : .175f;
                mesh.Box(new Vector3(x, .18f, z), new Vector3(.049f, .125f, .052f), face);
                mesh.Box(new Vector3(x, .047f, z + .012f), new Vector3(.075f, .047f, .09f), hoof);
            }
            if (kind == SheepKind.Ram || kind == SheepKind.King)
            {
                mesh.Horn(-1, bone); mesh.Horn(1, bone);
            }
            if (kind == SheepKind.King)
            {
                for (var spike = -1; spike <= 1; spike++)
                    mesh.Ellipsoid(new Vector3(spike * .105f, .86f, .385f),
                        new Vector3(.04f, spike == 0 ? .115f : .09f, .045f), bone, 4, 1);
            }
            return mesh.Finish("Sheep" + kind, (float)(Form(kind).HeightM / Form(kind).Scale));
        }

        public static Vector3 EmblemAnchor(SheepKind kind)
        {
            var factor = LocalHeightFactor(kind);
            // Follow the new fleece forehead with the live -55 degree emblem tilt.
            return new Vector3(0, .865f * factor, .540f * factor);
        }

        private static float LocalHeightFactor(SheepKind kind)
        {
            var designHeight = kind == SheepKind.King ? .975f : .9f;
            return (float)(Form(kind).HeightM / Form(kind).Scale) / designHeight;
        }

        private static Color32 Shade(Color32 color, int amount)
        {
            return new Color32((byte)System.Math.Min(255, color.r + amount),
                (byte)System.Math.Min(255, color.g + amount), (byte)System.Math.Min(255, color.b + amount), 255);
        }

        private sealed class Builder
        {
            private readonly Vector3[] vertices = new Vector3[VertexBudgetPerForm];
            private readonly Vector3[] normals = new Vector3[VertexBudgetPerForm];
            private readonly Color32[] colors = new Color32[VertexBudgetPerForm];
            private readonly int[] indices = new int[TriangleBudgetPerForm * 3];
            private int vertexCount;
            private int indexCount;

            private int Vertex(Vector3 position, Vector3 normal, Color32 color)
            {
                if (vertexCount == vertices.Length) throw new System.InvalidOperationException("Sheep vertex budget exceeded");
                var index = vertexCount++;
                vertices[index] = position;
                normals[index] = normal.normalized;
                colors[index] = color;
                return index;
            }

            private void Face(int a, int b, int c)
            {
                if (indexCount + 3 > indices.Length) throw new System.InvalidOperationException("Sheep triangle budget exceeded");
                var cross = Vector3.Cross(vertices[b] - vertices[a], vertices[c] - vertices[a]);
                if (Vector3.Dot(cross, normals[a] + normals[b] + normals[c]) < 0f)
                { var swap = b; b = c; c = swap; }
                indices[indexCount++] = a; indices[indexCount++] = b; indices[indexCount++] = c;
            }

            public void Ellipsoid(Vector3 center, Vector3 radii, Color32 color, int segments, int rings, bool scalloped = false)
            {
                var top = Vertex(center + new Vector3(0f, radii.y, 0f), Vector3.up, color);
                var first = vertexCount;
                for (var ring = 1; ring <= rings; ring++)
                {
                    var phi = Mathf.PI * ring / (rings + 1);
                    for (var segment = 0; segment < segments; segment++)
                    {
                        var theta = 2f * Mathf.PI * segment / segments;
                        var unit = new Vector3(Mathf.Cos(theta) * Mathf.Sin(phi), Mathf.Cos(phi), Mathf.Sin(theta) * Mathf.Sin(phi));
                        var point = Vector3.Scale(unit, radii);
                        if (scalloped)
                        {
                            var puff = 1f + .055f * Mathf.Cos(theta * 6f + phi * 3f) * Mathf.Sin(phi);
                            point.x *= puff; point.z *= puff;
                        }
                        Vertex(center + point, new Vector3(unit.x / radii.x, unit.y / radii.y, unit.z / radii.z), color);
                    }
                }
                var bottom = Vertex(center - new Vector3(0f, radii.y, 0f), -Vector3.up, color);
                for (var segment = 0; segment < segments; segment++)
                {
                    var next = (segment + 1) % segments;
                    Face(top, first + segment, first + next);
                    for (var ring = 0; ring < rings - 1; ring++)
                    {
                        var a = first + ring * segments + segment;
                        var b = first + ring * segments + next;
                        Face(a, a + segments, b);
                        Face(b, a + segments, b + segments);
                    }
                    Face(bottom, first + (rings - 1) * segments + next, first + (rings - 1) * segments + segment);
                }
            }

            public void Eye(Vector3 center, float width, float height, Color32 color)
            {
                const int segments = 8;
                var middle = Vertex(center, Vector3.forward, color);
                for (var i = 0; i < segments; i++)
                {
                    var angle = 2f * Mathf.PI * i / segments;
                    Vertex(center + new Vector3(Mathf.Cos(angle) * width, Mathf.Sin(angle) * height, 0), Vector3.forward, color);
                }
                for (var i = 0; i < segments; i++) Face(middle, middle + 1 + i, middle + 1 + (i + 1) % segments);
            }

            public void Diamond(Vector3 center, Vector3 across, Vector3 up, Color32 color)
            {
                var normal = Vector3.Cross(across, up).normalized;
                var a = Vertex(center - across, normal, color);
                var b = Vertex(center + up, normal, color);
                var c = Vertex(center + across, normal, color);
                var d = Vertex(center - up, normal, color);
                Face(a, b, c); Face(a, c, d);
            }

            private void Quad(Vector3 a, Vector3 b, Vector3 c, Vector3 d, Color32 color)
            {
                var normal = Vector3.Cross(b - a, c - a).normalized;
                var start = Vertex(a, normal, color);
                Vertex(b, normal, color); Vertex(c, normal, color); Vertex(d, normal, color);
                Face(start, start + 1, start + 2); Face(start, start + 2, start + 3);
            }

            public void Box(Vector3 center, Vector3 half, Color32 color)
            {
                var a = center - half;
                var b = center + half;
                Quad(new Vector3(a.x, a.y, a.z), new Vector3(a.x, b.y, a.z), new Vector3(b.x, b.y, a.z), new Vector3(b.x, a.y, a.z), color);
                Quad(new Vector3(a.x, a.y, b.z), new Vector3(b.x, a.y, b.z), new Vector3(b.x, b.y, b.z), new Vector3(a.x, b.y, b.z), color);
                Quad(new Vector3(a.x, a.y, a.z), new Vector3(b.x, a.y, a.z), new Vector3(b.x, a.y, b.z), new Vector3(a.x, a.y, b.z), color);
                Quad(new Vector3(a.x, b.y, a.z), new Vector3(a.x, b.y, b.z), new Vector3(b.x, b.y, b.z), new Vector3(b.x, b.y, a.z), color);
                Quad(new Vector3(a.x, a.y, a.z), new Vector3(a.x, a.y, b.z), new Vector3(a.x, b.y, b.z), new Vector3(a.x, b.y, a.z), color);
                Quad(new Vector3(b.x, a.y, a.z), new Vector3(b.x, b.y, a.z), new Vector3(b.x, b.y, b.z), new Vector3(b.x, a.y, b.z), color);
            }

            public void Horn(int side, Color32 color)
            {
                const int steps = 5;
                const int segments = 3;
                var first = vertexCount;
                for (var step = 0; step <= steps; step++)
                {
                    var angle = (-55f + step * 300f / steps) * Mathf.PI / 180f;
                    var radius = .15f - step * .006f;
                    var center = new Vector3(side * (.235f + .075f * step / steps),
                        .715f + Mathf.Cos(angle) * radius, .365f + Mathf.Sin(angle) * radius);
                    var radial = new Vector3(0f, Mathf.Cos(angle), Mathf.Sin(angle));
                    var thickness = 0.055f * (1f - 0.80f * step / steps);
                    for (var segment = 0; segment < segments; segment++)
                    {
                        var around = 2f * Mathf.PI * segment / segments;
                        var normal = new Vector3(side * Mathf.Cos(around), 0f, 0f) + radial * Mathf.Sin(around);
                        Vertex(center + normal * thickness, normal, color);
                    }
                }
                for (var step = 0; step < steps; step++)
                {
                    for (var segment = 0; segment < segments; segment++)
                    {
                        var a = first + step * segments + segment;
                        var b = first + step * segments + (segment + 1) % segments;
                        Face(a, b, a + segments); Face(b, b + segments, a + segments);
                    }
                }
                var end = first + steps * segments;
                var capNormal = Vector3.Cross(vertices[end + 1] - vertices[end], vertices[end + 2] - vertices[end]).normalized;
                var cap = Vertex(vertices[end], capNormal, color);
                Vertex(vertices[end + 1], capNormal, color); Vertex(vertices[end + 2], capNormal, color);
                Face(cap, cap + 1, cap + 2);
            }

            public Mesh Finish(string name, float targetHeight)
            {
                var minY = float.MaxValue; var maxY = float.MinValue;
                for (var i = 0; i < vertexCount; i++)
                { minY = System.Math.Min(minY, vertices[i].y); maxY = System.Math.Max(maxY, vertices[i].y); }
                var factor = targetHeight / (maxY - minY);
                for (var i = 0; i < vertexCount; i++) vertices[i] = (vertices[i] - new Vector3(0f, minY, 0f)) * factor;
                var mesh = new Mesh();
                mesh.name = name;
                mesh.vertices = Copy(vertices, vertexCount);
                mesh.normals = Copy(normals, vertexCount);
                mesh.colors32 = Copy(colors, vertexCount);
                mesh.SetTriangles(Copy(indices, indexCount), 0);
                mesh.RecalculateBounds();
                return mesh;
            }

            private static T[] Copy<T>(T[] source, int count)
            {
                var result = new T[count];
                System.Array.Copy(source, result, count);
                return result;
            }
        }

        // §5(d)：额标是程序化网格 + 顶点色，不引用任何贴图
        public static Mesh BuildEmblem(SheepKind kind)
        {
            var form = Form(kind);
            var size = (float)(form.EmblemSizeM / form.Scale) * .50f;
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
            var faces = new[] { 0, 2, 1, 0, 3, 2, 0, 4, 3, 0, 1, 4 };
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
            indices[cursor + 1] = v + 2;
            indices[cursor + 2] = v + 1;
            indices[cursor + 3] = v;
            indices[cursor + 4] = v + 3;
            indices[cursor + 5] = v + 2;
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
            if (kind == SheepKind.Ram) return ArtPalette.Hex(0xD7BC92);
            if (kind == SheepKind.Elite) return ArtPalette.Hex(0xC5E2ED);
            if (kind == SheepKind.King) return ArtPalette.Hex(0xEDD4AA);
            return ArtPalette.Hex(0xF3E8D2);
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


    }
}
