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
        private readonly Material _gunMaterial;
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
            _gun = WeaponMesh.BuildComplete(1);
            _gunMaterial = WeaponMesh.CreateMaterial("Ac/ChenRifle");
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
            Graphics.DrawMeshInstanced(group == 4 ? _gun : _cube, 0, (group == 4 ? _gunMaterial : _materials[group]), _batch, count);
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
            Part("Torso", model.Body, new Vector3(0, 1.105f, 0), new Vector3(.50f, .55f, .30f), 0);
            Part("Belt", model.Body, new Vector3(0, .848f, 0), new Vector3(.51f, .075f, .32f), 2);
            Part("Buckle", model.Body, new Vector3(0, .848f, .17f), new Vector3(.083f, .059f, .025f), 3);
            Part("Neck", model.Body, new Vector3(0, 1.40f, 0), new Vector3(.18f, .14f, .19f), 1);
            for (var side = -1; side <= 1; side += 2)
            {
                var collar = Part(side < 0 ? "LeftCollar" : "RightCollar", model.Body,
                    new Vector3(side * .085f, 1.348f, .154f), new Vector3(.11f, .065f, .035f), 2);
                collar.transform.localRotation = Quaternion.Euler(0, 0, side * 24f);
                Part(side < 0 ? "LeftEpaulet" : "RightEpaulet", model.Body,
                    new Vector3(side * .212f, 1.38f, .005f), new Vector3(.115f, .027f, .23f), 3);
                Part(side < 0 ? "LeftPocket" : "RightPocket", model.Body,
                    new Vector3(side * .14f, 1.13f, .154f), new Vector3(.12f, .115f, .025f), 0);
                Part(side < 0 ? "LeftPocketFlap" : "RightPocketFlap", model.Body,
                    new Vector3(side * .14f, 1.184f, .17f), new Vector3(.13f, .030f, .025f), 2);
            }
            Part("Tie", model.Body, new Vector3(0, 1.227f, .16f), new Vector3(.040f, .16f, .026f), 2);
            Part("Badge", model.Body, new Vector3(-.14f, 1.27f, .17f), new Vector3(.068f, .082f, .028f), 3);
            model.Head = Pivot("HeadAim", model.Body, new Vector3(0, 1.43f, 0));
            Part("Head", model.Head, new Vector3(0, .135f, 0), new Vector3(.365f, .40f, .335f), 1);
            Part("Hair", model.Head, new Vector3(0, .304f, -.025f), new Vector3(.378f, .115f, .33f), 2);
            Part("HairBack", model.Head, new Vector3(0, .20f, -.16f), new Vector3(.345f, .21f, .045f), 2);
            for (var side = -1; side <= 1; side += 2)
            {
                Part(side < 0 ? "LeftEar" : "RightEar", model.Head,
                    new Vector3(side * .182f, .12f, -.005f), new Vector3(.063f, .10f, .10f), 1);
                Part(side < 0 ? "LeftEye" : "RightEye", model.Head,
                    new Vector3(side * .085f, .158f, .168f), new Vector3(.033f, .039f, .018f), 2);
                var brow = Part(side < 0 ? "LeftBrow" : "RightBrow", model.Head,
                    new Vector3(side * .085f, .211f, .17f), new Vector3(.102f, .023f, .025f), 2);
                brow.transform.localRotation = Quaternion.Euler(0, 0, side * 12f);
            }
            Part("Nose", model.Head, new Vector3(0, .10f, .182f), new Vector3(.065f, .075f, .075f), 1);
            Part("Mouth", model.Head, new Vector3(0, .035f, .169f), new Vector3(.095f, .016f, .018f), 2);
            model.Arms = Pivot("Aim", model.Body, new Vector3(0, 1.30f, 0));
            // Bent elbows converge on the rifle grip and handguard instead of parallel forward blocks.
            Limb("LeftSleeve", model.Arms, new Vector3(-.25f, .025f, 0), new Vector3(-.26f, -.20f, .21f), .16f, 0);
            Limb("LeftForearm", model.Arms, new Vector3(-.26f, -.20f, .21f), new Vector3(.035f, -.135f, .535f), .115f, 1);
            Limb("RightSleeve", model.Arms, new Vector3(.25f, .025f, 0), new Vector3(.28f, -.22f, .15f), .16f, 0);
            Limb("RightForearm", model.Arms, new Vector3(.28f, -.22f, .15f), new Vector3(.08f, -.145f, .34f), .115f, 1);
            Part("LeftHand", model.Arms, new Vector3(.035f, -.135f, .535f), new Vector3(.125f, .10f, .13f), 1);
            Part("RightHand", model.Arms, new Vector3(.08f, -.145f, .34f), new Vector3(.105f, .12f, .10f), 1);
            var gun = Part("Rifle", model.Arms, new Vector3(.07f, -.08f, .37f), Vector3.one, 2);
            gun.GetComponent<MeshFilter>().sharedMesh = _gun;
            gun.GetComponent<MeshRenderer>().sharedMaterial = _gunMaterial;
            model.LeftLeg = Leg("LeftLeg", model.Body, -.135f);
            model.RightLeg = Leg("RightLeg", model.Body, .135f);
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
            Part("Trouser", leg, new Vector3(0, -.325f, 0), new Vector3(.215f, .65f, .25f), 0);
            Part("Boot", leg, new Vector3(0, -.73f, .045f), new Vector3(.23f, .18f, .36f), 2);
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

        private void Limb(string name, Transform parent, Vector3 from, Vector3 to, float width, int material)
        {
            var part = Part(name, parent, (from + to) * .5f, new Vector3(width, width, (to - from).magnitude), material);
            part.transform.localRotation = Quaternion.LookRotation(to - from);
        }

        private static Mesh BuildCube()
        {
            var mesh = new ModelMeshBuilder();
            mesh.BeveledBox(Vector3.zero, Vector3.one * .5f, .13f, new Color32(255, 255, 255, 255), Quaternion.identity);
            return mesh.Finish("ChenSirBeveledBox");
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
            Kill(_gunMaterial);
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
