using System;
using UnityEngine;

namespace Ac.View
{
    // C05 §5.5/§5.6：第一人称相机的参数与状态。与引擎的接触点（Cursor / Camera 组件）只在播放态生效，
    // 批处理自检里它就是一组可断言的纯状态。
    public sealed class FpsCamera
    {
        public const double DefaultFov = 75.0;
        public const double MinFov = 60.0;
        public const double MaxFov = 100.0;
        public const double NearClipMeters = 0.1;
        public const double FarClipMeters = 300.0;
        public const double EyeHeightMeters = 1.6;
        public const double PitchLimitRad = 1.5707963267948966;

        public FpsCamera()
        {
            Fov = DefaultFov;
            PitchRad = 0.0;
            YawRad = 0.0;
            Locked = false;
            Focused = true;
        }

        public double Fov { get; private set; }
        public double X { get; private set; }
        public double Y { get; private set; }
        public double Z { get; private set; }
        public double YawRad { get; private set; }
        public double PitchRad { get; private set; }
        public bool Locked { get; private set; }
        public bool Focused { get; private set; }

        // §5.5：FOV 可调范围 60..100，越界钳制而不是拒绝。
        public void ApplyFov(double fov)
        {
            if (fov < MinFov) fov = MinFov;
            if (fov > MaxFov) fov = MaxFov;
            Fov = fov;
        }

        public static double ClampPitch(double pitchRad)
        {
            if (pitchRad > PitchLimitRad) return PitchLimitRad;
            if (pitchRad < -PitchLimitRad) return -PitchLimitRad;
            return pitchRad;
        }

        // §5.5：相机位置 = 玩家位置 + eyeHeight（沿世界 Y）。
        public void SetPose(double x, double y, double z, double yawRad, double pitchRad)
        {
            X = x;
            Y = y + EyeHeightMeters;
            Z = z;
            YawRad = yawRad;
            PitchRad = ClampPitch(pitchRad);
        }

        // §5.6：点击画面锁定指针，锁定期间鼠标增量才计入视角。
        public void RequestPointerLock()
        {
            Locked = true;
            if (!Application.isPlaying) return;
            Cursor.lockState = CursorLockMode.Locked;
            Cursor.visible = false;
        }

        public void ReleasePointerLock()
        {
            Locked = false;
            if (!Application.isPlaying) return;
            Cursor.lockState = CursorLockMode.None;
            Cursor.visible = true;
        }

        public void OnFocusChanged(bool focused)
        {
            Focused = focused;
            if (!focused) ReleasePointerLock();
        }
    }
}
