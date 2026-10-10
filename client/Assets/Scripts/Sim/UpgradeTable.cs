namespace Ac.Sim
{
    // server/src/config/upgrades.hpp 的客户端镜像（S16）。这里是**跨语言冻结值**：改任何一项都必须
    // 同时改服务端那份（ADR-010 §7 的契约变更流程）。新常量不进 fixture 的 configHash（导出真值来源是
    // 冻结的 v1，没有升级系统），一致性由本镜像 + 双侧常量断言测试保证（同 WeaponTable 镜像模式）。
    public static class UpgradeTable
    {
        public const int Count = 4;          // kUpgradeCount
        public const int MaxLevel = 5;       // kUpgradeMaxLevel
        public const int PointsPerWaveClear = 1;  // kPointsPerWaveClear

        // 升级 id（与 MatchState/UpgradeSelect 的线上编号一致）：0 伤害 / 1 移速 / 2 换弹 / 3 备弹。
        public const int Damage = 0;
        public const int Speed = 1;
        public const int Reload = 2;
        public const int Reserve = 3;

        // 每级增量（乘数公式见下）。
        public const float DamagePerLevel = 0.15f;
        public const float SpeedPerLevel = 0.08f;
        public const float ReloadTimePerLevel = 0.12f;
        public const int ReservePerLevel = 40;

        // 换弹时间乘数下限：等级 5 时 1.0 - 0.6 = 0.4（不为 0）。
        public const float ReloadTimeMinMultiplier = 0.4f;

        public static float DamageMultiplier(int level) => 1f + DamagePerLevel * ClampLevel(level);
        public static float SpeedMultiplier(int level) => 1f + SpeedPerLevel * ClampLevel(level);
        public static float ReloadTimeMultiplier(int level)
        {
            var mult = 1f - ReloadTimePerLevel * ClampLevel(level);
            return mult < ReloadTimeMinMultiplier ? ReloadTimeMinMultiplier : mult;
        }

        public static int ReserveBonus(int level) => ReservePerLevel * ClampLevel(level);

        private static int ClampLevel(int level)
        {
            return level < 0 ? 0 : (level > MaxLevel ? MaxLevel : level);
        }
    }
}
