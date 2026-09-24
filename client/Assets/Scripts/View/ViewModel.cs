using UnityEngine;

namespace Ac.View
{
    // C09 §5(a)：视图模型挂载与枪口挂点。参数写成常量并被用例断言。
    public sealed class ViewModel
    {
        // 冻结值只有一份：C05 的 ViewModelAnchor（本层不重复声明 62/0.01/12/基座偏移）
        public const float ModelFovDeg = (float)ViewModelAnchor.Fov;
        public const float NearClipM = (float)ViewModelAnchor.NearClipMeters;
        public const float FarClipM = (float)ViewModelAnchor.FarClipMeters;
        public static readonly Vector3 BaseOffset = new Vector3((float)ViewModelAnchor.OffsetX, (float)ViewModelAnchor.OffsetY, (float)ViewModelAnchor.OffsetZ);
        public static readonly Vector3 MuzzleOffset = new Vector3(0.0f, 0.02f, 0.58f);
        public const float MinMuzzleEyeDistanceM = 0.5f;

        private Transform _eye;
        private int _fireCount;

        public Vector3 EyeWorld { get; private set; }        // 眼位（相机原点）
        public Vector3 BaseWorld { get; private set; }       // 视图模型基座（相机空间基准位）
        public Vector3 MuzzleWorld { get; private set; }
        public int FireCount { get { return _fireCount; } }

        public void Attach(Camera camera)
        {
            _eye = camera == null ? null : camera.transform;
            Refresh();
        }

        public void Refresh()
        {
            if (_eye == null) { EyeWorld = Vector3.zero; BaseWorld = BaseOffset; MuzzleWorld = MuzzleOffset; return; }
            EyeWorld = _eye.position;
            BaseWorld = EyeWorld + _eye.rotation * BaseOffset;
            MuzzleWorld = EyeWorld + _eye.rotation * MuzzleOffset;
        }

        // §9：开火转发（枪口火焰由 Effects 负责，这里只记录与刷新挂点）
        public void OnFire() { _fireCount += 1; Refresh(); }

        public float MuzzleEyeDistanceM { get { return Vector3.Distance(EyeWorld, MuzzleWorld); } }
    }
}
