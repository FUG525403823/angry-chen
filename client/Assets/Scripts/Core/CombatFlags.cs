namespace Ac.Core
{
    // 权威：server/src/config/combat.hpp 的 kHitFlagHeadshot / kHitFlagDowned / kHitFlagKilled = 1 / 2 / 4。
    // Ac.View（命中提示）与 Ac.UI（音频层）共用这一份，避免位掩码在多处同值双源。
    public static class CombatFlags
    {
        public const int HitFlagHeadshot = 1;
        public const int HitFlagDowned = 2;
        public const int HitFlagKilled = 4;
    }

    public static class BusCount
    {
        public const int MixBusCount = 4;      // MixBus: Master / Sfx / Music / Subtitle
    }
}
