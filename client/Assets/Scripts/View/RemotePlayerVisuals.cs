using System;
using Ac.Net;
using UnityEngine;

namespace Ac.View
{
    // Owns only presentation resources; snapshot identity and collision remain elsewhere.
    public sealed class RemotePlayerVisuals : IDisposable
    {
        private sealed class Model
        {
            internal GameObject Root;
            internal Transform Body, Head, Arms, LeftLeg, RightLeg;
            internal ushort Id;
            internal uint Lifetime;
            internal bool Active;
            internal Vector3 Previous;
            internal float Phase;
            internal int Renderers;
            internal int Triangles;
            internal MeshRenderer[] Parts;
            internal int[] Groups;
        }

        private readonly GameObject _root;
        private readonly Mesh _cube;
        private readonly Mesh _gun;
        private readonly Material[] _materials;
        private readonly Model[] _models = new Model[EntityViews.Capacity];
        private readonly int[] _byId = new int[EntityViews.Capacity + 1];
        private readonly Matrix4x4[] _batch = new Matrix4x4[1023];
        private int _created;
        private bool _disposed;

        public int Count { get; private set; }
        // Enabled model geometry before Unity camera culling/batching, not measured engine draws.
        public int RendererCount { get; private set; }
        public int SubmittedDraws { get; private set; }
        public int SubmittedInstances { get; private set; }
        public int TriangleCount { get; private set; }
        public int CreatedCount { get { return _created; } }
        public GameObject Root { get { return _root; } }

        public RemotePlayerVisuals(Transform parent)
        {
            _root = new GameObject("Ac.RemotePlayers");
            _root.transform.SetParent(parent, false);
            _cube = BuildCube();
            _gun = WeaponMesh.BuildBody(1);
            _materials = new[]
            {
                ArenaMaterials.CreateInstanced("Ac/ChenUniform", ArtPalette.Hex(0x36566B)),
                ArenaMaterials.CreateInstanced("Ac/ChenSkin", ArtPalette.Hex(0xE3B58E)),
                ArenaMaterials.CreateInstanced("Ac/ChenDark", ArtPalette.Hex(0x292B30)),
                ArenaMaterials.CreateInstanced("Ac/ChenBadge", ArtPalette.Hex(0xEAC75D)),
            };
        }

        public int SyncFrame(EntityViews views, ushort localPlayerId, double dtMs)
        {
            if (_disposed) return 0;
            // Release first, so a replacement identity can reuse a slot in this same frame.
            for (var i = 0; i < _created; i++)
            {
                var model = _models[i];
                if (!model.Active) continue;
                EntityView view;
                if (views != null && views.TryGet(model.Id, out view) && Eligible(view, localPlayerId) && model.Lifetime == view.Lifetime) continue;
                _byId[model.Id] = 0;
                model.Active = false;
                model.Root.SetActive(false);
            }
            Count = 0;
            RendererCount = 0;
            SubmittedDraws = 0;
            SubmittedInstances = 0;
            TriangleCount = 0;
            if (views == null) return 0;
            for (var i = 0; i < views.ActiveCount; i++)
            {
                EntityView view;
                if (!views.TryGet(views.ActiveIdAt(i), out view) || !Eligible(view, localPlayerId)) continue;
                var position = new Vector3((float)view.RenderX, (float)view.RenderY, (float)view.RenderZ);
                var handle = _byId[view.Id] - 1;
                if (handle < 0)
                {
                    handle = 0;
                    while (handle < _created && _models[handle].Active) handle++;
                    if (handle == _created) _models[_created++] = CreateModel();
                    var fresh = _models[handle];
                    fresh.Id = view.Id;
                    fresh.Lifetime = view.Lifetime;
                    fresh.Active = true;
                    fresh.Previous = position;
                    fresh.Phase = 0f;
                    fresh.Root.SetActive(true);
                    _byId[view.Id] = handle + 1;
                }
                var active = _models[handle];
                Pose(active, view, position, dtMs);
                Count++;
                RendererCount += active.Renderers;
                TriangleCount += active.Triangles;
            }
            Submit();
            return Count;
        }

