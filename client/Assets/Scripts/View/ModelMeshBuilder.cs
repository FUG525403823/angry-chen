using System.Collections.Generic;
using UnityEngine;

namespace Ac.View
{
    // Startup-only construction. Face-local vertices keep panels, bevels and barrel facets crisp.
    internal sealed class ModelMeshBuilder
    {
        private readonly List<Vector3> _vertices = new List<Vector3>(1024);
        private readonly List<Vector3> _normals = new List<Vector3>(1024);
        private readonly List<Color32> _colors = new List<Color32>(1024);
        private readonly List<int> _indices = new List<int>(2048);

        public void BeveledBox(Vector3 center, Vector3 half, float bevel, Color32 color, Quaternion rotation)
        {
            var inset = half - Vector3.one * Mathf.Min(bevel, Mathf.Min(half.x, half.y, half.z) * .8f);
            // Six panels, twelve bevel strips and eight corners, preserving the original bounds.
            for (var axis = 0; axis < 3; axis++)
            {
                var u = (axis + 1) % 3; var v = (axis + 2) % 3;
                for (var side = -1; side <= 1; side += 2)
                {
                    var n = Vector3.zero; n[axis] = side;
                    var p = Vector3.zero; p[axis] = half[axis] * side;
                    var a = Vector3.zero; a[u] = inset[u];
                    var b = Vector3.zero; b[v] = inset[v];
                    Face(p - a - b, p + a - b, p + a + b, p - a + b, n, color, center, rotation);
                }
                for (var su = -1; su <= 1; su += 2)
                    for (var sv = -1; sv <= 1; sv += 2)
                    {
                        var a = Vector3.zero; a[axis] = -inset[axis]; a[u] = su * half[u]; a[v] = sv * inset[v];
                        var b = a; b[axis] = inset[axis];
                        var c = b; c[u] = su * inset[u]; c[v] = sv * half[v];
                        var d = c; d[axis] = -inset[axis];
                        var normal = Vector3.zero; normal[u] = su; normal[v] = sv;
                        Face(a, b, c, d, normal, color, center, rotation);
                    }
            }
            for (var x = -1; x <= 1; x += 2)
                for (var y = -1; y <= 1; y += 2)
                    for (var z = -1; z <= 1; z += 2)
                        Triangle(new Vector3(x * half.x, y * inset.y, z * inset.z),
                            new Vector3(x * inset.x, y * half.y, z * inset.z),
                            new Vector3(x * inset.x, y * inset.y, z * half.z),
                            new Vector3(x, y, z), color, center, rotation);
        }

        public void Panel(Vector3 center, Vector3 across, Vector3 up, Vector3 normal, Color32 color)
        {
            Face(center - across - up, center + across - up, center + across + up, center - across + up,
                normal, color, Vector3.zero, Quaternion.identity);
        }

        // Eight-sided barrel, with a rim, inside wall and recessed dark bore disk.
        public void Barrel(Vector3 center, float radius, float bore, float length, Color32 metal, Color32 dark)
        {
            const int sides = 8;
            var back = center.z - length * .5f; var front = center.z + length * .5f;
            var recess = front - Mathf.Min(.022f, length * .65f);
            for (var i = 0; i < sides; i++)
            {
                var angle = 2f * Mathf.PI * i / sides; var next = 2f * Mathf.PI * (i + 1) / sides;
                var a = new Vector3(Mathf.Cos(angle), Mathf.Sin(angle), 0f);
                var b = new Vector3(Mathf.Cos(next), Mathf.Sin(next), 0f);
                var normal = (a + b).normalized;
                var offset = new Vector3(center.x, center.y, 0f);
                var ab = offset + a * radius + Vector3.forward * back;
                var bb = offset + b * radius + Vector3.forward * back;
                var af = offset + a * radius + Vector3.forward * front;
                var bf = offset + b * radius + Vector3.forward * front;
                Face(ab, bb, bf, af, normal, metal, Vector3.zero, Quaternion.identity);
                Triangle(offset + Vector3.forward * back, bb, ab, Vector3.back, metal, Vector3.zero, Quaternion.identity);
                var ai = offset + a * bore + Vector3.forward * front;
                var bi = offset + b * bore + Vector3.forward * front;
                var ar = offset + a * bore + Vector3.forward * recess;
                var br = offset + b * bore + Vector3.forward * recess;
                Face(af, bf, bi, ai, Vector3.forward, metal, Vector3.zero, Quaternion.identity);
                Face(ai, bi, br, ar, -normal, dark, Vector3.zero, Quaternion.identity);
                Triangle(offset + Vector3.forward * recess, ar, br, Vector3.forward, dark, Vector3.zero, Quaternion.identity);
            }
        }

        private void Face(Vector3 a, Vector3 b, Vector3 c, Vector3 d, Vector3 outward, Color32 color, Vector3 center, Quaternion rotation)
        {
            if (Vector3.Dot(Vector3.Cross(b - a, c - a), outward) < 0f) { var swap = b; b = d; d = swap; }
            var normal = rotation * FaceNormal(a, b, c);
            var start = _vertices.Count;
            Vertex(center + rotation * a, normal, color); Vertex(center + rotation * b, normal, color);
            Vertex(center + rotation * c, normal, color); Vertex(center + rotation * d, normal, color);
            _indices.Add(start); _indices.Add(start + 1); _indices.Add(start + 2);
            _indices.Add(start); _indices.Add(start + 2); _indices.Add(start + 3);
        }

        private void Triangle(Vector3 a, Vector3 b, Vector3 c, Vector3 outward, Color32 color, Vector3 center, Quaternion rotation)
        {
            if (Vector3.Dot(Vector3.Cross(b - a, c - a), outward) < 0f) { var swap = b; b = c; c = swap; }
            var normal = rotation * FaceNormal(a, b, c);
            var start = _vertices.Count;
            Vertex(center + rotation * a, normal, color); Vertex(center + rotation * b, normal, color); Vertex(center + rotation * c, normal, color);
            _indices.Add(start); _indices.Add(start + 1); _indices.Add(start + 2);
        }

        private void Vertex(Vector3 point, Vector3 normal, Color32 color)
        {
            _vertices.Add(point); _normals.Add(normal); _colors.Add(color);
        }

        private static Vector3 FaceNormal(Vector3 a, Vector3 b, Vector3 c)
        {
            var cross = Vector3.Cross(b - a, c - a);
            // Vector3.normalized zeros vectors below 1e-5: valid millimeter-scale bevels are smaller.
            return cross / Mathf.Sqrt(cross.sqrMagnitude);
        }

        public Mesh Finish(string name)
        {
            var mesh = new Mesh { name = name };
            mesh.SetVertices(_vertices); mesh.SetNormals(_normals); mesh.SetColors(_colors);
            mesh.SetTriangles(_indices, 0); mesh.RecalculateBounds();
            return mesh;
        }
    }
}
