using UnityEngine;

namespace Ac.View
{
    // C09 §9：视图模型的程序化网格（ADR-003：零外部素材，全部由代码生成）。
    // 三种武器各一份：手枪 / 步枪 / 霰弹枪。坐标是**相机空间**：+Z 指向准星方向，+X 向右，+Y 向上；
    // 网孔原点即"持枪手位"，由 PresentationLayer 挂到相机下的 ViewModel.BaseOffset 处。
    public static class WeaponMesh
    {
        public const int SlotCount = 3;
        // 枪口在相机空间的位置（相对网格原点）：开火特效与抖动都参考它
        public static readonly Vector3 MuzzleLocal = new Vector3(0f, 0.012f, 0.30f);

        // 枪身配色（顶点色）：枪体深灰、木质件棕、弹匣略深、枪口内部近黑
        private static readonly Color32 Body = new Color32(58, 60, 66, 255);
        private static readonly Color32 BodyDark = new Color32(38, 39, 44, 255);
        private static readonly Color32 Wood = new Color32(96, 66, 42, 255);
        private static readonly Color32 Metal = new Color32(96, 99, 108, 255);

        // 两套网格：枪体（深灰）与配件（钢/木）各一套，分别给不同材质，
        // 出包里才有层次（单材质就是一块黑色轮廓）。
        public static Mesh BuildAccent(int slot)
        {
            if (slot < 0 || slot >= SlotCount) slot = 0;
            var vertices = new Vector3[8 * 12];
            var indices = new int[36 * 12];
            var colors = new Color32[8 * 12];
            var v = 0;
            var n = 0;

            // 手位：整枪向右下偏移一点，枪口朝 +Z。配件体：金属件与木件
            switch (slot)
            {
                case 1:   // 步枪：枪管 + 弹匣 + 眨具 + 木托
                    Box(vertices, indices, colors, ref v, ref n, new Vector3(0f, 0.006f, 0.20f), new Vector3(0.012f, 0.012f, 0.090f), Metal);
                    Box(vertices, indices, colors, ref v, ref n, new Vector3(0f, -0.004f, -0.10f), new Vector3(0.024f, 0.034f, 0.075f), Wood);
                    Box(vertices, indices, colors, ref v, ref n, new Vector3(0f, -0.040f, 0.05f), new Vector3(0.017f, 0.038f, 0.026f), BodyDark);
                    Box(vertices, indices, colors, ref v, ref n, new Vector3(0f, 0.048f, 0.02f), new Vector3(0.010f, 0.010f, 0.055f), Metal);
                    Box(vertices, indices, colors, ref v, ref n, new Vector3(0f, 0.006f, 0.128f), new Vector3(0.016f, 0.016f, 0.020f), Metal);
                    break;
                case 2:   // 霰弹枪：抽动护木 + 双管 + 木托
                    Box(vertices, indices, colors, ref v, ref n, new Vector3(0f, 0.014f, 0.10f), new Vector3(0.017f, 0.017f, 0.150f), Metal);
                    Box(vertices, indices, colors, ref v, ref n, new Vector3(0f, -0.014f, 0.06f), new Vector3(0.024f, 0.024f, 0.070f), Wood);
                    Box(vertices, indices, colors, ref v, ref n, new Vector3(0f, -0.010f, -0.09f), new Vector3(0.026f, 0.036f, 0.080f), Wood);
                    Box(vertices, indices, colors, ref v, ref n, new Vector3(0f, 0.006f, 0.22f), new Vector3(0.010f, 0.010f, 0.030f), BodyDark);
                    Box(vertices, indices, colors, ref v, ref n, new Vector3(0f, -0.038f, 0.10f), new Vector3(0.020f, 0.020f, 0.055f), Wood);
                    break;
                default:  // 手枪：套筒 + 枪口 + 准星
                    Box(vertices, indices, colors, ref v, ref n, new Vector3(0f, 0.004f, 0.145f), new Vector3(0.010f, 0.010f, 0.022f), Metal);
                    Box(vertices, indices, colors, ref v, ref n, new Vector3(0f, 0.030f, 0.075f), new Vector3(0.006f, 0.006f, 0.014f), Metal);
                    Box(vertices, indices, colors, ref v, ref n, new Vector3(0f, 0.030f, 0.020f), new Vector3(0.006f, 0.006f, 0.010f), Metal);
                    break;
            }
            var mesh = new Mesh();
            mesh.name = "Weapon" + slot;
            mesh.vertices = Slice(vertices, v);
            mesh.SetTriangles(Slice(indices, n), 0);
            mesh.colors32 = Slice(colors, v);
            mesh.RecalculateNormals();
            mesh.RecalculateBounds();
            return mesh;
        }

