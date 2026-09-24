namespace Ac.View
{
    public enum DrawMode : byte { Single = 0, Instanced = 1, Combined = 2 }

    // C14 §5 实例化/合批/剔除阈值与画质档映射。这里只做"决策"，不碰渲染 API：
    // 真机由装配步骤把 Decide/BatchCount 的结果交给 Graphics.DrawMeshInstanced / RenderMesh。
    public static class Batching
    {
        public const int InstanceThreshold = 25;        // 同类羊可见数 >= 25 走实例化
        public const int MaxInstancesPerBatch = 1023;
        public const int CombineMeshThreshold = 8;      // 静态几何同材质 >= 8 个 mesh 合并
        public const float SheepCullDistanceM = 60f;
        public const float ArenaCullDistanceM = 80f;
        public const int TierCount = 3;

        public static readonly float[] ShadowDistanceM = { 20f, 35f, 50f };
        public static readonly int[] InstanceCap = { 256, 512, 1024 };
        public static readonly int[] ParticleCap = { 96, 160, 256 };

        public static int ClampTier(int tier)
        {
            return tier < 0 ? 0 : (tier >= TierCount ? TierCount - 1 : tier);
        }

        public static DrawMode Decide(int visibleSameKind)
        {
            return visibleSameKind >= InstanceThreshold ? DrawMode.Instanced : DrawMode.Single;
        }

        public static bool ShouldCombineStatic(int sameMaterialMeshCount)
        {
            return sameMaterialMeshCount >= CombineMeshThreshold;
        }

        // 一批最多 1023 个实例，且不超过画质档的实例化上限
        public static int BatchCount(int instances, int qualityTier)
        {
            if (instances <= 0) return 0;
            var perBatch = MaxInstancesPerBatch;
            var cap = InstanceCap[ClampTier(qualityTier)];
            if (cap < perBatch) perBatch = cap;
            return (instances + perBatch - 1) / perBatch;
        }

        public static int VisibleInstances(int wanted, int qualityTier)
        {
            if (wanted <= 0) return 0;
            var cap = InstanceCap[ClampTier(qualityTier)];
            return wanted > cap ? cap : wanted;
        }

        // ---- C14 §9 冻结接口：SetQualityTier / DrawCallLimit / CullDistanceMeters(int kind) ----
        // 计划只冻结了签名、没冻结 kind 的取值：这里取 0 = 羊、1 = 场地（§5 的"羊 60m、场地 80m"），
        // 已登记待裁决。
        public const int CullKindSheep = 0;
        public const int CullKindArena = 1;

        private static int _qualityTier;

        public static int QualityTier { get { return _qualityTier; } }

        // 当前画质档（0..2，越界夹取）。低档先保帧率，是 §5 档位表的选择器。
        public static void SetQualityTier(int tier) { _qualityTier = ClampTier(tier); }

        // 本档位允许的每帧 draw call 上限：与 §5 预算表同源，不再写第二份 120。
        public static int DrawCallLimit { get { return Ac.Core.FrameBudget.DrawCallBudget; } }

        public static float CullDistanceMeters(int kind)
        {
            return kind == CullKindArena ? ArenaCullDistanceM : SheepCullDistanceM;
        }

        public static bool WithinCullDistanceMeters(float distanceM, int kind)
        {
            return distanceM <= CullDistanceMeters(kind);
        }

        // 兼容旧调用点（计划冻结的是上面那个 int 版）。新代码请用 CullDistanceMeters。
        public static float CullDistanceM(bool isSheep) { return CullDistanceMeters(isSheep ? CullKindSheep : CullKindArena); }

        public static bool WithinCullDistance(float distanceM, bool isSheep)
        {
            if (float.IsNaN(distanceM) || distanceM < 0f) return false;
            return distanceM <= CullDistanceM(isSheep);
        }

        public static float ShadowDistanceFor(int qualityTier) { return ShadowDistanceM[ClampTier(qualityTier)]; }
        public static int ParticleCapFor(int qualityTier) { return ParticleCap[ClampTier(qualityTier)]; }
    }

    // 池化：稳态下 Rent/Return 不产生分配（数组只增不减，扩容走倍增）
    public sealed class InstancePool
    {
        public const int InitialCapacity = 64;

        private int[] _ids = new int[InitialCapacity];
        private int[] _free = new int[InitialCapacity];
        private bool[] _inUse = new bool[InitialCapacity];
        private int _freeCount;
        private int _next;
        private int _live;
        private int _peakLive;
        private int _growthCount;

        public int Capacity { get { return _ids.Length; } }
        public int Live { get { return _live; } }
        public int Pooled { get { return _ids.Length - _live; } }
        public int PeakLive { get { return _peakLive; } }
        public int GrowthCount { get { return _growthCount; } }
        public int RentCount { get; private set; }
        public int ReturnCount { get; private set; }

        public bool IsInUse(int handle) { return handle >= 0 && handle < _ids.Length && _inUse[handle]; }

        // 句柄必须能按任意顺序归还：交换删除会让外部持有的旧句柄指向别的实体，所以用空闲栈
        public int Rent(int entityId)
        {
            int handle;
            if (_freeCount > 0)
            {
                _freeCount -= 1;
                handle = _free[_freeCount];
            }
            else
            {
                if (_next >= _ids.Length) Grow();
                handle = _next;
                _next += 1;
            }
            _ids[handle] = entityId;
            _inUse[handle] = true;
            _live += 1;
            RentCount += 1;
            if (_live > _peakLive) _peakLive = _live;
            return handle;
        }

        public bool Return(int handle)
        {
            if (!IsInUse(handle)) return false;
            _inUse[handle] = false;
            _ids[handle] = 0;
            _free[_freeCount] = handle;
            _freeCount += 1;
            _live -= 1;
            ReturnCount += 1;
            return true;
        }

        public int EntityAt(int handle) { return IsInUse(handle) ? _ids[handle] : 0; }

        public void Clear()
        {
            for (var i = 0; i < _ids.Length; i++) { _ids[i] = 0; _inUse[i] = false; }
            _freeCount = 0;
            _next = 0;
            _live = 0;
        }

        public void Reset()
        {
            _ids = new int[InitialCapacity];
            _free = new int[InitialCapacity];
            _inUse = new bool[InitialCapacity];
            _freeCount = 0;
            _next = 0;
            _live = 0;
            _peakLive = 0;
            _growthCount = 0;
            RentCount = 0;
            ReturnCount = 0;
        }

        private void Grow()
        {
            var size = _ids.Length * 2;
            var ids = new int[size];
            System.Array.Copy(_ids, ids, _ids.Length);
            _ids = ids;
            var free = new int[size];
            System.Array.Copy(_free, free, _free.Length);
            _free = free;
            var inUse = new bool[size];
            System.Array.Copy(_inUse, inUse, _inUse.Length);
            _inUse = inUse;
            _growthCount += 1;
        }
    }
}
