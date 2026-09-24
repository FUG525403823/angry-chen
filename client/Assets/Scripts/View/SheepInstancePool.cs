using UnityEngine;

namespace Ac.View
{
    // C08 §5(c)：1024 容量实例池。构造期一次性预分配，Reset() 只复位游标，池满拒绝并计数。
    public struct SheepInstance
    {
        public Matrix4x4 Transform;
        public Matrix4x4 EmblemTransform;   // §5(b) 额标挂点：头部偏移 × 羊形缩放
        public float Darken;                // §5(c) 尸体变暗是独立通道，不是缩放乘子
        public float HpRatio;
        public float EmblemIntensity;
        public byte Kind;
        public byte State;
        public bool Visible;
    }

    public sealed class SheepInstancePool
    {
        public const int EntityCapacity = 1024;
        public const int PerFormCapacity = 256;
        public const int FormCount = SheepMesh.FormCount;

        private readonly SheepInstance[] _instances = new SheepInstance[EntityCapacity];
        private readonly int[] _cursor = new int[FormCount];

        public int OverflowCount { get; private set; }
        public int Count { get; private set; }

        // 池只在构造期分配：这个引用在整局里都不变，零分配判据的一半就是"它没换过"
        public SheepInstance[] Instances { get { return _instances; } }

        public void Reset()
        {
            for (var i = 0; i < FormCount; i++) _cursor[i] = 0;
            // 上一帧写过的记录必须显式失活：Culling 只看 Visible，不清就会留幽灵羊
            for (var i = 0; i < Count; i++) _instances[i].Visible = false;
            Count = 0;
        }

        // §9：按羊形取一条实例记录；返回 -1 表示该形已满（拒绝绘制并计数）
        public int TryAcquire(int form, out int index)
        {
            index = -1;
            if (form < 0 || form >= FormCount) { OverflowCount += 1; return -1; }
            if (_cursor[form] >= PerFormCapacity) { OverflowCount += 1; return -1; }
            index = form * PerFormCapacity + _cursor[form];
            _cursor[form] += 1;
            Count += 1;
            return index;
        }

        public int CursorOf(int form) { return form < 0 || form >= FormCount ? 0 : _cursor[form]; }

    }
}