        private void Submit()
        {
            SubmittedDraws = 0;
            if (!SystemInfo.supportsInstancing) { SubmittedDraws = RendererCount; SubmittedInstances = RendererCount; return; }
            for (var group = 0; group < 5; group++)
            {
                var count = 0;
                for (var i = 0; i < _created; i++)
                {
                    var model = _models[i];
                    if (!model.Active) continue;
                    for (var part = 0; part < model.Parts.Length; part++)
                    {
                        if (model.Groups[part] != group) continue;
                        _batch[count++] = model.Parts[part].transform.localToWorldMatrix;
                        if (count == _batch.Length) { DrawGroup(group, count); count = 0; }
                    }
                }
                if (count > 0) DrawGroup(group, count);
            }
        }

        private void DrawGroup(int group, int count)
        {
            Graphics.DrawMeshInstanced(group == 4 ? _gun : _cube, 0, _materials[group == 4 ? 2 : group], _batch, count);
            SubmittedDraws++;
            SubmittedInstances += count;
        }

        private static bool Eligible(EntityView view, ushort localPlayerId)
        {
            return view.Visible && view.Kind == 0 && view.Id != localPlayerId;
        }

        private static void Pose(Model model, EntityView view, Vector3 position, double dtMs)
        {
            var downed = (view.Flags & EntityRecord.DownedFlag) != 0;
            var delta = position - model.Previous;
            delta.y = 0f;
            var distance = delta.magnitude;
            var moving = !downed && dtMs > 0 && distance > 0.0001f && distance < 2f;
            if (moving) model.Phase = (model.Phase + distance * 9f) % (Mathf.PI * 2f);
            var stride = moving ? Mathf.Sin(model.Phase) * 28f : 0f;
            model.Previous = position;
            model.Root.transform.SetPositionAndRotation(position, Quaternion.Euler(0f, (float)view.YawRad * Mathf.Rad2Deg, 0f));
            model.Body.localPosition = downed ? new Vector3(0f, 0.5f, 0f) : Vector3.zero;
            model.Body.localRotation = Quaternion.Euler(downed ? 90f : 0f, 0f, 0f);
            var pitch = downed ? 0f : -(float)view.PitchRad * Mathf.Rad2Deg;
            model.Head.localRotation = Quaternion.Euler(pitch, 0f, 0f);
            model.Arms.localRotation = Quaternion.Euler(pitch, 0f, 0f);
            model.LeftLeg.localRotation = Quaternion.Euler(stride, 0f, 0f);
            model.RightLeg.localRotation = Quaternion.Euler(-stride, 0f, 0f);
        }

        private Model CreateModel()
        {
            var model = new Model();
            model.Root = new GameObject("ChenSir");
            model.Root.transform.SetParent(_root.transform, false);
            model.Body = Pivot("Body", model.Root.transform, Vector3.zero);
            Part("Torso", model.Body, new Vector3(0f, 1.13f, 0f), new Vector3(.48f, .58f, .27f), 0);
            Part("Belt", model.Body, new Vector3(0f, .86f, 0f), new Vector3(.49f, .09f, .29f), 2);
            Part("Badge", model.Body, new Vector3(-.12f, 1.27f, .145f), new Vector3(.075f, .09f, .025f), 3);
            model.Head = Pivot("HeadAim", model.Body, new Vector3(0f, 1.48f, 0f));
            Part("Head", model.Head, new Vector3(0f, .1f, 0f), new Vector3(.30f, .34f, .29f), 1);
            Part("Hair", model.Head, new Vector3(0f, .25f, -.015f), new Vector3(.32f, .10f, .30f), 2);
            Part("Brow", model.Head, new Vector3(0f, .15f, .151f), new Vector3(.24f, .035f, .025f), 2);
            Part("Nose", model.Head, new Vector3(0f, .07f, .17f), new Vector3(.065f, .08f, .07f), 1);
            model.Arms = Pivot("Aim", model.Body, new Vector3(0f, 1.32f, 0f));
            Part("LeftArm", model.Arms, new Vector3(-.24f, -.09f, .20f), new Vector3(.16f, .18f, .48f), 0);
            Part("RightArm", model.Arms, new Vector3(.24f, -.09f, .20f), new Vector3(.16f, .18f, .48f), 0);
            Part("LeftHand", model.Arms, new Vector3(-.15f, -.09f, .43f), new Vector3(.16f, .14f, .15f), 1);
            Part("RightHand", model.Arms, new Vector3(.16f, -.09f, .43f), new Vector3(.16f, .14f, .15f), 1);
            var gun = Part("Rifle", model.Arms, new Vector3(.08f, -.04f, .40f), Vector3.one, 2);
            gun.GetComponent<MeshFilter>().sharedMesh = _gun;
            model.LeftLeg = Leg("LeftLeg", model.Body, -.14f);
            model.RightLeg = Leg("RightLeg", model.Body, .14f);
            // Model topology is immutable after construction. Mesh.triangles and hierarchy queries
            // allocate, so cache their actual counts here rather than on the frame path.
            var renderers = model.Root.GetComponentsInChildren<MeshRenderer>();
            model.Parts = renderers;
            model.Groups = new int[renderers.Length];
            for (var i = 0; i < renderers.Length; i++)
            {
                var renderer = renderers[i];
                var mesh = renderer.GetComponent<MeshFilter>().sharedMesh;
                model.Groups[i] = mesh == _gun ? 4 : Array.IndexOf(_materials, renderer.sharedMaterial);
                renderer.forceRenderingOff = SystemInfo.supportsInstancing;
            }
            model.Renderers = renderers.Length;
            for (var i = 0; i < renderers.Length; i++)
                model.Triangles += renderers[i].GetComponent<MeshFilter>().sharedMesh.triangles.Length / 3;
            return model;
        }

