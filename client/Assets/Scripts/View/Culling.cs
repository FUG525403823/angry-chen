using UnityEngine;

namespace Ac.View
{
    // C08 §5(e)：6 平面视锥 + 距离 + 屏幕占比三级剔除；输出复用调用方给的索引缓冲（零分配）。
    public static class Culling
    {
        public const double CullDistanceM = 90.0;
        public const double CullMinScreenRatio = 0.0015;
        public const int PlaneCount = 6;
        public const float MinDistanceM = 0.01f;   // 贴脸时不做占比判定，免得除零

        // 静态缓冲：Filter/IsVisible 共用，故 Culling 不可重入（单线程渲染主循环里够用）
        private static readonly Plane[] Planes = new Plane[PlaneCount];

        public static int Filter(Camera camera, SheepInstancePool pool, int[] visible)
        {
            if (camera == null || pool == null || visible == null) return 0;
            GeometryUtility.CalculateFrustumPlanes(camera, Planes);
            var eye = camera.transform.position;
            var fovTan = Mathf.Tan(camera.fieldOfView * 0.5f * Mathf.Deg2Rad);
            var instances = pool.Instances;
            var count = 0;
            for (var i = 0; i < instances.Length; i++)
            {
                var instance = instances[i];
                if (!instance.Visible) continue;
                var column = instance.Transform.GetColumn(3);
                var position = new Vector3(column.x, column.y, column.z);
                var distance = (position - eye).magnitude;
                if (distance > (float)CullDistanceM) continue;
                if (count >= visible.Length) break;

                var radius = (float)SheepMesh.Form((SheepKind)instance.Kind).RadiusM;   // 羊形半径已是世界尺度
                var inside = true;
                for (var p = 0; p < PlaneCount; p++)
                {
                    if (Planes[p].GetDistanceToPoint(position) < -radius) { inside = false; break; }
                }
                if (!inside) continue;

                // 屏幕占比：以视口高度为基准的近似（半径 / (距离 × tan(fov/2))）
                if (distance > MinDistanceM && radius / (distance * fovTan) < (float)CullMinScreenRatio) continue;
                visible[count] = i;
                count += 1;
            }
            return count;
        }

        // 单点判定（供调试与用例逐条核对）
        public static bool IsVisible(Camera camera, Vector3 position, float radius, out float screenRatio)
        {
            screenRatio = 0f;
            if (camera == null) return false;
            GeometryUtility.CalculateFrustumPlanes(camera, Planes);
            var eye = camera.transform.position;
            var distance = (position - eye).magnitude;
            if (distance > (float)CullDistanceM) return false;
            for (var p = 0; p < PlaneCount; p++) if (Planes[p].GetDistanceToPoint(position) < -radius) return false;
            var fovTan = Mathf.Tan(camera.fieldOfView * 0.5f * Mathf.Deg2Rad);
            screenRatio = distance > MinDistanceM ? radius / (distance * fovTan) : 1f;
            return screenRatio >= (float)CullMinScreenRatio;
        }
    }
}
