using Ac.Core;
using Ac.View;
using UnityEngine;

namespace Ac.Tests
{
    public static class ModelGeometrySuite
    {
        public static void Register()
        {
            SelfTest.Add("models.weapon_surface", ChecksWeaponSurface);
            SelfTest.Add("models.weapon_muzzle_attachment", ChecksMuzzleAttachment);
            SelfTest.Add("models.emblem_surface_attachment", ChecksEmblemSurfaceAttachment);
        }

        private static void ChecksWeaponSurface()
        {
            for (var slot = 0; slot < WeaponMesh.SlotCount; slot++)
            {
                var body = WeaponMesh.BuildBody(slot);
                var accent = WeaponMesh.BuildAccent(slot);
                try
                {
                    CheckSurface(body);
                    CheckSurface(accent);
                    SelfTest.True(body.vertexCount + accent.vertexCount <= 1536,
                        "weapon vertex budget", slot.ToString());
                    SelfTest.True((body.triangles.Length + accent.triangles.Length) / 3 <= 768,
                        "weapon triangle budget", slot.ToString());
                }
                finally { Object.DestroyImmediate(body); Object.DestroyImmediate(accent); }
            }
        }

        private static void CheckSurface(Mesh mesh)
        {
            var vertices = mesh.vertices;
            var normals = mesh.normals;
            var triangles = mesh.triangles;
            SelfTest.Equal(vertices.Length, normals.Length);
            SelfTest.Equal(vertices.Length, mesh.colors32.Length);
            for (var i = 0; i < triangles.Length; i += 3)
            {
                var a = triangles[i]; var b = triangles[i + 1]; var c = triangles[i + 2];
                var cross = Vector3.Cross(vertices[b] - vertices[a], vertices[c] - vertices[a]);
                SelfTest.True(cross.sqrMagnitude > 1e-16f, "nondegenerate weapon surface", mesh.name);
                var normal = cross / Mathf.Sqrt(cross.sqrMagnitude);
                // Flat panels and bevels must retain their own normals, including at shared corners.
                SelfTest.True(Vector3.Dot(normal, normals[a]) > .98f &&
                    Vector3.Dot(normal, normals[b]) > .98f && Vector3.Dot(normal, normals[c]) > .98f,
                    "hard weapon edges must retain face normals and outward winding",
                    mesh.name + " triangle=" + i / 3 + " area2=" + cross.magnitude + " normal=" + normals[a]);
            }
        }

        private static void ChecksMuzzleAttachment()
        {
            for (var slot = 0; slot < WeaponMesh.SlotCount; slot++)
            {
                var mesh = WeaponMesh.BuildAccent(slot);
                try
                {
                    SelfTest.True(Mathf.Abs(mesh.bounds.max.z - WeaponMesh.MuzzleLocal.z) < .0001f,
                        "barrel ends at the shared muzzle flash anchor", slot.ToString());
                    var muzzle = WeaponMesh.MuzzleLocal;
                    SelfTest.True(mesh.bounds.min.x < muzzle.x && mesh.bounds.max.x > muzzle.x &&
                        mesh.bounds.min.y < muzzle.y && mesh.bounds.max.y > muzzle.y,
                        "muzzle lies inside the barrel cross section", slot.ToString());
                }
                finally { Object.DestroyImmediate(mesh); }
            }
        }

        private static void ChecksEmblemSurfaceAttachment()
        {
            var inward = -(Quaternion.Euler(-55f, 0, 0) * Vector3.forward);
            for (var k = 0; k < SheepMesh.FormCount; k++)
            {
                var kind = (SheepKind)k;
                var mesh = SheepMesh.Build(kind);
                try
                {
                    var origin = SheepMesh.EmblemAnchor(kind);
                    var vertices = mesh.vertices; var triangles = mesh.triangles;
                    var nearest = float.PositiveInfinity;
                    for (var i = 0; i < triangles.Length; i += 3)
                    {
                        var a = vertices[triangles[i]];
                        var edge1 = vertices[triangles[i + 1]] - a;
                        var edge2 = vertices[triangles[i + 2]] - a;
                        var cross = Vector3.Cross(inward, edge2);
                        var determinant = Vector3.Dot(edge1, cross);
                        if (Mathf.Abs(determinant) < 1e-8f) continue;
                        var offset = origin - a;
                        var u = Vector3.Dot(offset, cross) / determinant;
                        if (u < 0 || u > 1) continue;
                        var q = Vector3.Cross(offset, edge1);
                        var v = Vector3.Dot(inward, q) / determinant;
                        if (v < 0 || u + v > 1) continue;
                        var distance = Vector3.Dot(edge2, q) / determinant;
                        if (distance >= 0) nearest = Mathf.Min(nearest, distance);
                    }
                    var worldGap = nearest * SheepMesh.FormScale(kind);
                    SelfTest.True(worldGap < .015f, "emblem rests within 1.5cm of the forehead",
                        kind + " gap=" + worldGap);
                }
                finally { Object.DestroyImmediate(mesh); }
            }
        }
    }
}
