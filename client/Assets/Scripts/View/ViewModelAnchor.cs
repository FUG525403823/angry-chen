namespace Ac.View
{
    // C05 §5.5：视图模型挂点参数（不含武器动画，动画留给 C09）。
    public static class ViewModelAnchor
    {
        public const double Fov = 62.0;
        public const double NearClipMeters = 0.01;
        public const double FarClipMeters = 12.0;

        // §5.5：相机局部坐标基座偏移 (x, y, z)。
        public const double OffsetX = 0.17;
        public const double OffsetY = -0.19;
        public const double OffsetZ = -0.02;
    }
}
