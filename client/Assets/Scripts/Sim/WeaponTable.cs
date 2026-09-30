namespace Ac.Sim
{
    // server/src/config/weapons.hpp 的客户端镜像（C10 准星散布的基线来源 + C06 §5(c) 本地开火镜像的
    // 射速/弹匣/换弹来源）。这里是**跨语言冻结值**：改任何一项都必须同时改服务端那份（ADR-010 §7 的
    // 契约变更流程），客户端内部也只留这一份——WeaponAnim 的同名常量从这里取，不再各写一个字面量。
    // 刻意不镜像的字段：damage / falloffStartM / falloffPerM / headshotMultiplier（纯服务端结算）、
    // isAuto（服务端只用"持续按住 = 持续开火 + 射速间隔"把关，两端都不用这一位）。
    public static class WeaponTable
    {
        public const int SlotCount = 3;   // 0 手枪 / 1 步枪 / 2 霰弹（v1 WEAPON_SLOT_ORDER）

        // kWeapons[slot].spreadDeg（手枪 0.8 / 步枪 0.6 / 霰弹 4.0）
        public static readonly float[] BaseSpreadDeg = { 0.8f, 0.6f, 4.0f };

        // kWeapons[slot].mag（手枪 12 / 步枪 30 / 霰弹 6）——HUD 的低弹色判据用它；
        // 此前客户端没有这张表，sample.MagSize 恒 0，弹药行只能显示原始数字、低弹色永远不触发。
        public static readonly int[] MagSize = { 12, 30, 6 };

        // kWeapons[slot].rpm（手枪 300 / 步枪 600 / 霰弹 70）：开火间隔 = 60000 / (rpm × 倍率)。
        public static readonly float[] Rpm = { 300f, 600f, 70f };

        // kWeapons[slot].pellets（手枪 1 / 步枪 1 / 霰弹 8）：一发子弹打几条射线（服务端结算，
        // 客户端只需要它来解释"一次开火可能来多条命中事件"）。
        public static readonly int[] Pellets = { 1, 1, 8 };

        // kWeapons[slot].reloadMs（手枪 1400 / 步枪 2000 / 霰弹 2600）
        public static readonly float[] ReloadMs = { 1400f, 2000f, 2600f };

        public static int MagSizeOf(int slot) { return MagSize[ClampSlot(slot)]; }

        public const float SpreadGrowthPerShotDeg = 0.1f;    // kSpreadGrowthPerShotDeg
        public const float SpreadMaxDeg = 0.25f;             // kSpreadMaxDeg
        public const float SpreadDecayDelayMs = 350f;        // kSpreadDecayDelayMs
        public const float SpreadDecayPerSecondDeg = 6.0f;   // kSpreadDecayPerSecondDeg

        // kReserveAmmoInitial：初始备弹（本地镜像的起始值，权威值仍以 MatchState 为准）。
        public const int ReserveAmmoInitial = 120;

        // kRecoilPitchPerShotDeg / kRecoilYawJitterDeg：C09 §5 冻结的每发后坐（俯仰 0.35°、偏航 ±0.2°），
        // WeaponAnim 与采样器注入面共用这一份。
        public const float RecoilPitchPerShotDeg = 0.35f;
        public const float RecoilYawJitterDeg = 0.2f;

        // kAimPitchLimitRad：**射击方向**的俯仰夹取（1.5533）。采样器的瞄准夹取是 π/2（1.5708），
        // 两者差 1°，所以本地预演射线时要用服务端这一份。
        public const float AimPitchLimitRad = 1.5533f;
        public const float DegToRad = 0.017453292519943295f;   // kDegToRad
        public const float ShotMaxDistanceM = 160f;            // kShotMaxDistanceM

        // §5.1：射速间隔 = 60000 / (rpm × mult)；公式现算，不写近似常数（霰弹枪 60000/70）。
        // 非法输入（<= 0）返回 +∞，与服务端 rpmToIntervalMs 同语义（那一段是防御性代码，
        // 客户端这张表是常量表，用 float.PositiveInfinity 表示"这一发之后永远打不了"）。
        public static float FireIntervalMs(int slot, float fireRateMultiplier)
        {
            var effective = Rpm[ClampSlot(slot)] * fireRateMultiplier;
            return effective > 0f ? 60000f / effective : float.PositiveInfinity;
        }

        public static int ClampSlot(int slot)
        {
            return slot < 0 ? 0 : (slot >= SlotCount ? SlotCount - 1 : slot);
        }

        public static float BaseSpreadOf(int slot) { return BaseSpreadDeg[ClampSlot(slot)]; }

        // 准星散布的输入（度）= 该槽位的基线 + 本帧的射击累计。
        // 相加的结果可能超出准星的显示域（[0.5°, 5°]），夹取由 Crosshair.SetSpread 负责。
        public static float CrosshairSpreadDeg(int slot, float shotSpreadDeg)
        {
            return BaseSpreadOf(slot) + shotSpreadDeg;
        }
    }
}
