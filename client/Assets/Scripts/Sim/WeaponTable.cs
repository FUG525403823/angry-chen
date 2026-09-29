namespace Ac.Sim
{
    // server/src/config/weapons.hpp 的客户端镜像（C10 准星散布的基线来源）。
    // 这里是**跨语言冻结值**：改任何一项都必须同时改服务端那份（ADR-010 §7 的契约变更流程），
    // 客户端内部也只留这一份——WeaponAnim 的同名常量从这里取，不再各写一个字面量。
    public static class WeaponTable
    {
        public const int SlotCount = 3;   // 0 手枪 / 1 步枪 / 2 霰弹（v1 WEAPON_SLOT_ORDER）

        // kWeapons[slot].spreadDeg（手枪 0.8 / 步枪 0.6 / 霰弹 4.0）
        public static readonly float[] BaseSpreadDeg = { 0.8f, 0.6f, 4.0f };

        // kWeapons[slot].mag（手枪 12 / 步枪 30 / 霰弹 6）——HUD 的低弹色判据用它；
        // 此前客户端没有这张表，sample.MagSize 恒 0，弹药行只能显示原始数字、低弹色永远不触发。
        public static readonly int[] MagSizePx = { 12, 30, 6 };

        public static int MagSizeOf(int slot) { return MagSizePx[ClampSlot(slot)]; }

        public const float SpreadGrowthPerShotDeg = 0.1f;    // kSpreadGrowthPerShotDeg
        public const float SpreadMaxDeg = 0.25f;             // kSpreadMaxDeg
        public const float SpreadDecayDelayMs = 350f;        // kSpreadDecayDelayMs
        public const float SpreadDecayPerSecondDeg = 6.0f;   // kSpreadDecayPerSecondDeg

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