        private Transform Leg(string name, Transform parent, float x)
        {
            var leg = Pivot(name, parent, new Vector3(x, .82f, 0f));
            Part("Trouser", leg, new Vector3(0f, -.35f, 0f), new Vector3(.20f, .70f, .22f), 0);
            Part("Boot", leg, new Vector3(0f, -.74f, .05f), new Vector3(.22f, .16f, .34f), 2);
            return leg;
        }

        private static Transform Pivot(string name, Transform parent, Vector3 position)
        {
            var node = new GameObject(name).transform;
            node.SetParent(parent, false);
            node.localPosition = position;
            return node;
        }

        private GameObject Part(string name, Transform parent, Vector3 position, Vector3 size, int material)
        {
            var node = Pivot(name, parent, position);
            node.localScale = size;
            node.gameObject.AddComponent<MeshFilter>().sharedMesh = _cube;
            node.gameObject.AddComponent<MeshRenderer>().sharedMaterial = _materials[material];
            return node.gameObject;
        }

        private static Mesh BuildCube()
        {
            // Four separate vertices per face keep the low-poly silhouette flat shaded.
            var corners = new[] { new Vector3(-.5f,-.5f,-.5f), new Vector3(.5f,-.5f,-.5f), new Vector3(.5f,.5f,-.5f), new Vector3(-.5f,.5f,-.5f), new Vector3(-.5f,-.5f,.5f), new Vector3(.5f,-.5f,.5f), new Vector3(.5f,.5f,.5f), new Vector3(-.5f,.5f,.5f) };
            var faces = new[] { 0,3,2,1, 4,5,6,7, 0,1,5,4, 3,7,6,2, 0,4,7,3, 1,2,6,5 };
            var vertices = new Vector3[24];
            var triangles = new int[36];
            for (var f = 0; f < 6; f++)
            {
                for (var v = 0; v < 4; v++) vertices[f * 4 + v] = corners[faces[f * 4 + v]];
                var t = f * 6; var b = f * 4;
                triangles[t] = b; triangles[t+1] = b+1; triangles[t+2] = b+2;
                triangles[t+3] = b; triangles[t+4] = b+2; triangles[t+5] = b+3;
            }
            var mesh = new Mesh { name = "ChenSirCube", vertices = vertices, triangles = triangles };
            mesh.RecalculateNormals();
            mesh.RecalculateBounds();
            return mesh;
        }

        public void Dispose()
        {
            if (_disposed) return;
            _disposed = true;
            Count = 0;
            RendererCount = 0;
            TriangleCount = 0;
            _root.SetActive(false);
            Kill(_root);
            Kill(_cube);
            Kill(_gun);
            for (var i = 0; i < _materials.Length; i++) Kill(_materials[i]);
        }

        private static void Kill(UnityEngine.Object resource)
        {
            if (resource == null) return;
            if (Application.isPlaying) UnityEngine.Object.Destroy(resource);
            else UnityEngine.Object.DestroyImmediate(resource);
        }
    }
}
