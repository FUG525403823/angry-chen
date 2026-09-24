namespace Ac.Net
{
    // S03 §5.4 / codec.hpp 的 EventIdTracker：eventId 是全局幂等键，且按「必须严格递增」判定——
    // 接收侧跨帧共用一份（服务端同名同语义），因此事件解码不能各自持有局部去重集合。
    public sealed class EventIdTracker
    {
        public uint MaxEventId { get; private set; }

        public bool IsNew(uint eventId)
        {
            if (eventId == 0u || eventId <= MaxEventId) return false;
            MaxEventId = eventId;
            return true;
        }

        public void Reset()
        {
            MaxEventId = 0u;
        }
    }
}