        // 枪体（深灰主体）
        public static Mesh BuildBody(int slot)
        {
            if (slot < 0 || slot >= SlotCount) slot = 0;
            var vertices = new Vector3[8 * 12];
            var indices = new int[36 * 12];
            var colors = new Color32[8 * 12];
            var v = 0;
            var n = 0;
            switch (slot)
            {
                case 1:   // 步枪：机匣 + 握把
                    Box(vertices, indices, colors, ref v, ref n, new Vector3(0f, 0f, 0.06f), new Vector3(0.028f, 0.040f, 0.150f), Body);
                    Box(vertices, indices, colors, ref v, ref n, new Vector3(0f, -0.052f, -0.02f), new Vector3(0.020f, 0.048f, 0.030f), BodyDark);
                    break;
                case 2:   // 霰弹枪：机匣 + 握把
                    Box(vertices, indices, colors, ref v, ref n, new Vector3(0f, 0f, 0.02f), new Vector3(0.030f, 0.030f, 0.120f), Body);
                    Box(vertices, indices, colors, ref v, ref n, new Vector3(0f, -0.048f, -0.02f), new Vector3(0.020f, 0.044f, 0.028f), BodyDark);
                    break;
                default:  // 手枪：套筒 + 握把
                    Box(vertices, indices, colors, ref v, ref n, new Vector3(0f, 0.012f, 0.07f), new Vector3(0.020f, 0.024f, 0.075f), Body);
                    Box(vertices, indices, colors, ref v, ref n, new Vector3(0f, -0.040f, 0.02f), new Vector3(0.018f, 0.048f, 0.026f), BodyDark);
                    break;
            }
            var mesh = new Mesh();
            mesh.name = "WeaponBody" + slot;
            mesh.vertices = Slice(vertices, v);
            mesh.SetTriangles(Slice(indices, n), 0);
            mesh.colors32 = Slice(colors, v);
            mesh.RecalculateNormals();
            mesh.RecalculateBounds();
            return mesh;
        }
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

        // 轴对齐长方体（8 顶点 / 12 三角形），带顶点色：够用的枪械轮廓，零素材。
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

            // 背面（-Z）/ 正面（+Z）
            Quad(indices, ref n, baseIndex + 0, baseIndex + 1, baseIndex + 2, baseIndex + 3);
            Quad(indices, ref n, baseIndex + 4, baseIndex + 5, baseIndex + 6, baseIndex + 7);
            Quad(indices, ref n, baseIndex + 0, baseIndex + 4, baseIndex + 5, baseIndex + 1);   // 底
            Quad(indices, ref n, baseIndex + 3, baseIndex + 7, baseIndex + 6, baseIndex + 2);   // 顶
            Quad(indices, ref n, baseIndex + 0, baseIndex + 3, baseIndex + 7, baseIndex + 4);   // 左
            Quad(indices, ref n, baseIndex + 1, baseIndex + 2, baseIndex + 6, baseIndex + 5);   // 右
        }

        private static void Quad(int[] indices, ref int n, int a, int b, int c, int d)
        {
            indices[n++] = a; indices[n++] = b; indices[n++] = c;
            indices[n++] = a; indices[n++] = c; indices[n++] = d;
        }
    }
}
