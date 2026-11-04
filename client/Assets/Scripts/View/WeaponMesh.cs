using UnityEngine;

namespace Ac.View
{
    // Procedural, cached view models. +Z follows the barrel and +Y is the top rail.
    public static class WeaponMesh
    {
        public const int SlotCount = 3;
        public static readonly Vector3 MuzzleLocal = new Vector3(0f, .012f, .30f);
        private static readonly Color32 Body = new Color32(83, 94, 108, 255);
        private static readonly Color32 Dark = new Color32(32, 38, 47, 255);
        private static readonly Color32 Wood = new Color32(153, 95, 56, 255);
        private static readonly Color32 Metal = new Color32(150, 163, 174, 255);
        private static readonly Color32 Bore = new Color32(12, 16, 22, 255);

        // Asset-backed vertex color shader: wood, steel and the bore retain their colors in builds.
        public static Material CreateMaterial(string name) { return ArenaMaterials.CreateSheep(name); }

        public static Mesh BuildBody(int slot)
        {
            slot = ValidSlot(slot);
            var mesh = new ModelMeshBuilder();
            if (slot == 0)
            {
                Box(mesh, new Vector3(0, .012f, .163f), new Vector3(.026f, .026f, .119f), Body, .007f);
                Box(mesh, new Vector3(0, -.021f, .116f), new Vector3(.024f, .014f, .096f), Dark);
                Box(mesh, new Vector3(0, -.072f, .062f), new Vector3(.023f, .052f, .031f), Wood, .006f, 14f);
                TriggerGuard(mesh, .146f);
            }
            else if (slot == 1)
            {
                Box(mesh, new Vector3(0, 0, .047f), new Vector3(.035f, .037f, .101f), Body, .007f);
                Box(mesh, new Vector3(0, .003f, .190f), new Vector3(.030f, .028f, .051f), Wood, .006f);
                Box(mesh, new Vector3(0, -.066f, -.026f), new Vector3(.022f, .047f, .028f), Dark, .005f, 15f);
                Box(mesh, new Vector3(0, -.073f, .059f), new Vector3(.024f, .052f, .029f), Dark, .005f, -10f);
            }
            else
            {
                Box(mesh, new Vector3(0, 0, .035f), new Vector3(.033f, .031f, .087f), Body, .006f);
                Box(mesh, new Vector3(0, -.015f, -.116f), new Vector3(.029f, .042f, .071f), Wood, .010f, 9f);
                Box(mesh, new Vector3(0, -.055f, -.032f), new Vector3(.022f, .038f, .034f), Wood, .006f, 23f);
                TriggerGuard(mesh, .035f);
            }
            return mesh.Finish("WeaponBody" + slot);
        }

