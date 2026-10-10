namespace Ac.Net
{
    // S16 type 12 UpgradeSelect：波间购买升级。载荷 = upgradeId u8（0..3），
    // 与 server/src/net/codec.cpp 的 encodeUpgradeSelect / kUpgradeSelectPayloadBytes 同口径。
    // 只做结构校验；"能不能买"（相位 / 点数 / 等级上限）归服务端裁决，客户端不做重复规则。
    public static class UpgradeCodec
    {
        public const int PayloadBytes = 1;

        // upgradeId 越界（>= kUpgradeCount）时返回 false：调用方不发送，而不是发出去等拒收。
        public static bool TryEncode(int upgradeId, out byte[] payload)
        {
            payload = null;
            if (upgradeId < 0 || upgradeId >= Sim.UpgradeTable.Count) return false;
            payload = new byte[PayloadBytes];
            payload[0] = (byte)upgradeId;
            return true;
        }
    }
}