        public static Mesh BuildAccent(int slot)
        {
            slot = ValidSlot(slot);
            var mesh = new ModelMeshBuilder();
            if (slot == 0)
            {
                mesh.Barrel(new Vector3(0, MuzzleLocal.y, .280f), .016f, .009f, .040f, Metal, Bore);
                Box(mesh, new Vector3(0, .043f, .258f), new Vector3(.005f, .006f, .010f), Metal, .001f);
                for (var side = -1; side <= 1; side += 2)
                    Box(mesh, new Vector3(side * .016f, .044f, .064f), new Vector3(.005f, .007f, .009f), Metal, .001f);
                mesh.Panel(new Vector3(.0261f, .015f, .180f), new Vector3(0, .010f, 0), new Vector3(0, 0, .023f), Vector3.right, Bore);
                for (var side = -1; side <= 1; side += 2)
                    for (var i = 0; i < 4; i++)
                        mesh.Panel(new Vector3(side * .0261f, .011f, .080f + i * .010f), new Vector3(0, .012f, 0), new Vector3(0, 0, .0014f), Vector3.right * side, Dark);
            }
            else if (slot == 1)
            {
                mesh.Barrel(new Vector3(0, MuzzleLocal.y, .266f), .014f, .008f, .068f, Metal, Bore);
                Box(mesh, new Vector3(0, -.008f, -.121f), new Vector3(.031f, .042f, .069f), Wood, .008f);
                Box(mesh, new Vector3(0, -.008f, -.191f), new Vector3(.034f, .046f, .008f), Dark);
                Box(mesh, new Vector3(0, .042f, .060f), new Vector3(.021f, .006f, .090f), Dark, .002f);
                Box(mesh, new Vector3(0, .056f, -.004f), new Vector3(.014f, .010f, .012f), Metal, .002f);
                Box(mesh, new Vector3(0, .050f, .224f), new Vector3(.006f, .017f, .009f), Metal, .002f);
                Box(mesh, new Vector3(.042f, .008f, .033f), new Vector3(.012f, .006f, .014f), Metal, .002f);
                for (var side = -1; side <= 1; side += 2)
                    for (var i = 0; i < 4; i++)
                        mesh.Panel(new Vector3(side * .0301f, .006f, .158f + i * .018f), new Vector3(0, .013f, 0), new Vector3(0, 0, .002f), Vector3.right * side, Dark);
            }
            else
            {
                mesh.Barrel(new Vector3(0, MuzzleLocal.y, .205f), .020f, .013f, .190f, Metal, Bore);
                mesh.Barrel(new Vector3(0, -.030f, .204f), .013f, .006f, .148f, Dark, Bore);
                Box(mesh, new Vector3(0, -.027f, .162f), new Vector3(.031f, .025f, .055f), Wood, .007f);
                Box(mesh, new Vector3(0, -.019f, -.193f), new Vector3(.032f, .046f, .009f), Dark, .004f, 9f);
                Box(mesh, new Vector3(0, .037f, .282f), new Vector3(.004f, .005f, .007f), Metal, .001f);
                mesh.Panel(new Vector3(.0331f, .004f, .047f), new Vector3(0, .013f, 0), new Vector3(0, 0, .027f), Vector3.right, Bore);
                for (var side = -1; side <= 1; side += 2)
                    for (var i = 0; i < 5; i++)
                        mesh.Panel(new Vector3(side * .0311f, -.027f, .128f + i * .017f), new Vector3(0, .015f, 0), new Vector3(0, 0, .0015f), Vector3.right * side, Dark);
            }
            return mesh.Finish("WeaponAccent" + slot);
        }

        // Teammates use a complete rifle in one instancing group.
        public static Mesh BuildComplete(int slot)
        {
            var body = BuildBody(slot); var accent = BuildAccent(slot);
            try
            {
                var mesh = new Mesh { name = "WeaponComplete" + ValidSlot(slot) };
                mesh.CombineMeshes(new[] { new CombineInstance { mesh = body, transform = Matrix4x4.identity },
                    new CombineInstance { mesh = accent, transform = Matrix4x4.identity } }, true, false);
                return mesh;
            }
            finally
            {
                if (Application.isPlaying) { Object.Destroy(body); Object.Destroy(accent); }
                else { Object.DestroyImmediate(body); Object.DestroyImmediate(accent); }
            }
        }

        private static int ValidSlot(int slot) { return slot < 0 || slot >= SlotCount ? 0 : slot; }
        private static void Box(ModelMeshBuilder mesh, Vector3 center, Vector3 half, Color32 color, float bevel = .003f, float pitch = 0)
        {
            mesh.BeveledBox(center, half, bevel, color, Quaternion.Euler(pitch, 0, 0));
        }
        private static void TriggerGuard(ModelMeshBuilder mesh, float z)
        {
            Box(mesh, new Vector3(0, -.062f, z), new Vector3(.018f, .005f, .036f), Dark, .002f);
            Box(mesh, new Vector3(0, -.044f, z + .031f), new Vector3(.018f, .018f, .005f), Dark, .002f);
        }
    }
}
